#include "mdDrumsDevice.h"

#include "mdProtocol/mdautomation.h"
#include "mdProtocol/mdkitchange.h"
#include "mdProtocol/mdsysexcodec.h"

#include "synthLib/deviceException.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mdDrums
{
	namespace
	{
		namespace sysex = md::automation::sysex;
		namespace machinedrum = md::automation::machinedrum;
		constexpr auto g_model = md::MachineModel::Machinedrum;

		constexpr uint8_t g_kitDump = 0x52;

		// The Telemetry takes the engine's outputs and Tracks by their index
		static_assert(Telemetry::OutputCount == Engine::OutputCount && Telemetry::TrackCount == Engine::TrackCount);
	}

	Device::Device(const synthLib::DeviceCreateParams& _params, std::shared_ptr<Telemetry> _telemetry)
		: synthLib::Device(_params), m_telemetry(std::move(_telemetry))
	{
		const auto kit = messages::factoryKit(_params.romData);
		try
		{
			m_engine = std::make_unique<Engine>(_params.romData);
		}
		catch(const std::exception& _error)
		{
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
				std::string("The Machinedrum UW flash image with OS 1.63 could not be read: ") + _error.what());
		}
		if(!kit)
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
				"The Machinedrum UW flash image holds no factory Kits.");
		m_kit = *kit;
		applyKit();
		applyMixer();
		m_valid = true;
	}

	Device::~Device() = default;

#if SYNTHLIB_DEMO_MODE == 0
	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType)
	{
		// The Plugin layer has already written its version header: append
		const auto state = messages::writeState({m_kit, m_mixer});
		_state.insert(_state.end(), state.begin(), state.end());
		return true;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType)
	{
		const auto state = messages::parseState(_state.data(), _state.size());
		if(!state)
			return false;
		m_kit = state->kit;
		m_mixer = state->mixer;
		applyKit();
		applyMixer();
		return true;
	}
