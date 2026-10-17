// Measurement, not a gate (ticket 12 of the editor map): what a second engine costs the Hit screen and the browser's
// preview, which would render a Track's Hit again while a knob turns. Per kind of engine (MD Drums' own, with its
// master effects; mdEngine's alone, without them): the memory the process takes for one, the time to build it, the
// time to render a Hit's 0.8 s on its own output for a few machines, and whether the same Hit renders the same twice
// on one engine and on two.
// usage: mdDrumsPreviewTest

#include "mdDrumsEngine.h"

#include "Firmware.h"
#include "MdEngine.h"

#include "mdDrumsTestSupport.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using mdDrums::test::differences;
	using mdDrums::test::machineId;
	using mdDrums::test::msSince;
	using mdDrums::test::privateMb;
	using mdDrums::test::require;

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

	// The same on mdEngine's engine alone, which starts as MD Drums' (applyEngineDefaults): the track after its effects
	// (its own output before VOL)
	std::vector<int32_t> hit(md::engine::Engine& _engine, const uint8_t _machine)
	{
		auto& host = _engine.host();
		host.setMachine(0, _machine);
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
	return mdDrums::test::measure("mdDrumsPreviewTest", [](const std::vector<uint8_t>& _flash)
	{
		const auto os = md::fw::loadFirmwareFromFlash(_flash);
		const auto bank = md::fw::loadRomBankFromFlash(_flash);
		const char* machines[] = {"TRXBD", "TRXSD", "EFMBD", "E12SD", "P-IMT", "ROM01"};

		// MD Drums' engine, master effects included
		{
			const auto before = privateMb();
			const auto start = std::chrono::steady_clock::now();
			auto engine = std::make_unique<mdDrums::Engine>(_flash);
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
				const auto differ = differences(first, second);
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
			mdDrums::applyEngineDefaults(engine->host());
			const auto built = msSince(start);
			const auto memory = privateMb() - before;
			std::printf("mdEngine's engine alone: %.1f MB, built in %.1f ms\n", memory, built);
			auto other = std::make_unique<md::engine::Engine>(os.firmware, os.osImage);
			other->loadRomBank(bank);
			mdDrums::applyEngineDefaults(other->host());
			std::string line;
			for(const auto* name : machines)
			{
				const auto id = machineId(engine->os().machines(), name);
				const auto t0 = std::chrono::steady_clock::now();
				const auto first = hit(*engine, id);
				const auto ms = msSince(t0);
				const auto differAgain = differences(first, hit(*engine, id));
				const auto differFresh = differences(first, hit(*other, id));
				char text[160];
				(void)std::snprintf(text, sizeof(text), " %s %.1f ms (struck again %zu differ, another engine %zu)", name, ms,
					differAgain, differFresh);
				line += text;
			}
			std::printf("  a Hit's 0.8 s and 0.1 s before it:%s\n", line.c_str());
		}
		std::printf("flash image kept: %.1f MB\n", static_cast<double>(_flash.size()) / (1024.0 * 1024.0));
	});
}
