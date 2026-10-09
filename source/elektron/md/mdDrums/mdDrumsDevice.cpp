#include "mdDrumsDevice.h"

#include "mdProtocol/mdautomation.h"
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

		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_masterEffect = 0x5d;	// to $60: echo, reverb, EQ, dynamix
		constexpr uint8_t g_lfoChange = 0x62;
		constexpr uint8_t g_linkChange = 0x65;
		constexpr uint8_t g_chokeChange = 0x66;
		constexpr uint8_t g_kitDump = 0x52;

		// The Kit dump keeps the master effects in this order: reverb, echo, EQ, dynamix
		constexpr uint8_t g_masterBlock[sysex::MasterEffectCount] = {1, 0, 2, 3};

		constexpr uint8_t g_trackOff = 0x7f;	// a Link or Choke off on the wire

		bool isMachinedrumSysex(const synthLib::SMidiEvent& _event, const uint8_t _command, const size_t _size)
		{
			const auto& s = _event.sysex;
			return s.size() == _size && s[0] == 0xf0 && s[1] == 0x00 && s[2] == 0x20 && s[3] == 0x3c && s[4] == 0x02
				&& s[5] == 0x00 && s[6] == _command && s.back() == 0xf7;
		}
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
		m_outCount = 0;
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
		m_outCount = 0;
		m_machinesToReport = 0;
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
		reportMachines();
		readMidiOut(_midiOut);
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

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		for(size_t i = 0; i < m_outCount; ++i)
			_midiOut.push_back(m_out[i]);
		m_outCount = 0;
	}

	void Device::render(const synthLib::TAudioOutputs& _outputs, const size_t _offset, const size_t _count)
	{
		if(!_count)
			return;
		std::array<float*, OutputCount> outputs{};
		for(size_t channel = 0; channel < OutputCount; ++channel)
			outputs[channel] = _outputs[channel] ? _outputs[channel] + _offset : nullptr;
		m_engine->render(outputs.data(), _count);
		if(m_telemetry)
		{
			for(int channel = 0; channel < OutputCount; ++channel)
			{
				const auto* samples = outputs[static_cast<size_t>(channel)];
				if(!samples)
					continue;
				float peak = 0.0f;
				for(size_t i = 0; i < _count; ++i)
					peak = std::max(peak, std::abs(samples[i]));
				if(peak > 0.0f)
					m_telemetry->raisePeak(channel, peak);
			}
			capture(outputs, _count);
		}
		m_position += static_cast<int64_t>(_count);
	}

	void Device::capture(const std::array<float*, Engine::OutputCount>& _outputs, const size_t _count)
	{
		const auto toColumn = [](const float _v)
		{
			return static_cast<int16_t>(std::clamp(std::lround(_v * 32767.0f), -32767L, 32767L));
		};
		for(int track = 0; track < Engine::TrackCount; ++track)
		{
			auto& c = m_captures[static_cast<size_t>(track)];
			const auto* samples = _outputs[2 + static_cast<size_t>(track)];
			if(!c.active || !samples)
				continue;
			for(size_t i = 0; i < _count; ++i)
			{
				if(m_position + static_cast<int64_t>(i) < c.start)
					continue;
				const auto value = samples[i];
				c.low = c.filled ? std::min(c.low, value) : value;
				c.high = c.filled ? std::max(c.high, value) : value;
				if(++c.filled < Telemetry::ColumnSamples)
					continue;
				c.filled = 0;
				c.active = m_telemetry->addColumn(track, toColumn(c.low), toColumn(c.high));
				if(!c.active)
					break;
			}
		}
	}

	void Device::queueOut(const synthLib::SMidiEvent& _event)
	{
		if(m_outCount < m_out.size())
			m_out[m_outCount++] = _event;
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
			if(track < 0 || track >= Engine::TrackCount)
				return;
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
				m_captures[static_cast<size_t>(t)] = {true, m_position + Engine::SampleAccurateDelay, 0, 0.0f, 0.0f};
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
		if(const auto own = messages::parse(sysex::MessageView(s)))
		{
			const auto bit = static_cast<uint16_t>(1u << own->track);
			switch(own->command)
			{
			case messages::Command::Solo:
				m_mixer.solo = static_cast<uint16_t>(own->on ? m_mixer.solo | bit : m_mixer.solo & ~bit);
				break;
			case messages::Command::Out:
				m_mixer.out = static_cast<uint16_t>(own->on ? m_mixer.out | bit : m_mixer.out & ~bit);
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

		if(isMachinedrumSysex(_event, g_assignMachine, 11))
		{
			// The UW flag adds 128 to the id
			if(s[7] < Engine::TrackCount)
				applyMachine(s[7], static_cast<uint8_t>(s[8] | (s[9] ? 0x80 : 0)));
			return;
		}
		for(uint8_t effect = 0; effect < sysex::MasterEffectCount; ++effect)
		{
			if(!isMachinedrumSysex(_event, static_cast<uint8_t>(g_masterEffect + effect), 10))
				continue;
			if(s[7] < sysex::MasterEffectParameters)
			{
				const auto index = g_masterBlock[effect] * sysex::MasterEffectParameters + s[7];
				m_kit.masterEffects[index] = s[8];
				m_engine->setMaster(index, s[8]);
			}
			return;
		}
		if(isMachinedrumSysex(_event, g_lfoChange, 10))
		{
			const auto track = static_cast<uint8_t>(s[7] >> 3), field = static_cast<uint8_t>(s[7] & 7);
			constexpr uint8_t maximum[] = {15, 23, md::LfoSettings::ShapeCount - 1, md::LfoSettings::ShapeCount - 1,
				md::LfoSettings::UpdateCount - 1};
			if(track >= Engine::TrackCount || field >= std::size(maximum))
				return;
			m_kit.lfos[track][field] = std::min(s[8], maximum[field]);
			m_engine->setLfo(track, m_kit.lfo(track));
			return;
		}
		if(isMachinedrumSysex(_event, g_linkChange, 10) || isMachinedrumSysex(_event, g_chokeChange, 10))
		{
			const auto track = s[7];
			if(track >= Engine::TrackCount || (s[8] >= Engine::TrackCount && s[8] != g_trackOff))
				return;
			const auto target = s[8] == g_trackOff ? sysex::MdKit::Off : s[8];
			const auto toEngine = target == sysex::MdKit::Off ? Engine::NoTrack : target;
			if(s[6] == g_linkChange)
			{
				m_kit.links[track] = target;
				m_engine->setLink(track, toEngine);
			}
			else
			{
				m_kit.chokes[track] = target;
				m_engine->setChoke(track, toEngine);
			}
			return;
		}
		if(s.size() == sysex::MdKitDumpSize && s[6] == g_kitDump)
		{
			// A whole Kit, the one this Device plays whatever its slot (allocates: a dump is no realtime message)
			if(auto kit = sysex::parseMdKit(sysex::MessageView(s)))
			{
				m_kit = *kit;
				applyKit();
			}
		}
	}

	void Device::applyMachine(const uint8_t _track, const uint8_t _machine)
	{
		const auto& machines = m_engine->machines();
		const auto info = std::find_if(machines.begin(), machines.end(),
			[&](const md::engine::MachineInfo& _m) { return _m.id == _machine; });
		if(info == machines.end())
			return;
		// As the OS: the Kit takes the machine and its SYN1-8 defaults, the voice switches at its next Hit
		m_kit.machines[_track] = _machine;
		m_engine->setMachine(_track, _machine);
		for(uint8_t parameter = 0; parameter < 8; ++parameter)
		{
			m_kit.parameters[_track][parameter] = info->defaults[parameter];
			m_engine->setParam(_track, parameter, info->defaults[parameter]);
		}
		m_machinesToReport = static_cast<uint16_t>(m_machinesToReport | 1u << _track);
	}

	void Device::reportMachines()
	{
		for(uint8_t track = 0; track < Engine::TrackCount && m_machinesToReport; ++track)
		{
			if(!(m_machinesToReport & (1u << track)))
				continue;
			for(uint8_t parameter = 0; parameter < 8; ++parameter)
			{
				const auto cc = md::automation::encodeParameterChange(g_model,
					{machinedrum::Synthesis, track, parameter, m_kit.parameters[track][parameter]}, 0);
				if(cc)
					queueOut(synthLib::SMidiEvent(synthLib::MidiEventSource::Device, (*cc)[0], (*cc)[1], (*cc)[2]));
			}
		}
		m_machinesToReport = 0;
	}

	void Device::applyKit()
	{
		for(uint8_t track = 0; track < Engine::TrackCount; ++track)
		{
			m_engine->setMachine(track, m_kit.machine(track));
			for(uint8_t parameter = 0; parameter < sysex::MdKit::ParameterCount; ++parameter)
				m_engine->setParam(track, parameter, m_kit.parameters[track][parameter]);
			m_engine->setLevel(track, m_kit.levels[track]);
			m_engine->loadLfo(track, m_kit.lfos[track].data());
			m_engine->setLink(track, m_kit.links[track] == sysex::MdKit::Off ? Engine::NoTrack : m_kit.links[track]);
			m_engine->setChoke(track, m_kit.chokes[track] == sysex::MdKit::Off ? Engine::NoTrack : m_kit.chokes[track]);
		}
		// The dump keeps the master effects in the engine's order
		for(int index = 0; index < Engine::MasterCount; ++index)
			m_engine->setMaster(index, m_kit.masterEffects[static_cast<size_t>(index)]);
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
		m_engine->setSeparateOutputs(m_mixer.out);
	}
}
