// Ticket 31 of the editor map: what a Track's first Hit costs against the next ones, on mdEngine's engine with factory
// Kit 1, split between the OS's code in the 68k emulator (the tick) and the voice DSP; then the same on an engine whose
// machines were compiled ahead (EngineT::warmUpVoices). mdDrumsInstancesTest saw blocks of 5 to 9 ms against a 2.9 ms
// deadline, all at a Track's first Hit. The times are printed only (they depend on the PC); the test fails when the two
// engines do not give the same samples.
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

	// An engine with factory Kit 1, a second rendered
	std::unique_ptr<md::engine::Engine> build(const std::vector<uint8_t>& _flash, const md::automation::sysex::MdKit& _kit,
		const bool _warm, double& _warmMs)
	{
		auto os = md::fw::loadFirmwareFromFlash(_flash);
		auto engine = std::make_unique<md::engine::Engine>(os.firmware, std::move(os.osImage));
		engine->loadRomBank(md::fw::loadRomBankFromFlash(_flash));
		const auto start = Clock::now();
		if(_warm && engine->warmUpVoices() < 0)
			throw std::runtime_error("the warm-up faulted");
		_warmMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
		md::engine::Engine::Output out;
		for(int block = 0; block < 1378; ++block)
			engine->render(out);
		return engine;
	}

	// _track struck, then 0.1 s: the slowest block and its parts; the Tracks' samples appended to _samples
	Cost strike(md::engine::Engine& _engine, const int _track, std::vector<int32_t>& _samples)
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
			for(const auto& track : out.tracks)
				_samples.insert(_samples.end(), track.begin(), track.end());
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
		std::vector<int32_t> samples[2];
		for(const bool warm : {false, true})
		{
			double warmMs = 0.0;
			auto engine = build(flash, *kit, warm, warmMs);
			if(warm)
				std::printf("machines compiled ahead in %.0f ms\n", warmMs);
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
		}
		size_t differ = 0;
		for(size_t i = 0; i < samples[0].size(); ++i)
			differ += samples[0][i] != samples[1][i];
		std::printf("the two engines: %zu of %zu Track samples differ\n", differ, samples[0].size());
		std::cout << "mdDrumsFirstHitTest: " << (differ ? "the warmed engine sounds different" : "done") << '\n';
		return differ ? 1 : 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsFirstHitTest: " << _error.what() << '\n';
		return 1;
	}
}
