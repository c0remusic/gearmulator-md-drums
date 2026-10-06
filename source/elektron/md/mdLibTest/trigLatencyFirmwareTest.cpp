// Note-to-sound latency of the Machinedrum: a host note reaches the firmware's MIDI input, the firmware
// triggers a track, and this measures the samples from the note's position in the host block to the first
// audible sample at the plug-in's output, at each plug-in latency setting. It also reports which notes
// sound, what the firmware sends back, and whether the velocity changes the level.

#include "firmwareBench.h"

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
	using namespace mdFirmwareBench;

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

	// One hit of each machine, one after the other on one booted machine, the n-th on track n + 2 so that no voice
	// has played before (see recordHit; the boot probes track 1): writes, for each, its id, its track (0-15), its
	// sample count and a second of output A from the note on (three 32-bit unsigned, then 32-bit floats;
	// little-endian), the reference mdEngineFirmwareTest compares the engines with.
	void hits(const char* _path, const std::string& _file, const std::vector<uint16_t>& _machines)
	{
		Bench bench(_path, 0);
		const auto threshold = std::max(1e-4f, bootUntilNotesPlay(bench) * 8.0f);
		std::vector<uint8_t> bytes;
		const auto append = [&](const void* _data, const size_t _size)
		{
			const auto* data = static_cast<const uint8_t*>(_data);
			bytes.insert(bytes.end(), data, data + _size);
		};
		require(_machines.size() < 16, "more machines than fresh tracks");
		for(size_t i = 0; i < _machines.size(); ++i)
		{
			const auto track = static_cast<uint8_t>(i + 1);
			const auto channels = recordHit(bench, threshold, _machines[i], track);
			const uint32_t id = _machines[i], trackIndex = track, count = static_cast<uint32_t>(channels[0].size());
			append(&id, sizeof(id));
			append(&trackIndex, sizeof(trackIndex));
			append(&count, sizeof(count));
			append(channels[0].data(), channels[0].size() * sizeof(float));
			for(size_t channel = 0; channel < channels.size(); ++channel)
			{
				float peak = 0;
				for(const auto value : channels[channel])
					peak = std::max(peak, std::abs(value));
				std::printf("machine %u output %zu peak %.4f\n", id, channel, peak);
			}
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write the hits");
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
					++silent;
			}
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
		// --hits <file> <machine id>...: one hit of each machine on track 1, for comparison with another engine.
		else if(argc > 3 && std::string(argv[1]) == "--hits")
		{
			std::vector<uint16_t> machines;
			for(int i = 3; i < argc; ++i)
				machines.push_back(static_cast<uint16_t>(std::atoi(argv[i])));
			hits(path, argv[2], machines);
		}
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
