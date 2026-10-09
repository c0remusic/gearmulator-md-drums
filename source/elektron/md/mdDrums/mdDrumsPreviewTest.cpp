// Measurement, not a gate (ticket 12 of the editor map): what a second engine costs the Hit screen and the browser's
// preview, which would render a Track's Hit again while a knob turns. Per kind of engine (MD Drums' own, with its
// master effects; mdEngine's alone, without them): the memory the process takes for one, the time to build it, the
// time to render a Hit's 0.8 s on its own output for a few machines, and whether the same Hit renders the same twice
// on one engine and on two.
// usage: mdDrumsPreviewTest

#include "mdDrumsEngine.h"

#include "Firmware.h"
#include "MdEngine.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// The process's private memory, in MB
	double privateMb()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
#else
		return 0.0;
#endif
	}

	double msSince(const std::chrono::steady_clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start).count();
	}

	uint8_t machineId(const std::vector<md::engine::MachineInfo>& _machines, const char* _name)
	{
		for(const auto& machine : _machines)
			if(machine.name == _name)
				return machine.id;
		throw std::runtime_error(std::string("no machine ") + _name);
	}

	constexpr size_t g_hitSamples = 35280;	// 0.8 s

	// A Hit of _machine on track 0 at velocity 100 and its 0.8 s on the track's own output, MD Drums' engine
	std::vector<float> hit(mdDrums::Engine& _engine, const uint8_t _machine)
	{
		_engine.setMachine(0, _machine);
		std::vector<float> settle(4410);
		std::array<float*, mdDrums::Engine::OutputCount> outputs{};
		outputs[2] = settle.data();
		_engine.render(outputs.data(), settle.size());
		_engine.trigger(0, 100);
		std::vector<float> samples(g_hitSamples);
		outputs[2] = samples.data();
		_engine.render(outputs.data(), samples.size());
		return samples;
	}

	// The same on mdEngine's engine alone: the track after its effects (its own output before VOL)
	std::vector<int32_t> hit(md::engine::Engine& _engine, const uint8_t _machine)
	{
		auto& host = _engine.host();
		host.setMachine(0, _machine);
		for(int param = 8; param < mdDrums::Engine::ParamCount; ++param)
			host.setParam(0, param, mdDrums::Engine::paramDefaults()[param]);
		host.setLevel(0, 100);
		md::engine::Engine::Output out;
		for(int block = 0; block < 138; ++block)
			require(_engine.render(out), "engine fault");
		host.trigger(0, 100);
		std::vector<int32_t> samples;
		while(samples.size() < g_hitSamples)
		{
			require(_engine.render(out), "engine fault");
			samples.insert(samples.end(), out.tracks[0].begin(), out.tracks[0].end());
		}
		samples.resize(g_hitSamples);
		return samples;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsPreviewTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		const auto bank = md::fw::loadRomBankFromFlash(flash);
		const char* machines[] = {"TRXBD", "TRXSD", "EFMBD", "E12SD", "P-IMT", "ROM01"};

		// MD Drums' engine, master effects included
		{
			const auto before = privateMb();
			const auto start = std::chrono::steady_clock::now();
			auto engine = std::make_unique<mdDrums::Engine>(flash);
			const auto built = msSince(start);
			const auto memory = privateMb() - before;
			std::printf("MD Drums' engine: %.1f MB, built in %.1f ms\n", memory, built);
			std::string line;
			for(const auto* name : machines)
			{
				const auto id = machineId(engine->machines(), name);
				const auto t0 = std::chrono::steady_clock::now();
				const auto first = hit(*engine, id);
				const auto ms = msSince(t0);
				const auto second = hit(*engine, id);
				size_t differ = 0;
				for(size_t i = 0; i < first.size(); ++i)
					differ += first[i] != second[i];
				char text[128];
				(void)std::snprintf(text, sizeof(text), " %s %.1f ms (%zu of %zu samples differ struck again)", name, ms,
					differ, first.size());
				line += text;
			}
			std::printf("  a Hit's 0.8 s and 0.1 s before it:%s\n", line.c_str());
		}

		// mdEngine's engine alone, no master effects
		{
			const auto before = privateMb();
			const auto start = std::chrono::steady_clock::now();
			auto engine = std::make_unique<md::engine::Engine>(os.firmware, os.osImage);
			engine->loadRomBank(bank);
			const auto built = msSince(start);
			const auto memory = privateMb() - before;
			std::printf("mdEngine's engine alone: %.1f MB, built in %.1f ms\n", memory, built);
			auto other = std::make_unique<md::engine::Engine>(os.firmware, os.osImage);
			other->loadRomBank(bank);
			std::string line;
			for(const auto* name : machines)
			{
				const auto id = machineId(engine->os().machines(), name);
				const auto t0 = std::chrono::steady_clock::now();
				const auto first = hit(*engine, id);
				const auto ms = msSince(t0);
				const auto again = hit(*engine, id);
				const auto fresh = hit(*other, id);
				size_t differAgain = 0, differFresh = 0;
				for(size_t i = 0; i < first.size(); ++i)
				{
					differAgain += first[i] != again[i];
					differFresh += first[i] != fresh[i];
				}
				char text[160];
				(void)std::snprintf(text, sizeof(text), " %s %.1f ms (struck again %zu differ, another engine %zu)", name, ms,
					differAgain, differFresh);
				line += text;
			}
			std::printf("  a Hit's 0.8 s and 0.1 s before it:%s\n", line.c_str());
		}
		std::printf("flash image kept: %.1f MB\n", static_cast<double>(flash.size()) / (1024.0 * 1024.0));
		std::cout << "mdDrumsPreviewTest: done\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsPreviewTest: " << _error.what() << '\n';
		return 1;
	}
}
