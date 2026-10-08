// The Machinedrum firmware as a plug-in sees it, for measurements: a Device behind synthLib::Plugin, rendered
// offline block by block, with helpers to boot it until notes play and to record one hit.
#pragma once

#include "hitParameters.h"

#include "mdProtocol/mdautomation.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "mdProtocol/mdsysexautomation.h"
#include "mdProtocol/mdtypes.h"

#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mdFirmwareBench
{
	inline void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	constexpr uint32_t g_block = 256;
	constexpr float g_rate = 44100.0f;

	class Bench
	{
	public:
		Bench(const char* _path, const uint32_t _latencyBlocks, const std::string& _homePath = {})
		{
			synthLib::DeviceCreateParams params;
			params.homePath = _homePath;
			require(baseLib::filesystem::readFile(params.romData, _path), "cannot read firmware");
			require(md::RomLoader::isRomForModel(params.romData, md::MachineModel::Machinedrum),
				"not the pinned MD firmware");
			params.romName = _path;
			params.customData = md::deviceCustomData(md::MachineModel::Machinedrum);
			m_device = std::make_unique<md::Device>(params);
			require(m_device->isValid(), "invalid device");
			m_plugin = std::make_unique<synthLib::Plugin>(m_device.get(), [](synthLib::Device*) {});
			m_plugin->reserveMidiEventCapacity();
			m_plugin->setHostSamplerate(g_rate, g_rate);
			m_plugin->setBlockSize(g_block);
			m_plugin->setLatencyBlocks(_latencyBlocks);
			// Offline: the render thread is waited for, so a block it has not delivered yet never plays as
			// silence. This measures the machine's own delay, not whether a CPU keeps up.
			m_plugin->setHostRealtime(false);

			for(size_t channel = 0; channel < m_outputs.size(); ++channel)
				m_outs[channel] = m_outputs[channel].data();
			m_ins = {m_silence[0].data(), m_silence[1].data(), nullptr, nullptr};
		}

		uint32_t reportedLatency() const { return m_plugin->getLatencyMidiToOutput(); }
		md::Hardware& hardware() { return m_device->getHardware(); }

		// One host block; returns the block's peak over all outputs and, if any sample reaches _threshold,
		// the index of the first one.
		float process(const float _threshold, std::optional<uint32_t>& _first)
		{
			m_plugin->process(m_ins, m_outs, g_block, 120.0f, 0.0f, false);
			m_plugin->getMidiOut(m_midiOut);
			m_midiSeen.insert(m_midiSeen.end(), m_midiOut.begin(), m_midiOut.end());
			m_midiOut.clear();
			float peak = 0;
			_first.reset();
			for(uint32_t sample = 0; sample < g_block; ++sample)
				for(const auto& channel : m_outputs)
				{
					const auto value = std::abs(channel[sample]);
					require(std::isfinite(value), "non-finite sample");
					peak = std::max(peak, value);
					if(!_first && value >= _threshold)
						_first = sample;
				}
			m_frame += g_block;
			return peak;
		}

		float process()
		{
			std::optional<uint32_t> first;
			return process(2.0f, first);
		}

		void note(const uint8_t _status, const uint8_t _note, const uint8_t _velocity, const uint32_t _offset)
		{
			m_plugin->addMidiEvent({synthLib::MidiEventSource::Host, _status, _note, _velocity, _offset});
		}

		void sysex(const std::vector<uint8_t>& _message)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_message.begin(), _message.end());
			m_plugin->addMidiEvent(event);
		}

		const std::array<std::array<float, g_block>, 6>& outputs() const { return m_outputs; }
		uint64_t frame() const { return m_frame; }
		std::vector<synthLib::SMidiEvent>& midiSeen() { return m_midiSeen; }

	private:
		std::unique_ptr<md::Device> m_device;
		std::unique_ptr<synthLib::Plugin> m_plugin;
		std::array<std::array<float, g_block>, 6> m_outputs{};
		std::array<std::array<float, g_block>, 2> m_silence{};
		synthLib::TAudioOutputs m_outs{};
		synthLib::TAudioInputs m_ins{};
		std::vector<synthLib::SMidiEvent> m_midiOut, m_midiSeen;
		uint64_t m_frame = 0;
	};

	struct Hit
	{
		bool sounded = false;
		uint64_t latency = 0;	// samples from the note to the first audible sample
		float peak = 0;
	};

	// Waits for silence, plays the note at _offset in the next block and follows it for up to a second.
	inline Hit play(Bench& _bench, const float _threshold, const uint8_t _status, const uint8_t _note,
		const uint8_t _velocity, const uint32_t _offset)
	{
		uint32_t quiet = 0;
		for(uint32_t block = 0; quiet < 16 && block < 2000; ++block)
			quiet = _bench.process() < _threshold ? quiet + 1 : 0;
		require(quiet >= 16, "the machine never fell silent");

		const auto at = _bench.frame() + _offset;
		_bench.note(_status, _note, _velocity, _offset);
		Hit hit;
		for(uint32_t block = 0; block < 172; ++block)
		{
			std::optional<uint32_t> first;
			const auto start = _bench.frame();
			const auto peak = _bench.process(_threshold, first);
			if(first && !hit.sounded)
			{
				hit.sounded = true;
				hit.latency = start + *first - at;
			}
			hit.peak = std::max(hit.peak, peak);
		}
		_bench.note(static_cast<uint8_t>(0x80 | (_status & 0x0f)), _note, 0, 0);
		return hit;
	}

	inline float boot(Bench& _bench)
	{
		for(uint32_t block = 0; !_bench.hardware().isFirmwareMidiReady(); ++block)
		{
			require(block < 172 * 60, "firmware not ready for MIDI within 60 s of audio");
			_bench.process();
		}
		std::printf("MIDI ready %.2f s of audio after boot\n", _bench.frame() / g_rate);
		// Settle, then take the idle floor.
		float floor = 0;
		for(uint32_t block = 0; block < 172 * 2; ++block)
		{
			const auto peak = _bench.process();
			if(block >= 172)
				floor = std::max(floor, peak);
		}
		_bench.midiSeen().clear();
		return floor;
	}

	// The firmware reports MIDI ready seconds before a note plays: probe until one does.
	inline float bootUntilNotesPlay(Bench& _bench)
	{
		const auto floor = boot(_bench);
		const auto threshold = std::max(1e-4f, floor * 8.0f);
		for(uint32_t probe = 0; ; ++probe)
		{
			require(probe < 60, "no note played within a minute of MIDI ready");
			if(play(_bench, threshold, 0x90, 36, 100, 0).sounded)
			{
				std::printf("first note played %.2f s of audio after boot\n", _bench.frame() / g_rate);
				return floor;
			}
		}
	}

	// The note that triggers each track on the base channel (the firmware's default map, white keys C2-D4).
	inline constexpr std::array<uint8_t, 16> g_trackNotes{36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62};

	// One hit of _machine on _track (0-15), routed to output A (no master effects), with hitParameters(_machine) and
	// velocity 100, on a booted machine: a second of every output channel from the note on. A voice keeps its noise
	// generator's state from one hit to the next, so a hit compared with another engine takes a track not played yet.
	inline std::vector<std::vector<float>> recordHit(Bench& _bench, const float _threshold, const uint16_t _machine,
		const uint8_t _track)
	{
		using namespace md::automation;
		const auto routing = sysex::trackRouting(_track, sysex::TrackOutput::A);
		const auto machine = sysex::assignMachine(md::MachineModel::Machinedrum, _track, _machine);
		require(routing && machine, "no routing or machine message");
		_bench.sysex(std::vector<uint8_t>(routing->begin(), routing->end()));
		_bench.sysex(std::vector<uint8_t>(machine->begin(), machine->end()));
		for(uint32_t block = 0; block < 86; ++block)
			_bench.process();
		const auto parameters = hitParameters(_machine);
		for(uint8_t parameter = 0; parameter < parameters.size(); ++parameter)
		{
			const auto cc = encodeParameterChange(md::MachineModel::Machinedrum,
				{static_cast<uint8_t>(parameter / 8), _track, static_cast<uint8_t>(parameter % 8), parameters[parameter]}, 0);
			require(cc.has_value(), "no CC for a track parameter");
			_bench.note((*cc)[0], (*cc)[1], (*cc)[2], 0);
		}
		const auto level = encodeParameterChange(md::MachineModel::Machinedrum,
			{md::automation::machinedrum::Level, _track, 0, g_hitLevel}, 0);
		require(level.has_value(), "no CC for the track level");
		_bench.note((*level)[0], (*level)[1], (*level)[2], 0);
		// Let the smoothing settle, then wait for silence.
		uint32_t quiet = 0;
		for(uint32_t block = 0; block < 172 || quiet < 16; ++block)
		{
			require(block < 172 * 10, "the machine never fell silent");
			quiet = _bench.process() < _threshold ? quiet + 1 : 0;
		}
		_bench.note(0x90, g_trackNotes[_track], 100, 0);
		std::vector<std::vector<float>> channels(6);
		for(uint32_t block = 0; block < 172; ++block)
		{
			_bench.process();
			for(size_t channel = 0; channel < 6; ++channel)
				channels[channel].insert(channels[channel].end(), _bench.outputs()[channel].begin(),
					_bench.outputs()[channel].end());
		}
		return channels;
	}

	// Takes _track's hit out of the outputs once recorded (LEV 0): a hit still sounding, as a looped ROM sample does
	// while it holds, would otherwise play into the next one and keep the machine from falling silent.
	inline void silenceHit(Bench& _bench, const uint8_t _track)
	{
		const auto level = md::automation::encodeParameterChange(md::MachineModel::Machinedrum,
			{md::automation::machinedrum::Level, _track, 0, 0}, 0);
		require(level.has_value(), "no CC for the track level");
		_bench.note((*level)[0], (*level)[1], (*level)[2], 0);
	}
}
