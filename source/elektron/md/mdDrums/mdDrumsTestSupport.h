#pragma once

// What the tests of MD Drums' engine side share, header-only: require, their main (the flash image found or the test
// skipped, then the verdict), a machine by name, the pseudo-random offsets, samples compared, a file read and a
// folder of the test's own, times and memory. Include it after the other headers: on Windows it
// includes windows.h, whose macros (near, far, ERROR and the like) would reach them.

#include "mdDrumsEngine.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace mdDrums::test
{
	inline void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	inline void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// A test's main: stdout unbuffered (_IOLBF crashes the Microsoft runtime at startup), _withoutFlash, which needs no
	// flash image, then the flash image found, or the test skipped (exit 77, which ctest reports as Skipped), and _body
	// with it. _passed when both return (exit 0), FAIL and the reason when one throws (exit 1).
	template<class TWithoutFlash, class TBody>
	int run(const char* const _name, TWithoutFlash&& _withoutFlash, TBody&& _body, const char* const _passed = "PASS")
	{
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		try
		{
			_withoutFlash();
			const auto flash = Engine::findFlashImage({});
			if(flash.empty())
			{
				std::printf("%s: SKIP (no Machinedrum UW OS 1.63 flash image found)\n", _name);
				return 77;
			}
			_body(flash);
		}
		catch(const std::exception& _error)
		{
			std::printf("%s: FAIL: %s\n", _name, _error.what());
			return 1;
		}
		std::printf("%s: %s\n", _name, _passed);
		return 0;
	}

	// The same with nothing to run before the flash image
	template<class TBody> int run(const char* const _name, TBody&& _body)
	{
		return run(_name, [] {}, _body);
	}

	// A measurement's main, not a gate's: "done" where a test says PASS
	template<class TBody> int measure(const char* const _name, TBody&& _body)
	{
		return run(_name, [] {}, _body, "done");
	}

	// The machine the OS's table names _name (MachineInfo::name, "TRXBD"); throws when it has none
	inline const md::engine::MachineInfo& machineNamed(const std::vector<md::engine::MachineInfo>& _machines,
		const std::string& _name)
	{
		for(const auto& machine : _machines)
			if(machine.name == _name)
				return machine;
		throw std::runtime_error("no machine " + _name + " in the OS's table");
	}

	inline uint8_t machineId(const std::vector<md::engine::MachineInfo>& _machines, const std::string& _name)
	{
		return machineNamed(_machines, _name).id;
	}

	// The next state of a xorshift generator (13, 17, 5): the same pseudo-random offsets and noise on every run
	inline uint32_t xorshift(uint32_t& _state)
	{
		_state ^= _state << 13;
		_state ^= _state >> 17;
		_state ^= _state << 5;
		return _state;
	}

	// The samples that differ between two renders, each one the shorter lacks counted as one
	template<class T> size_t differences(const std::vector<T>& _a, const std::vector<T>& _b)
	{
		const auto common = std::min(_a.size(), _b.size());
		size_t differ = std::max(_a.size(), _b.size()) - common;
		for(size_t i = 0; i < common; ++i)
			differ += _a[i] != _b[i];
		return differ;
	}

	// A whole file, as bytes or as a std::string; empty when it cannot be read
	template<class T = std::vector<uint8_t>> T readFile(const std::filesystem::path& _file)
	{
		std::ifstream in(_file, std::ios::binary);
		return T(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	// A folder in the system's temporary one that no other run names, which what the test writes there creates, removed
	// with all it holds when the object goes
	class TempFolder
	{
	public:
		explicit TempFolder(const std::string& _prefix)
			: m_path(std::filesystem::temp_directory_path()
				/ (_prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
		{
		}

		~TempFolder()
		{
			std::error_code error;
			std::filesystem::remove_all(m_path, error);
		}

		TempFolder(const TempFolder&) = delete;
		TempFolder& operator=(const TempFolder&) = delete;

		const std::filesystem::path& path() const { return m_path; }

	private:
		std::filesystem::path m_path;
	};

	inline double msSince(const std::chrono::steady_clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start).count();
	}

	// The process's private memory, in MB (0 elsewhere than on Windows)
	inline double privateMb()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
			sizeof(counters));
		return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
#else
		return 0.0;
#endif
	}
}
