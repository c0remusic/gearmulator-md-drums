// The engines without the ColdFire OS (mdEngine) against the full firmware: one hit of each machine in
// g_hitMachines, with the same 24 track parameters and level, routed to an individual output, must give the same
// samples (2026-10-06: residuals 99-110 dB down, the 32-bit float conversion's). The firmware's hits come from `mdTrigLatencyFirmwareTest --hits` (a separate
// process: Musashi's entry points serve one CPU class per executable); the engine reads its OS and DSP programs
// from the same 8 MB flash image the firmware boots from.
// usage: mdEngineFirmwareTest <hits file>

#include "hitParameters.h"

#include "MdEngine.h"
#include "Firmware.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace mdFirmwareBench;

	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::vector<uint8_t> readAll(const char* _path)
	{
		std::ifstream file(_path, std::ios::binary);
		require(file.good(), "cannot read a file");
		return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	}

	struct FirmwareHit
	{
		int track = 0;
		std::vector<float> samples;
	};

	// The firmware's hits by machine id, as mdTrigLatencyFirmwareTest --hits writes them.
	std::map<uint16_t, FirmwareHit> readHits(const char* _path)
	{
		const auto bytes = readAll(_path);
		std::map<uint16_t, FirmwareHit> hits;
		size_t at = 0;
		const auto read32 = [&]
		{
			require(at + 4 <= bytes.size(), "truncated hits file");
			uint32_t value;
			std::memcpy(&value, &bytes[at], 4);
			at += 4;
			return value;
		};
		while(at < bytes.size())
		{
			const auto id = static_cast<uint16_t>(read32());
			auto& hit = hits[id];
			hit.track = static_cast<int>(read32());
			require(hit.track < 16, "a hit's track is out of range");
			const auto count = read32();
			require(at + count * sizeof(float) <= bytes.size(), "truncated hits file");
			hit.samples.resize(count);
			std::memcpy(hit.samples.data(), &bytes[at], count * sizeof(float));
			at += count * sizeof(float);
		}
		return hits;
	}

	// _track alone from the trigger on, routed to output A, as mdEngine renders the hit recordHit plays on the
	// firmware.
	std::vector<float> engineHit(md::engine::Engine& _engine, const uint8_t _machine, const int _track,
		const size_t _count)
	{
		auto& host = _engine.host();
		host.setMachine(_track, _machine);
		host.setRouting(_track, 0);
		for(int parameter = 0; parameter < static_cast<int>(g_hitParameters.size()); ++parameter)
			host.setParam(_track, parameter, g_hitParameters[parameter]);
		host.setLevel(_track, g_hitLevel);

		md::engine::Engine::Output out;
		const int channel = md::engine::Mixer::frameChannel(0);
		// Two seconds for the smoothing to settle and the previous hit to die away.
		for(int block = 0; block < 44100 * 2 / 32; ++block)
			require(_engine.render(out), "engine render fault");
		host.trigger(_track, 100);
		std::vector<float> samples;
		while(samples.size() < _count)
		{
			require(_engine.render(out), "engine render fault");
			for(int sample = 0; sample < 32; ++sample)
				samples.push_back(static_cast<float>(out.mix.frame[sample][channel]) / 8388608.0f);
		}
		samples.resize(_count);
		return samples;
	}

	size_t onset(const std::vector<float>& _samples)
	{
		for(size_t i = 0; i < _samples.size(); ++i)
			if(std::abs(_samples[i]) >= 1e-3f)
				return i;
		throw std::runtime_error("a hit stayed silent");
	}

	struct Comparison
	{
		double correlation = 0;
		double gain = 0;		// firmware over engine, by RMS
		double residualDb = 0;	// after matching the gain, relative to the signal
	};

	// Both hits from their first audible sample on.
	Comparison compare(const std::vector<float>& _firmware, const std::vector<float>& _engine)
	{
		const auto a = onset(_firmware), b = onset(_engine);
		const auto length = std::min(_firmware.size() - a, _engine.size() - b);
		double dot = 0, aa = 0, bb = 0;
		for(size_t i = 0; i < length; ++i)
		{
			dot += double(_firmware[a + i]) * _engine[b + i];
			aa += double(_firmware[a + i]) * _firmware[a + i];
			bb += double(_engine[b + i]) * _engine[b + i];
		}
		Comparison result;
		result.correlation = dot / std::sqrt(aa * bb);
		result.gain = std::sqrt(aa / bb);
		double residual = 0;
		for(size_t i = 0; i < length; ++i)
		{
			const auto difference = _firmware[a + i] - result.gain * _engine[b + i];
			residual += difference * difference;
		}
		result.residualDb = 10.0 * std::log10(residual / aa);
		return result;
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "mdEngineFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n";
		return 77;
	}
	if(_argc < 2)
	{
		std::cerr << "usage: mdEngineFirmwareTest <hits file from mdTrigLatencyFirmwareTest --hits>\n";
		return 2;
	}
	try
	{
		auto os = md::fw::loadFirmwareFromFlash(readAll(path));
		md::engine::Engine engine(os.firmware, std::move(os.osImage));
		const auto hits = readHits(_argv[1]);

		for(const auto id : g_hitMachines)
		{
			const auto* machine = engine.os().machine(static_cast<uint8_t>(id));
			require(machine != nullptr, "machine missing from the OS's table");
			const auto firmware = hits.find(id);
			require(firmware != hits.end(), "the hits file lacks a machine");

			const auto& hit = firmware->second;
			const auto result = compare(hit.samples,
				engineHit(engine, static_cast<uint8_t>(id), hit.track, hit.samples.size()));
			std::printf("%-6s id %3u track %2d: correlation %.6f, gain %.4f (%+.2f dB), residual %.1f dB\n",
				machine->name.c_str(), id, hit.track + 1, result.correlation, result.gain,
				20.0 * std::log10(result.gain), result.residualDb);
			require(result.correlation > 0.999999, "the engine's hit differs from the firmware's");
			require(std::abs(20.0 * std::log10(result.gain)) < 0.01, "the engine's hit is louder or quieter");
			require(result.residualDb < -90.0, "the engine's hit leaves a residual above -90 dB");
		}
		std::cout << "mdEngineFirmwareTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdEngineFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
