// The engines without the ColdFire OS (mdEngine) against the full firmware: one hit of each machine in
// g_hitMachines, with the same 24 track parameters and level, routed to an individual output, must give the same
// samples (2026-10-06: residuals 99-110 dB down, the 32-bit float conversion's; a ROM machine within
// hitResidualFloorDb, as the firmware's own ROM hits vary). The firmware's hits come from `mdTrigLatencyFirmwareTest --hits` (a separate
// process: Musashi's entry points serve one CPU class per executable); the engine reads its OS and DSP programs
// from the same 8 MB flash image the firmware boots from. The UW bank the engine copies into its voice DSP must
// equal, word for word, what the firmware's boot leaves in DSP2 (`mdTrigLatencyFirmwareTest --dsp2`, from $147e00).
// The Links, Chokes and Mute of g_groupScenarios (`mdTrigLatencyFirmwareTest --groups`) must give the same samples
// wherever both sound or both are silent, and part only where the firmware's tick timing decides (apartBlocks).
// usage: mdEngineFirmwareTest <hits file> <DSP2 dump> [<group scenarios file>]

#include "hitParameters.h"

#include "MdEngine.h"
#include "Firmware.h"

#include "mdProtocol/mdkit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
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

	void require(const bool _condition, const std::string& _message)
	{
		require(_condition, _message.c_str());
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

	// The UW bank as loadRomBankFromFlash makes it, against DSP2's P memory after the firmware's boot (little-endian
	// 32-bit words from fw::RomBank::DirectoryAddr): every ROM slot's directory entry, then every sample word.
	void compareRomBank(const md::fw::RomBank& _bank, const std::vector<uint8_t>& _dump)
	{
		const auto word = [&](const uint32_t _address)
		{
			const size_t at = static_cast<size_t>(_address - md::fw::RomBank::DirectoryAddr) * 4;
			require(_address >= md::fw::RomBank::DirectoryAddr && at + 4 <= _dump.size(),
				"the DSP2 dump does not cover the bank");
			uint32_t value;
			std::memcpy(&value, &_dump[at], 4);
			return value & 0xffffff;
		};
		size_t entries = 0, data = 0;
		for(const auto& entry : _bank.directory)
			for(uint32_t i = 0; i < 4; ++i)
				entries += word(entry.addr + i) != entry.words[i];
		for(size_t i = 0; i < _bank.data.size(); ++i)
			data += word(md::fw::RomBank::DataAddr + static_cast<uint32_t>(i)) != _bank.data[i];
		std::printf("UW bank: %u samples in %zu words; against the firmware's boot, %zu directory and %zu sample words"
			" differ\n", _bank.samples, _bank.data.size(), entries, data);
		require(_bank.samples > 0, "the flash image holds no UW bank");
		require(entries == 0 && data == 0, "the engine's UW bank differs from the one the firmware's boot leaves in DSP2");
	}

	// _track alone from the trigger on, routed to output A, as mdEngine renders the hit recordHit plays on the
	// firmware.
	std::vector<float> engineHit(md::engine::Engine& _engine, const uint8_t _machine, const int _track,
		const size_t _count)
	{
		auto& host = _engine.host();
		host.setMachine(_track, _machine);
		host.setRouting(_track, 0);
		const auto parameters = hitParameters(_machine);
		for(int parameter = 0; parameter < static_cast<int>(parameters.size()); ++parameter)
			host.setParam(_track, parameter, parameters[parameter]);
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
	// The firmware's group scenarios, as mdTrigLatencyFirmwareTest --groups writes them: per scenario its index, its
	// sample count and outputs A to F.
	using Outputs = std::array<std::vector<float>, 6>;
	std::vector<Outputs> readScenarios(const char* _path)
	{
		const auto bytes = readAll(_path);
		std::vector<Outputs> scenarios;
		size_t at = 0;
		const auto read32 = [&]
		{
			require(at + 4 <= bytes.size(), "truncated scenarios file");
			uint32_t value;
			std::memcpy(&value, &bytes[at], 4);
			at += 4;
			return value;
		};
		while(at < bytes.size())
		{
			const auto index = read32();
			require(index == scenarios.size(), "scenarios out of order");
			const auto count = read32();
			require(at + 6 * count * sizeof(float) <= bytes.size(), "truncated scenarios file");
			auto& outputs = scenarios.emplace_back();
			for(auto& output : outputs)
			{
				output.resize(count);
				std::memcpy(output.data(), &bytes[at], count * sizeof(float));
				at += count * sizeof(float);
			}
		}
		require(scenarios.size() == g_groupScenarios.size(), "the scenarios file lacks a scenario");
		return scenarios;
	}

	// The firmware's idle outputs hold one LSB of its 24-bit samples; an output above g_soundingLevel sounds for sure
	constexpr float g_firmwareFloor = 2e-7f;
	constexpr float g_soundingLevel = 1e-5f;

	// One scenario on a fresh engine, its tracks set as the firmware's (machines, hitParameters, level, factory Kit 1's
	// Links and Chokes, then the scenario's), its notes played at the engine blocks _blocks from the first: _count
	// samples of outputs A to F from the first note's block.
	Outputs engineScenario(const md::fw::FlashOs& _os, const md::automation::sysex::MdKit& _kit, const size_t _index,
		const std::vector<int>& _blocks, const size_t _count)
	{
		md::engine::Engine engine(_os.firmware, _os.osImage);
		auto& host = engine.host();
		for(int track = 0; track < 16; ++track)
		{
			host.setLink(track, _kit.links[track]);
			host.setChoke(track, _kit.chokes[track]);
		}
		for(const auto& track : g_groupTracks)
		{
			host.setMachine(track.track, static_cast<uint8_t>(track.machine));
			const auto parameters = hitParameters(track.machine);
			for(int parameter = 0; parameter < static_cast<int>(parameters.size()); ++parameter)
				host.setParam(track.track, parameter, parameters[parameter]);
			host.setLevel(track.track, g_hitLevel);
		}
		const auto& scenario = g_groupScenarios[_index];
		for(int output = 0; output < 6; ++output)
			if(scenario.outputs[output] != g_noTrack)
				host.setRouting(scenario.outputs[output], output);
		for(const auto& group : scenario.links)
			host.setLink(group[0], group[1]);
		for(const auto& group : scenario.chokes)
			host.setChoke(group[0], group[1]);
		for(const auto track : scenario.muted)
			host.setMute(track, true);

		md::engine::Engine::Output out;
		for(int block = 0; block < 44100 / 32; ++block)
			require(engine.render(out), "engine render fault");
		Outputs outputs;
		for(int block = 0; outputs[0].size() < _count; ++block)
		{
			for(size_t note = 0; note < scenario.notes.size(); ++note)
				if(_blocks[note] == block)
					host.trigger(scenario.notes[note].track, scenario.notes[note].velocity);
			require(engine.render(out), "engine render fault");
			for(int output = 0; output < 6; ++output)
			{
				const int channel = md::engine::Mixer::frameChannel(output);
				for(int sample = 0; sample < 32; ++sample)
					outputs[output].push_back(static_cast<float>(out.mix.frame[sample][channel]) / 8388608.0f);
			}
		}
		for(auto& output : outputs)
			output.resize(_count);
		return outputs;
	}

	// Samples in a run of at least a block at the firmware's floor: a cut or a silence, not a waveform's zero crossing
	std::vector<bool> silence(const float* _samples, const size_t _count)
	{
		std::vector<bool> silent(_count, false);
		for(size_t i = 0; i < _count;)
		{
			if(std::abs(_samples[i]) > g_firmwareFloor)
			{
				++i;
				continue;
			}
			auto end = i;
			while(end < _count && std::abs(_samples[end]) <= g_firmwareFloor)
				++end;
			if(end - i >= 32)
				std::fill(silent.begin() + static_cast<std::ptrdiff_t>(i), silent.begin() + static_cast<std::ptrdiff_t>(end), true);
			i = end;
		}
		return silent;
	}

	// An engine output against the firmware's, the engine's shifted by _shift samples
	struct Agreement
	{
		double residual = 0;	// over the samples where both sound, or both are silent
		double energy = 0;		// the firmware's, over the same samples
		size_t apart = 0;		// samples where one sounds and the other is silent
		double enginePeak = 0;

		Agreement& operator+=(const Agreement& _other)
		{
			residual += _other.residual;
			energy += _other.energy;
			apart += _other.apart;
			enginePeak = std::max(enginePeak, _other.enginePeak);
			return *this;
		}
		double ratio() const { return energy > 0 ? residual / energy : residual; }
	};

	Agreement agreement(const std::vector<float>& _firmware, const std::vector<float>& _engine, const size_t _shift)
	{
		const auto count = _firmware.size() - _shift;
		const auto silentFirmware = silence(_firmware.data() + _shift, count);
		const auto silentEngine = silence(_engine.data(), count);
		Agreement result;
		for(size_t i = 0; i < count; ++i)
		{
			const double a = _firmware[_shift + i], b = _engine[i];
			result.enginePeak = std::max(result.enginePeak, std::abs(b));
			if((silentFirmware[i] && std::abs(b) > g_soundingLevel) || (silentEngine[i] && std::abs(a) > g_soundingLevel))
			{
				++result.apart;
				continue;
			}
			result.residual += (a - b) * (a - b);
			result.energy += a * a;
		}
		return result;
	}

	// Over the outputs a scenario plays on
	Agreement scenarioAgreement(const GroupScenario& _scenario, const Outputs& _firmware, const Outputs& _engine,
		const size_t _shift)
	{
		Agreement result;
		for(size_t output = 0; output < 6; ++output)
			if(_scenario.outputs[output] != g_noTrack)
				result += agreement(_firmware[output], _engine[output], _shift);
		return result;
	}

	std::optional<size_t> firstSound(const std::vector<float>& _samples)
	{
		for(size_t i = 0; i < _samples.size(); ++i)
			if(std::abs(_samples[i]) > g_firmwareFloor)
				return i;
		return std::nullopt;
	}

	// The firmware's notes wait for its next control tick, the engine's start at its next block: each note after the
	// first is placed on the engine block whose Hit leaves the least residual, the first note's Hit aligned on the
	// firmware's. Then each output must match the firmware's within its machine's floor, a silent one stay silent.
	void compareGroups(const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const char* _path)
	{
		const auto scenarios = readScenarios(_path);
		const auto image = md::fw::loadPatchImageFromFlash(_flash);
		require(image.size() == 0x80000, "no factory patch image in the flash");
		const auto kit = md::automation::sysex::MdKit::fromRecord(&image[0x8ca]);
		require(kit.chokes[8] == 9, "factory Kit 1's track 9 does not choke track 10");

		// MD_ENGINE_GROUPS_DUMP: a file for the engine's outputs, in the firmware file's format, aligned on its samples
		std::vector<uint8_t> dump;
		std::string failure;
		for(size_t index = 0; index < scenarios.size(); ++index)
		{
			const auto& firmware = scenarios[index];
			const auto& scenario = g_groupScenarios[index];
			const auto count = firmware[0].size();

			// The first note's Hit, on its own output in both
			size_t firstOutput = 0;
			while(firstOutput < 6 && scenario.outputs[firstOutput] != scenario.notes[0].track)
				++firstOutput;
			require(firstOutput < 6, "a scenario's first note has no output");
			const auto firmwareOnset = firstSound(firmware[firstOutput]);
			require(firmwareOnset.has_value(), "the firmware's first Hit stayed silent");

			std::vector<int> blocks(scenario.notes.size());
			for(size_t note = 0; note < blocks.size(); ++note)
				blocks[note] = static_cast<int>((scenario.notes[note].frame + 16) / 32);
			auto engine = engineScenario(_os, kit, index, blocks, count);
			const auto engineOnset = firstSound(engine[firstOutput]);
			require(engineOnset.has_value(), "the engine's first Hit stayed silent");
			require(*engineOnset <= *firmwareOnset, "the engine's first Hit sounds later than the firmware's");
			const auto shift = *firmwareOnset - *engineOnset;

			// The least residual where both agree, among the placements that part on no more blocks than the
			// scenario allows
			const auto maxApart = scenario.apartBlocks[1] * 32;
			const auto better = [&](const Agreement& _a, const Agreement& _b)
			{
				if((_a.apart <= maxApart) != (_b.apart <= maxApart))
					return _a.apart <= maxApart;
				return _a.apart <= maxApart ? _a.ratio() < _b.ratio() : _a.apart < _b.apart;
			};
			for(size_t note = 1; note < blocks.size(); ++note)
			{
				const auto guess = blocks[note];
				auto bestAgreement = scenarioAgreement(scenario, firmware, engine, shift);
				for(int candidate = guess - 6; candidate <= guess + 16; ++candidate)
				{
					if(candidate == guess || candidate < 0)
						continue;
					auto trial = blocks;
					trial[note] = candidate;
					auto outputs = engineScenario(_os, kit, index, trial, count);
					const auto trialAgreement = scenarioAgreement(scenario, firmware, outputs, shift);
					if(better(trialAgreement, bestAgreement))
					{
						bestAgreement = trialAgreement;
						blocks[note] = candidate;
						engine = std::move(outputs);
					}
				}
			}

			// Where one of the two sounds and the other is silent, the firmware's tick timing or its DSP1's lead
			// decides; the rest must match
			const auto fail = [&](const std::string& _what)
			{
				if(failure.empty())
					failure = std::string(scenario.name) + ": " + _what;
			};
			size_t disagreeing = 0;
			std::printf("%-30s", scenario.name);
			for(size_t output = 0; output < 6; ++output)
			{
				const auto track = scenario.outputs[output];
				if(track == g_noTrack)
					continue;
				const auto result = agreement(firmware[output], engine[output], shift);
				const auto residual = result.residual, energy = result.energy, enginePeak = result.enginePeak;
				const auto apart = result.apart;
				disagreeing += apart;
				const auto machine = std::find_if(g_groupTracks.begin(), g_groupTracks.end(),
					[&](const GroupTrack& _t) { return _t.track == track; })->machine;
				if(energy < count * double(g_firmwareFloor) * g_firmwareFloor)
				{
					std::printf("  %c track %2u silent, engine peak %.1e", static_cast<char>('A' + output), track + 1,
						enginePeak);
					if(enginePeak >= 1e-6)
						fail("the engine sounds where the firmware is silent");
					continue;
				}
				const auto db = residual > 0 ? 10.0 * std::log10(residual / energy) : -999.0;
				if(residual > 0)
					std::printf("  %c track %2u %.1f dB", static_cast<char>('A' + output), track + 1, db);
				else
					std::printf("  %c track %2u identical", static_cast<char>('A' + output), track + 1);
				if(apart)
					std::printf(" (%zu apart)", apart);
				const auto floor = scenario.concurrent ? g_concurrentResidualFloorDb : hitResidualFloorDb(machine);
				if(db >= floor)
					fail("an output differs from the firmware's");
			}
			const auto apartBlocks = (disagreeing + 31) / 32;
			if(apartBlocks < scenario.apartBlocks[0] || apartBlocks > scenario.apartBlocks[1])
				fail("the firmware and the engine part on " + std::to_string(disagreeing) + " samples");
			std::printf("  (notes at engine blocks");
			for(const auto block : blocks)
				std::printf(" %d", block);
			std::printf(")\n");

			const auto append32 = [&](const uint32_t _value)
			{
				for(int byte = 0; byte < 4; ++byte)
					dump.push_back(static_cast<uint8_t>(_value >> (8 * byte)));
			};
			append32(static_cast<uint32_t>(index));
			append32(static_cast<uint32_t>(count));
			for(const auto& output : engine)
			{
				std::vector<float> aligned(count, 0.0f);
				std::copy(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(count - shift),
					aligned.begin() + static_cast<std::ptrdiff_t>(shift));
				const auto* bytes = reinterpret_cast<const uint8_t*>(aligned.data());
				dump.insert(dump.end(), bytes, bytes + count * sizeof(float));
			}
		}
		if(const auto* file = std::getenv("MD_ENGINE_GROUPS_DUMP"); file && *file)
		{
			std::ofstream out(file, std::ios::binary);
			out.write(reinterpret_cast<const char*>(dump.data()), static_cast<std::streamsize>(dump.size()));
		}
		require(failure.empty(), failure);
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
	if(_argc < 3)
	{
		std::cerr << "usage: mdEngineFirmwareTest <hits file from mdTrigLatencyFirmwareTest --hits>"
			" <DSP2 dump from mdTrigLatencyFirmwareTest --dsp2, from $147e00>\n";
		return 2;
	}
	try
	{
		const auto flash = readAll(path);
		const auto groupsOs = md::fw::loadFirmwareFromFlash(flash);
		auto os = md::fw::loadFirmwareFromFlash(flash);
		md::engine::Engine engine(os.firmware, std::move(os.osImage));
		const auto bank = md::fw::loadRomBankFromFlash(flash);
		compareRomBank(bank, readAll(_argv[2]));
		engine.loadRomBank(bank);
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
			// Silenced once recorded, as the firmware bench does.
			engine.host().setLevel(hit.track, 0);
			std::printf("%-6s id %3u track %2d: correlation %.6f, gain %.4f (%+.2f dB), residual %.1f dB\n",
				machine->name.c_str(), id, hit.track + 1, result.correlation, result.gain,
				20.0 * std::log10(result.gain), result.residualDb);
			require(result.correlation > 0.999999, "the engine's hit differs from the firmware's");
			require(std::abs(20.0 * std::log10(result.gain)) < 0.01, "the engine's hit is louder or quieter");
			require(result.residualDb < hitResidualFloorDb(id), "the engine's hit leaves a residual above its floor");
		}
		if(_argc > 3)
			compareGroups(flash, groupsOs, _argv[3]);
		std::cout << "mdEngineFirmwareTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdEngineFirmwareTest: " << _error.what() << '\n';
		return 1;
	}
}
