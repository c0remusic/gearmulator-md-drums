// Measurement, not a gate (ticket 25 of the editor map): what splitting the 16 voices across threads
// (md::engine::ParallelVoiceEngine) does for the engine MD Drums plays, against the single voice DSP it plays today.
// 16 tracks of TRX and EFM machines, each struck in turn, so that after the first 16 Hits every voice renders every
// block, with the master effects; on each engine the same blocks and Hits. Per configuration: the time on the calling
// thread (the audio thread in the plug-in) per block and per host block of 256 samples, the CPU of the whole process,
// and whether Main stays the same, sample for sample, as the single voice DSP's.
// usage: mdDrumsParallelTest [seconds]

#include "mdDrumsEngine.h"

#include "Firmware.h"
#include "MdEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctime>
#endif

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// CPU time of the whole process, every thread, in seconds
	double processCpu()
	{
#ifdef _WIN32
		FILETIME creation, exit, kernel, user;
		GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
		const auto seconds = [](const FILETIME& _t)
		{
			return static_cast<double>((static_cast<uint64_t>(_t.dwHighDateTime) << 32) | _t.dwLowDateTime) * 1e-7;
		};
		return seconds(kernel) + seconds(user);
#else
		return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
	}

	uint8_t machineId(const std::vector<md::engine::MachineInfo>& _machines, const char* _name)
	{
		for(const auto& machine : _machines)
			if(machine.name == _name)
				return machine.id;
		throw std::runtime_error(std::string("machine missing from the OS's table: ") + _name);
	}

	struct Result
	{
		std::vector<double> blockUs;		// per engine block, on the calling thread
		double cpuSeconds = 0;				// the whole process
		std::vector<int32_t> main;			// Main, L R interleaved
	};

	template<class TEngine>
	Result run(TEngine& _engine, const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const int _blocks)
	{
		_engine.loadRomBank(md::fw::loadRomBankFromFlash(_flash));
		_engine.enableMaster(_os.firmware);
		auto& host = _engine.host();
		const char* kit[16] = {"TRXBD", "TRXSD", "TRXXT", "TRXCP", "TRXRS", "TRXCB", "TRXCH", "TRXOH",
			"TRXCY", "TRXMA", "TRXCL", "TRXXC", "TRXB2", "TRXS2", "EFMBD", "EFMSD"};
		for(int track = 0; track < 16; ++track)
		{
			host.setMachine(track, machineId(_engine.os().machines(), kit[track]));
			for(int param = 8; param < mdDrums::Engine::ParamCount; ++param)
				host.setParam(track, param, mdDrums::Engine::paramDefaults()[param]);
			host.setLevel(track, 100);
		}
		for(int index = 0; index < mdDrums::Engine::MasterCount; ++index)
			host.setMaster(index, mdDrums::Engine::masterDefaults()[index]);

		typename TEngine::Output out;
		const auto block = [&](const int _b)
		{
			if(_b % 21 == 0)
				host.trigger((_b / 21) % 16, 100);
			require(_engine.render(out), "engine fault: " + _engine.fault());
		};
		// The JIT compiles each DSP's code on its first blocks: a second before measuring
		for(int b = 0; b < 1378; ++b)
			block(b);

		Result result;
		result.blockUs.reserve(static_cast<size_t>(_blocks));
		result.main.reserve(static_cast<size_t>(_blocks) * 64);
		const auto cpu0 = processCpu();
		for(int b = 0; b < _blocks; ++b)
		{
			const auto t0 = std::chrono::steady_clock::now();
			block(1378 + b);
			result.blockUs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
			for(const auto& frame : out.master)
			{
				result.main.push_back(frame[0]);
				result.main.push_back(frame[1]);
			}
		}
		result.cpuSeconds = processCpu() - cpu0;
		return result;
	}

	void report(const char* _name, const Result& _result, const Result& _reference)
	{
		constexpr double blockUs = 32.0 / 44100.0 * 1e6;
		const auto blocks = _result.blockUs.size();
		auto sorted = _result.blockUs;
		std::sort(sorted.begin(), sorted.end());
		const auto total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
		// Host blocks of 256 samples: 8 engine blocks each
		double worstHost = 0;
		for(size_t b = 0; b + 8 <= blocks; b += 8)
			worstHost = std::max(worstHost, std::accumulate(_result.blockUs.begin() + static_cast<std::ptrdiff_t>(b),
				_result.blockUs.begin() + static_cast<std::ptrdiff_t>(b + 8), 0.0));
		size_t differ = 0;
		for(size_t i = 0; i < _result.main.size() && i < _reference.main.size(); ++i)
			differ += _result.main[i] != _reference.main[i];
		const auto audio = static_cast<double>(blocks) * blockUs * 1e-6;
		std::printf("%-26s calling thread %5.1f %% of real time (block mean %5.1f us, p99 %6.1f, max %7.1f; worst 256-sample"
			" host block %5.1f %% of its time); process CPU %5.1f %% of one core; Main: %zu of %zu samples differ\n",
			_name, total * 1e-6 / audio * 100.0, total / static_cast<double>(blocks), sorted[blocks * 99 / 100],
			sorted.back(), worstHost / (8 * blockUs) * 100.0, _result.cpuSeconds / audio * 100.0, differ,
			_result.main.size());
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsParallelTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		const double seconds = _argc > 1 ? std::atof(_argv[1]) : 10.0;
		const auto blocks = static_cast<int>(seconds * 44100.0 / 32.0);
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		std::printf("16 tracks struck in turn, every voice rendering, master effects, %.0f s of audio per configuration\n",
			seconds);

		Result reference;
		{
			md::engine::Engine engine(os.firmware, os.osImage);
			reference = run(engine, flash, os, blocks);
		}
		report("one voice DSP (today)", reference, reference);
		for(const int groups : {1, 2, 4})
		{
			md::engine::ParallelEngine engine(os.firmware, os.osImage, groups);
			const auto result = run(engine, flash, os, blocks);
			const auto name = std::to_string(groups) + (groups == 1 ? " group" : " groups") + " (parallel)";
			report(name.c_str(), result, reference);
		}
		std::cout << "mdDrumsParallelTest: done\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsParallelTest: " << _error.what() << '\n';
		return 1;
	}
}
