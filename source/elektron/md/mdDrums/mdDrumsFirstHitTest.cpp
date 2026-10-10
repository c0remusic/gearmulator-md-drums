// Ticket 31 of the editor map: what a Track's first Hit costs against the next ones, on mdEngine's engine with factory
// Kit 1 and the master effects, split between the OS's code in the 68k emulator (the tick) and the voice DSP; and what
// the engine's first block costs, the master's in it. Then the same on an engine whose machines and master were
// compiled ahead (EngineT::warmUpVoices, MasterEngine::warmUp). mdDrumsInstancesTest saw blocks of 5 to 9 ms against a
// 2.9 ms deadline, all at a Track's first Hit; mdDrumsVst3HostTest a first block of 27 ms, the master's. The times are
// printed only (they depend on the PC); the test fails when the two engines do not give the same samples, the Tracks'
// and Main's.
// usage: mdDrumsFirstHitTest

#include "mdDrumsEngine.h"
#include "mdDrumsMessages.h"

#include "Firmware.h"
#include "MdEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	struct Cost
	{
		double worstBlockMs = 0.0;	// the slowest 32-sample block from the Hit on
		double tickMs = 0.0;		// in it: the OS's code
		double dspMs = 0.0;			// in it: the voice DSP
	};

	struct Samples
	{
		std::vector<int32_t> tracks;
		std::vector<int32_t> main;
	};

	void append(const md::engine::Engine::Output& _out, Samples& _samples)
	{
		for(const auto& track : _out.tracks)
			_samples.tracks.insert(_samples.tracks.end(), track.begin(), track.end());
		for(const auto& frame : _out.master)
			_samples.main.insert(_samples.main.end(), frame.begin(), frame.end());
	}

	size_t differences(const std::vector<int32_t>& _a, const std::vector<int32_t>& _b)
	{
		size_t differ = 0;
		for(size_t i = 0; i < _a.size(); ++i)
			differ += _a[i] != _b[i];
		return differ;
	}

	// An engine with factory Kit 1 and the master effects, a second rendered: its first block printed, the samples
	// appended to _samples
	std::unique_ptr<md::engine::Engine> build(const std::vector<uint8_t>& _flash, const md::automation::sysex::MdKit& _kit,
		const bool _warm, Samples& _samples)
	{
		auto os = md::fw::loadFirmwareFromFlash(_flash);
		auto engine = std::make_unique<md::engine::Engine>(os.firmware, std::move(os.osImage));
		engine->loadRomBank(md::fw::loadRomBankFromFlash(_flash));
		engine->enableMaster(os.firmware);
		if(_warm)
		{
			const auto start = Clock::now();
			if(engine->warmUpVoices() < 0)
				throw std::runtime_error("the voices' warm-up faulted");
			const auto voices = Clock::now();
			if(!engine->masterEngine()->warmUp())
				throw std::runtime_error("the master's warm-up faulted");
			std::printf("machines compiled ahead in %.0f ms, the master in %.0f ms\n",
				std::chrono::duration<double, std::milli>(voices - start).count(),
				std::chrono::duration<double, std::milli>(Clock::now() - voices).count());
		}
		engine->timingOn = true;
		auto& host = engine->host();
		for(int t = 0; t < 16; ++t)
		{
			host.setMachine(t, _kit.machine(static_cast<uint8_t>(t)));
			for(int p = 0; p < 24; ++p)
				host.setParam(t, p, _kit.parameters[t][p]);
			host.setLevel(t, _kit.levels[t]);
			host.loadLfo(t, _kit.lfos[t].data());
		}
		for(int index = 0; index < mdDrums::Engine::MasterCount; ++index)
			host.setMaster(index, _kit.masterEffects[static_cast<size_t>(index)]);
		md::engine::Engine::Output out;
		double worst = 0.0;
		for(int block = 0; block < 1378; ++block)
		{
			const auto master0 = engine->masterUs;
			const auto start = Clock::now();
			if(!engine->render(out))
				throw std::runtime_error("engine fault");
			const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
			if(block == 0)
			{
				std::printf("%s engine, first block %.2f ms (the master %.2f ms)", _warm ? "warmed" : "cold", ms,
					(engine->masterUs - master0) / 1000.0);
			}
			else
			{
				worst = std::max(worst, ms);
			}
			append(out, _samples);
		}
		std::printf(", the next ones %.2f ms at worst\n", worst);
		return engine;
	}

	// _track struck, then 0.1 s: the slowest block and its parts; the samples appended to _samples
	Cost strike(md::engine::Engine& _engine, const int _track, Samples& _samples)
	{
		auto& host = _engine.host();
		host.trigger(_track, 100);
		md::engine::Engine::Output out;
		Cost cost;
		for(int block = 0; block < 138; ++block)
		{
			const auto tick0 = host.tickUs, dsp0 = host.dspUs;
			const auto start = Clock::now();
			if(!_engine.render(out))
				throw std::runtime_error("engine fault");
			const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
			if(ms > cost.worstBlockMs)
				cost = {ms, (host.tickUs - tick0) / 1000.0, (host.dspUs - dsp0) / 1000.0};
			append(out, _samples);
		}
		return cost;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsFirstHitTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		const auto kit = mdDrums::messages::factoryKit(flash);
		if(!kit)
			throw std::runtime_error("no factory Kit");
		Samples samples[2];
		for(const bool warm : {false, true})
		{
			auto engine = build(flash, *kit, warm, samples[warm]);
			for(int round = 1; round <= 2; ++round)
			{
				double worst = 0.0;
				std::string line;
				for(int t = 0; t < 16; ++t)
				{
					const auto cost = strike(*engine, t, samples[warm]);
					worst = std::max(worst, cost.worstBlockMs);
					char text[64];
					(void)std::snprintf(text, sizeof(text), " %.2f (DSP %.2f)", cost.worstBlockMs, cost.dspMs);
					line += text;
				}
				std::printf("%s engine, round %d, each Track's worst block in ms (a block is 0.73 ms of audio):%s; worst %.2f\n",
					warm ? "warmed" : "cold", round, line.c_str(), worst);
			}
			if(!warm)
				continue;

			// Other Kits' master effects: each master parameter through its range, a Hit sounding, on the warmed engine
			// (code the warm-up did not reach compiles here)
			auto& host = engine->host();
			md::engine::Engine::Output out;
			double worstMaster = 0.0;
			int worstIndex = 0, worstValue = 0;
			for(int index = 0; index < mdDrums::Engine::MasterCount; ++index)
			{
				for(const int value : {0, 32, 64, 96, 127})
				{
					host.setMaster(index, value);
					host.trigger(index % 16, 100);
					for(int block = 0; block < 12; ++block)
					{
						const auto master0 = engine->masterUs;
						if(!engine->render(out))
							throw std::runtime_error("engine fault");
						const auto ms = (engine->masterUs - master0) / 1000.0;
						if(ms > worstMaster)
						{
							worstMaster = ms;
							worstIndex = index;
							worstValue = value;
						}
					}
				}
				host.setMaster(index, kit->masterEffects[static_cast<size_t>(index)]);
			}
			std::printf("warmed engine, each master parameter through its range: the master's worst block %.2f ms "
				"(parameter %d at %d)\n", worstMaster, worstIndex, worstValue);
		}
		const auto differTracks = differences(samples[0].tracks, samples[1].tracks);
		const auto differMain = differences(samples[0].main, samples[1].main);
		const auto heard = static_cast<size_t>(std::count_if(samples[0].main.begin(), samples[0].main.end(),
			[](const int32_t _s) { return _s != 0; }));
		std::printf("the two engines: %zu of %zu Track samples differ, %zu of %zu Main samples (%zu of them not silent)\n",
			differTracks, samples[0].tracks.size(), differMain, samples[0].main.size(), heard);
		const auto differ = differTracks + differMain + (heard ? 0 : 1);
		std::cout << "mdDrumsFirstHitTest: " << (differ ? "the warmed engine sounds different" : "done") << '\n';
		return differ ? 1 : 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsFirstHitTest: " << _error.what() << '\n';
		return 1;
	}
}