#endif

	void Device::process(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _size,
		const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		_midiOut.clear();
		size_t done = 0;
		try
		{
			// Each event at its offset: the engine renders up to it, then takes it (a Hit starts at its next block)
			for(const auto& event : _midiIn)
			{
				const auto at = std::clamp<size_t>(event.offset, done, _size);
				render(_outputs, done, at - done);
				done = at;
				apply(event);
			}
			render(_outputs, done, _size - done);
		}
		catch(const std::exception& _error)
		{
			std::fprintf(stderr, "[MD Drums] %s\n", _error.what());
			m_valid = false;
			for(size_t channel = 0; channel < OutputCount; ++channel)
				if(_outputs[channel])
					std::fill_n(_outputs[channel] + done, _size - done, 0.0f);
		}
		reportMachines(_midiOut);
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		render(_outputs, 0, _samples);
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
	{
		apply(_ev);
		return true;
	}

	void Device::render(const synthLib::TAudioOutputs& _outputs, const size_t _offset, const size_t _count)
	{
		// Main straight from the engine, each Track's own output into m_tracks a chunk at a time, then each Out the sum
		// of the Tracks on it
		for(size_t done = 0; done < _count;)
		{
			const auto count = std::min(_count - done, Chunk);
			std::array<float*, Engine::OutputCount> outputs{};
			for(size_t channel = 0; channel < 2; ++channel)
				outputs[channel] = _outputs[channel] ? _outputs[channel] + _offset + done : nullptr;
			for(size_t track = 0; track < Engine::TrackCount; ++track)
				outputs[2 + track] = m_tracks[track].data();
			m_engine->render(outputs.data(), count);

			for(size_t out = 0; out < OutCount; ++out)
				if(auto* samples = _outputs[2 + out])
					std::fill_n(samples + _offset + done, count, 0.0f);
			for(size_t track = 0; track < Engine::TrackCount; ++track)
			{
				const auto out = m_mixer.outs[track];
				auto* samples = out && out <= OutCount ? _outputs[1 + out] : nullptr;
				if(!samples)
					continue;
				samples += _offset + done;
				const auto& own = m_tracks[track];
				for(size_t i = 0; i < count; ++i)
					samples[i] += own[i];
			}

			if(m_telemetry)
			{
				for(int channel = 0; channel < Engine::OutputCount; ++channel)
				{
					const auto* samples = outputs[static_cast<size_t>(channel)];
					if(!samples)
						continue;
					float peak = 0.0f;
					for(size_t i = 0; i < count; ++i)
						peak = std::max(peak, std::abs(samples[i]));
					if(peak > 0.0f)
						m_telemetry->raisePeak(channel, peak);
				}
				capture(count);
			}
			m_position += static_cast<int64_t>(count);
			done += count;
		}
	}

	void Device::capture(const size_t _count)
	{
		for(int track = 0; track < Engine::TrackCount; ++track)
		{
			auto& c = m_captures[static_cast<size_t>(track)];
			if(!c.active)
				continue;
			const auto& own = m_tracks[static_cast<size_t>(track)];
			// The capture starts at the sample the Hit sounds, which may be in this chunk
			const auto first = std::clamp<int64_t>(c.start - m_position, 0, static_cast<int64_t>(_count));
			Telemetry::Column column{};
			for(auto i = static_cast<size_t>(first); i < _count; ++i)
			{
				if(!c.column.add(own[i], column))
					continue;
				c.active = m_telemetry->addColumn(track, column[0], column[1]);
				if(!c.active)
					break;
			}
		}
	}

	void Device::apply(const synthLib::SMidiEvent& _event)
	{
		if(!_event.sysex.empty())
		{
			if(_event.source != synthLib::MidiEventSource::Host)
				applySysex(_event);
			return;
		}
		const auto status = _event.a & 0xf0;
		if(status == synthLib::M_NOTEON && _event.c > 0)
		{
			const auto track = static_cast<int>(_event.b) - FirstNote;
			const auto param = static_cast<int>(_event.b) - FirstLockNote;
			if(param >= 0 && param < Engine::ParamCount)
			{
				// Channels 2-16 name their Track. Channel 1 names the last channel-1 Hit just before at the same sample:
				// Live gives a Max for Live device's notes channel 1, and a printed Track 1 plays there among the other
				// Tracks' channels (ADR 0003, amended); without one, Track 1
				const auto channel = _event.a & 0x0f;
				const auto lockTrack = channel == 0 && m_lastHitPosition == m_position ? m_lastHitTrack : channel;
				m_engine->lock(lockTrack, param, messages::lockValue(_event.c));
				return;
			}
			if(track < 0 || track >= Engine::TrackCount)
			{
				if(m_telemetry)
					m_telemetry->unplayed(_event.b);
				return;
			}
			if((_event.a & 0x0f) == 0)
			{
				m_lastHitTrack = track;
				m_lastHitPosition = m_position;
			}
			const auto struck = m_engine->trigger(track, _event.c);
			if(!m_telemetry)
				return;
			// Each struck Track counts a Hit and starts a capture where the Hit sounds
			for(int t = 0; t < Engine::TrackCount; ++t)
			{
				if(!(struck & (1u << t)))
					continue;
				m_telemetry->hit(t);
				m_telemetry->beginCapture(t, _event.c);
				m_captures[static_cast<size_t>(t)] = {true, m_position + Engine::CaptureDelay, {}};
			}
			return;
		}
		if(status == synthLib::M_CONTROLCHANGE && _event.source != synthLib::MidiEventSource::Host)
			applyControlChange(_event);
	}

	void Device::applyControlChange(const synthLib::SMidiEvent& _event)
	{
		const auto change = md::automation::decodeParameterChange(g_model, {_event.a, _event.b, _event.c}, 0);
		if(!change)
			return;
		const auto track = change->track;
		if(change->page <= machinedrum::Routing)
		{
			const auto parameter = static_cast<uint8_t>(change->page * 8 + change->index);
			m_kit.parameters[track][parameter] = change->value;
			m_engine->setParam(track, parameter, change->value);
		}
		else if(change->page == machinedrum::Level)
		{
			m_kit.levels[track] = change->value;
			m_engine->setLevel(track, change->value);
		}
		else if(change->page == machinedrum::Mute)
		{
			const auto bit = static_cast<uint16_t>(1u << track);
			m_mixer.mute = static_cast<uint16_t>(change->value ? m_mixer.mute | bit : m_mixer.mute & ~bit);
			applyMixer();
		}
	}

	void Device::applySysex(const synthLib::SMidiEvent& _event)
	{
		const auto& s = _event.sysex;
		if(const auto own = messages::parse(s))
		{
			const auto bit = static_cast<uint16_t>(1u << own->track);
			switch(own->command)
			{
			case messages::Command::Solo:
				m_mixer.solo = static_cast<uint16_t>(own->on ? m_mixer.solo | bit : m_mixer.solo & ~bit);
				break;
			case messages::Command::Out:
				m_mixer.outs[own->track] = own->out;
				break;
			case messages::Command::Tempo:
				m_engine->setTempo(own->bpm);
				return;
			case messages::Command::Mixer:
				m_mixer = own->mixer;
				break;
			case messages::Command::KitName:
				m_kit.name = own->name;
				return;
			}
			applyMixer();
			return;
		}

		// The Machinedrum's messages: one change of the Kit, as the OS reads it (mdkitchange.h), or a whole Kit, the
		// one this Device plays whatever its slot (which allocates: a dump is no realtime message)
		if(const auto change = sysex::decodeKitChange(s))
		{
			applyChange(*change);
			return;
		}
		if(s.size() > 6 && sysex::codec::hasHeader(g_model, s, g_kitDump))
		{
			if(auto kit = sysex::parseMdKit(s))
			{
				m_kit = *kit;
				applyKit();
			}
		}
	}

	void Device::applyChange(const sysex::KitChange& _change)
	{
		using Kind = sysex::KitChange::Kind;
		const auto track = _change.track;
		if(_change.kind == Kind::Machine)
		{
			// A machine the OS has, any other being no machine to the engine; the voice switches at its next Hit
			const auto& machines = m_engine->machines();
			const auto info = std::find_if(machines.begin(), machines.end(),
				[&](const md::engine::MachineInfo& _m) { return _m.id == _change.value; });
			if(info == machines.end() || !sysex::applyKitChange(_change, m_kit, &info->defaults))
				return;
			m_engine->setMachine(track, _change.value);
			for(uint8_t parameter = 0; parameter < Engine::ParamCount; ++parameter)
				m_engine->setParam(track, parameter, m_kit.parameters[track][parameter]);
			m_machinesToReport = static_cast<uint16_t>(m_machinesToReport | 1u << track);
			return;
		}
		if(!sysex::applyKitChange(_change, m_kit))
			return;
		switch(_change.kind)
		{
		case Kind::Master:
		{
			constexpr auto parameters = sysex::MasterEffectParameters;
			const auto index = sysex::mdKitMasterIndex(static_cast<uint8_t>(_change.index / parameters),
				static_cast<uint8_t>(_change.index % parameters));
			m_engine->setMaster(index, m_kit.masterEffects[index]);
			return;
		}
		case Kind::Lfo:
			// The LFO's five settings, or LFOS, LFOD and LFOM, the Track's last three parameters
			if(_change.index < md::LfoSettings::FieldMaximum.size())
				m_engine->setLfo(track, m_kit.lfo(track));
			else
				m_engine->setParam(track, Engine::ParamCount - sysex::KitChange::LfoFields + _change.index,
					_change.value);
			return;
		case Kind::Link:
			// MdKit::Off is past the Tracks, which switches the engine's Link or Choke off
			m_engine->setLink(track, m_kit.links[track]);
			return;
		case Kind::Choke:
			m_engine->setChoke(track, m_kit.chokes[track]);
			return;
		case Kind::Machine:
			return;
		}
	}

	void Device::reportMachines(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		// The 24 parameters as CCs, the synthesis, effects and routing pages: the Controller takes the changed ones
		for(uint8_t track = 0; track < Engine::TrackCount && m_machinesToReport; ++track)
		{
			if(!(m_machinesToReport & (1u << track)))
				continue;
			for(uint8_t parameter = 0; parameter < Engine::ParamCount; ++parameter)
			{
				const auto cc = md::automation::encodeParameterChange(g_model, {static_cast<uint8_t>(parameter / 8),
					track, static_cast<uint8_t>(parameter % 8), m_kit.parameters[track][parameter]}, 0);
				if(cc)
					_midiOut.emplace_back(synthLib::MidiEventSource::Device, (*cc)[0], (*cc)[1], (*cc)[2]);
			}
		}
		m_machinesToReport = 0;
	}

	void Device::applyKit()
	{
		mdDrums::applyKit(*m_engine, m_kit);
	}

	void Device::applyMixer()
	{
		// A Track is silent when muted, or when another one has its Solo on and it has not
		for(uint8_t track = 0; track < Engine::TrackCount; ++track)
		{
			const auto bit = static_cast<uint16_t>(1u << track);
			const bool muted = (m_mixer.mute & bit) || (m_mixer.solo && !(m_mixer.solo & bit));
			m_engine->setMute(track, muted);
		}
		m_engine->setSeparateOutputs(m_mixer.offMain());
	}
}
