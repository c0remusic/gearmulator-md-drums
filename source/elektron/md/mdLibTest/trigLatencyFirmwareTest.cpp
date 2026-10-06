// Note-to-sound latency of the Machinedrum: a host note reaches the firmware's MIDI input, the firmware
// triggers a track, and this measures the samples from the note's position in the host block to the first
// audible sample at the plug-in's output, at each plug-in latency setting. It also reports which notes
// sound, what the firmware sends back, and whether the velocity changes the level.

#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	constexpr uint32_t g_block = 256;
	constexpr float g_rate = 44100.0f;

	class Bench
	{
	public:
		Bench(const char* _path, const uint32_t _latencyBlocks)
		{
			synthLib::DeviceCreateParams params;
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
	Hit play(Bench& _bench, const float _threshold, const uint8_t _status, const uint8_t _note,
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

	float boot(Bench& _bench)
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
	float bootUntilNotesPlay(Bench& _bench)
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

	struct Stats
	{
		uint64_t minimum = ~0ull, maximum = 0;
		double sum = 0;
		uint32_t count = 0;
		void add(const uint64_t _value)
		{
			minimum = std::min(minimum, _value);
			maximum = std::max(maximum, _value);
			sum += static_cast<double>(_value);
			++count;
		}
	};

	void run(const char* _path)
	{
		// Which notes on the base channel (channel 1) trigger a track, and what the firmware sends back.
		{
			Bench bench(_path, 0);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			std::printf("idle floor %g, threshold %g\n", floor, threshold);
			for(uint8_t note = 34; note < 64; ++note)
			{
				const auto hit = play(bench, threshold, 0x90, note, 100, 0);
				std::printf("note %3u: %s", note, hit.sounded ? "sound" : "silent");
				if(hit.sounded)
					std::printf(" peak %.4f latency %llu", hit.peak, static_cast<unsigned long long>(hit.latency));
				for(const auto& event : bench.midiSeen())
					if(event.sysex.empty())
						std::printf(" out[%02x %u %u]", event.a, event.b, event.c);
				bench.midiSeen().clear();
				std::printf("\n");
			}
		}

		// Latency and jitter at each latency setting, notes at varied positions in the block.
		for(const uint32_t blocks : {0u, 1u, 2u, 4u})
		{
			Bench bench(_path, blocks);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			Stats stats;
			uint32_t silent = 0;
			uint32_t random = 0x16305eedu;
			for(uint32_t trial = 0; trial < 48; ++trial)
			{
				random ^= random << 13; random ^= random >> 17; random ^= random << 5;
				const auto offset = random % g_block;
				const auto hit = play(bench, threshold, 0x90, 36, 100, offset);
				if(hit.sounded)
					stats.add(hit.latency);
				else
					++silent;			}
			const auto reported = bench.reportedLatency();
			std::printf("latencyBlocks %u (reported %u): %u hits, %u silent, latency min %llu max %llu mean %.1f "
				"samples, beyond reported min %lld (%.2f ms), jitter %llu (%.2f ms)\n",
				blocks, reported, stats.count, silent,
				static_cast<unsigned long long>(stats.minimum), static_cast<unsigned long long>(stats.maximum),
				stats.count ? stats.sum / stats.count : 0.0,
				static_cast<long long>(stats.minimum) - reported,
				(static_cast<double>(stats.minimum) - reported) * 1000.0 / g_rate,
				static_cast<unsigned long long>(stats.maximum - stats.minimum),
				static_cast<double>(stats.maximum - stats.minimum) * 1000.0 / g_rate);
		}

		// Velocity.
		{
			Bench bench(_path, 0);
			const auto floor = bootUntilNotesPlay(bench);
			const auto threshold = std::max(1e-4f, floor * 8.0f);
			for(const uint8_t velocity : {127, 100, 64, 32, 8})
			{
				const auto hit = play(bench, threshold, 0x90, 36, velocity, 0);
				std::printf("velocity %3u: peak %.4f\n", velocity, hit.peak);
			}
		}
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "mdTrigLatencyFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n";
		return 77;
	}
	try
	{
		run(path);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdTrigLatencyFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
