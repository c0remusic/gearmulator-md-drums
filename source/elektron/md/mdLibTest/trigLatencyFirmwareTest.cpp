// Note-to-sound latency of the Machinedrum: a host note reaches the firmware's MIDI input, the firmware
// triggers a track, and this measures the samples from the note's position in the host block to the first
// audible sample at the plug-in's output, at each plug-in latency setting. It also reports which notes
// sound, what the firmware sends back, and whether the velocity changes the level.

#include "mdLib/mdautomation.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdsysexautomation.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <chrono>
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

	// The 128x64 LCD as text, two pixel rows per line.
	void printLcd(md::Hardware& _hardware)
	{
		md::FrontPanel panel;
		if(!_hardware.tryGetFrontPanelSnapshot(panel))
			return;
		const auto pixel = [&](const uint32_t _x, const uint32_t _y)
		{
			return (panel.getLcdVram(_x / 64, _y / 8, _x % 64) >> (_y % 8)) & 1;
		};
		for(uint32_t y = 0; y < 64; y += 2)
		{
			std::string line("      |");
			for(uint32_t x = 0; x < 128; ++x)
			{
				const auto top = pixel(x, y), bottom = pixel(x, y + 1);
				line += top && bottom ? '8' : top ? '\'' : bottom ? '.' : ' ';
			}
			std::printf("%s|\n", line.c_str());
		}
	}

	// From boot: when the firmware takes MIDI, when the factory flash is ready to cache, and when a note
	// first sounds, probing with one every 32 blocks (186 ms) if _probe.
	void startupTimeline(Bench& _bench, const bool _probe)
	{
		const auto seconds = [&] { return _bench.frame() / g_rate; };
		double midiReady = -1, cacheReady = -1, firstNote = -1;
		const auto wallStart = std::chrono::steady_clock::now();
		for(uint32_t block = 0; block < 172 * 60; ++block)
		{
			auto& hardware = _bench.hardware();
			if(midiReady < 0 && hardware.isFirmwareMidiReady())
				midiReady = seconds();
			if(cacheReady < 0 && hardware.isFactoryFlashCacheReady())
				cacheReady = seconds();
			if(block % 172 == 0)
				std::printf("    %.1f s: flash dirty %d, MIDI bytes taken %llu, queued %zu, LCD %016llx\n",
					seconds(), hardware.flashDirty(), static_cast<unsigned long long>(hardware.midiRxConsumedCount()),
					hardware.queuedMidiRxBytes(), static_cast<unsigned long long>(hardware.lcdPagesDigest(0, 7)));
			if(_probe && block % 344 == 0 && firstNote < 0 && std::getenv("MD_TRIG_LCD"))
				printLcd(hardware);
			if(_probe && midiReady >= 0 && firstNote < 0 && block % 32 == 0)
				_bench.note(0x90, 36, 100, 0);
			std::optional<uint32_t> first;
			_bench.process(1e-4f, first);
			if(first && firstNote < 0)
				firstNote = seconds();
			if(_probe && block % 32 == 16)
				_bench.note(0x80, 36, 0, 0);
			if((!_probe || firstNote >= 0) && cacheReady >= 0 && midiReady >= 0)
				break;
		}
		std::printf("  MIDI ready %.2f s, factory flash cacheable %.2f s, first note %.2f s (-1: never within 60 s)\n",
			midiReady, cacheReady, firstNote);
		const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
		std::printf("  %.2f s of audio in %.2f s of wall clock (%.2fx real time)\n", seconds(), wall, seconds() / wall);
	}

	void startup(const char* _path, const std::string& _homePath)
	{
		std::printf("fresh flash, no notes:\n");
		{
			Bench bench(_path, 0);
			startupTimeline(bench, false);
			const auto cache = bench.hardware().copyFactoryFlashCache();
			require(!cache.empty(), "no factory flash cache to write");
			const auto nvram = _homePath + "/nvram/";
			baseLib::filesystem::createDirectory(nvram);
			require(baseLib::filesystem::writeFile(nvram + "md-uw-1.63-factory-v2.cache", cache),
				"cannot write the factory flash cache");
		}
		std::printf("fresh flash, a note every 186 ms:\n");
		{
			Bench bench(_path, 0);
			startupTimeline(bench, true);
		}
		std::printf("factory flash from the cache, a note every 186 ms:\n");
		{
			Bench bench(_path, 0, _homePath);
			startupTimeline(bench, true);
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

	// The 24 track parameters a comparison hit plays with: SYN1-8, then AMD AMF EQF EQG FLTF FLTW FLTQ SRR, then
	// DIST VOL PAN DEL REV LFOS LFOD LFOM. The other engine sets the same values.
	constexpr std::array<uint8_t, 24> g_hitParameters{64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 64, 64, 0, 127, 0, 0,
		0, 100, 64, 0, 0, 0, 0, 0};

	// One hit of _machine on track 1, routed to output A (no master effects), velocity 100: writes a second of
	// every output channel from the note on, as raw 32-bit floats, channel after channel, to _file.
	void hit(const char* _path, const uint16_t _machine, const std::string& _file)
	{
		Bench bench(_path, 0);
		const auto floor = bootUntilNotesPlay(bench);
		const auto threshold = std::max(1e-4f, floor * 8.0f);
		using namespace md::automation;
		const auto routing = sysex::trackRouting(0, sysex::TrackOutput::A);
		const auto machine = sysex::assignMachine(md::MachineModel::Machinedrum, 0, _machine);
		require(routing && machine, "no routing or machine message");
		bench.sysex(std::vector<uint8_t>(routing->begin(), routing->end()));
		bench.sysex(std::vector<uint8_t>(machine->begin(), machine->end()));
		for(uint32_t block = 0; block < 86; ++block)
			bench.process();
		for(uint8_t parameter = 0; parameter < g_hitParameters.size(); ++parameter)
		{
			const auto cc = encodeParameterChange(md::MachineModel::Machinedrum,
				{static_cast<uint8_t>(parameter / 8), 0, static_cast<uint8_t>(parameter % 8), g_hitParameters[parameter]}, 0);
			require(cc.has_value(), "no CC for a track parameter");
			bench.note((*cc)[0], (*cc)[1], (*cc)[2], 0);
		}
		// Let the smoothing settle, then wait for silence.
		uint32_t quiet = 0;
		for(uint32_t block = 0; block < 172 || quiet < 16; ++block)
		{
			require(block < 172 * 10, "the machine never fell silent");
			quiet = bench.process() < threshold ? quiet + 1 : 0;
		}
		bench.note(0x90, 36, 100, 0);
		std::vector<std::vector<float>> channels(6);
		for(uint32_t block = 0; block < 172; ++block)
		{
			bench.process();
			for(size_t channel = 0; channel < 6; ++channel)
				channels[channel].insert(channels[channel].end(), bench.outputs()[channel].begin(),
					bench.outputs()[channel].end());
		}
		std::vector<uint8_t> bytes;
		for(const auto& channel : channels)
		{
			const auto* data = reinterpret_cast<const uint8_t*>(channel.data());
			bytes.insert(bytes.end(), data, data + channel.size() * sizeof(float));
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the hit");
		for(size_t channel = 0; channel < 6; ++channel)
		{
			float peak = 0;
			for(const auto value : channels[channel])
				peak = std::max(peak, std::abs(value));
			std::printf("output %zu peak %.4f\n", channel, peak);
		}
	}

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

int main(int argc, char** argv)
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
		// --startup <folder>: boot timeline only, with a factory flash cache written to that folder.
		if(argc > 2 && std::string(argv[1]) == "--startup")
			startup(path, argv[2]);
		// --hit <machine id> <file>: one hit on track 1, every output, for comparison with another engine.
		else if(argc > 3 && std::string(argv[1]) == "--hit")
			hit(path, static_cast<uint16_t>(std::atoi(argv[2])), argv[3]);
		else
			run(path);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdTrigLatencyFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
