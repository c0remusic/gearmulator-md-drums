// mdDrums::Engine as MD Drums drives it: it finds the flash image, plays a trigger within two 32-sample blocks,
// scales the velocity, plays the UW bank's samples, and renders all 16 tracks playing and the master effects within
// 30 % of real time. It reports the note-to-sound latency and its jitter, sample-accurate (ticket 19) and not, mixes
// and runs the master effects as the engine's own mixer and mixer DSP do, retriggers without a click of its own, and
// plays Links, Chokes and Mute as the Machinedrum does.

#include "mdDrumsEngine.h"

#include "Firmware.h"
#include "MdEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
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

	// Main after a Hit: the master effects' EQ and Dynamix keep a tail of a few LSB, as the Machinedrum's do, so Main is
	// silent below -100 dBFS rather than at 0
	constexpr float g_silence = 1e-5f;

	uint8_t machineId(const mdDrums::Engine& _engine, const char* _name)
	{
		for(const auto& machine : _engine.machines())
			if(machine.name == _name)
				return machine.id;
		throw std::runtime_error("machine missing from the OS's table");
	}

	// Samples from now to the first one at or above -60 dBFS, and the peak, over a second.
	std::pair<int, float> listen(mdDrums::Engine& _engine)
	{
		std::vector<float> left(44100), right(44100);
		_engine.render(left.data(), right.data(), left.size());
		int first = -1;
		float peak = 0;
		for(size_t i = 0; i < left.size(); ++i)
		{
			const auto value = std::max(std::abs(left[i]), std::abs(right[i]));
			if(first < 0 && value >= 0.001f)
				first = static_cast<int>(i);
			peak = std::max(peak, value);
		}
		return {first, peak};
	}

	void settle(mdDrums::Engine& _engine)
	{
		std::vector<float> left(44100 * 2), right(44100 * 2);
		_engine.render(left.data(), right.data(), left.size());
	}

	// Note to sound: 48 notes at pseudo-random samples of the host's blocks (256), each the samples from the note to
	// the first one that is not 0, as the plug-in renders up to a note's offset and triggers there. Returns the jitter.
	int latency(mdDrums::Engine& _engine)
	{
		constexpr size_t hostBlock = 256;
		std::vector<float> left(44100), right(44100);
		uint32_t random = 0x16305eedu;
		int minimum = 1 << 30, maximum = 0;
		double sum = 0;
		for(int note = 0; note < 48; ++note)
		{
			settle(_engine);
			random ^= random << 13; random ^= random >> 17; random ^= random << 5;
			const auto offset = random % hostBlock;
			_engine.render(left.data(), right.data(), offset);
			_engine.trigger(0, 100);
			// Then the host's blocks from the note on: the rest of this one, then whole ones
			for(size_t done = 0, count = hostBlock - offset; done < left.size(); done += count, count = hostBlock)
			{
				count = std::min(count, left.size() - done);
				_engine.render(left.data() + done, right.data() + done, count);
			}
			int first = -1;
			for(size_t i = 0; i < left.size() && first < 0; ++i)
				if(std::abs(left[i]) > g_silence || std::abs(right[i]) > g_silence)
					first = static_cast<int>(i);
			require(first >= 0, "a note stayed silent");
			minimum = std::min(minimum, first);
			maximum = std::max(maximum, first);
			sum += first;
		}
		std::printf("note to sound (TRXBD, 48 notes, %s): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms)\n",
			_engine.isSampleAccurate() ? "sample-accurate" : "on the engine's blocks", minimum, maximum, sum / 48,
			sum / 48 * 1000.0 / 44100.0, maximum - minimum, (maximum - minimum) * 1000.0 / 44100.0);
		// The rest of the engine's block (or the constant 31), then TRX-BD's voice, which sounds a block after its
		// trigger, from its 5th sample, then Dynamix's look-ahead on Main
		require(maximum <= mdDrums::Engine::BlockSize * 2 + 5 + mdDrums::Engine::MasterDelay,
			"a note took more than two blocks to sound");
		return maximum - minimum;
	}

	std::vector<int32_t> toSamples(const std::vector<float>& _values)
	{
		std::vector<int32_t> samples(_values.size());
		for(size_t i = 0; i < _values.size(); ++i)
			samples[i] = static_cast<int32_t>(std::lround(_values[i] * 8388608.0f));
		return samples;
	}

	// The engine's own mixer (md::engine::Engine, as mdEngineFirmwareTest compares it with the firmware) against
	// mdDrums::Engine with every Hit on an engine block and every Track on a machine whose voice is held back as long
	// (the default ones, TRX-SD for those that start later), so that every Track waits as long: the same samples, at
	// once when not sample-accurate, SampleAccurateDelay and the hold later when it is. The master effects run on the engine's buses as
	// they are, and on the same buses SampleAccurateDelay later, each with the OS's words of every tick. The two
	// mdDrums::Engines come from the flash image, their machines and master compiled ahead, against an engine and
	// master effects that were not: the warm-up leaves the samples as they are.
	void equivalence(const std::vector<uint8_t>& _flash, const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto machine = [](const int _track)
		{
			const auto m = mdDrums::Engine::defaultMachine(_track);
			return mdDrums::Engine::voiceStart(m) == mdDrums::Engine::voiceStart(17) ? m : uint8_t{17};	// TRX-SD
		};
		md::engine::Engine raw(_os.firmware, _os.osImage);
		raw.loadRomBank(_bank);
		md::engine::MasterEngine onBlocks(_os.firmware), late(_os.firmware);
		for(int track = 0; track < mdDrums::Engine::TrackCount; ++track)
		{
			raw.host().setMachine(track, machine(track));
			for(int param = 8; param < mdDrums::Engine::ParamCount; ++param)
				raw.host().setParam(track, param, mdDrums::Engine::paramDefaults()[param]);
			raw.host().setLevel(track, mdDrums::Engine::DefaultLevel);
		}
		for(int index = 0; index < mdDrums::Engine::MasterCount; ++index)
			raw.host().setMaster(index, mdDrums::Engine::masterDefaults()[index]);
		mdDrums::Engine accurate(_flash), blocks(_flash);
		blocks.setSampleAccurate(false);
		for(int track = 0; track < mdDrums::Engine::TrackCount; ++track)
		{
			accurate.setMachine(track, machine(track));
			blocks.setMachine(track, machine(track));
		}
		accurate.setParam(4, 18, 20);	// a panned Track, and one with a send
		blocks.setParam(4, 18, 20);
		raw.host().setParam(4, 18, 20);
		accurate.setParam(7, 19, 90);
		blocks.setParam(7, 19, 90);
		raw.host().setParam(7, 19, 90);

		constexpr int blockCount = 600;
		constexpr size_t block = mdDrums::Engine::BlockSize;
		const auto shift = static_cast<size_t>(mdDrums::Engine::SampleAccurateDelay + mdDrums::Engine::AlignedStart
			- mdDrums::Engine::voiceStart(17));
		std::vector<int32_t> rawLeft, rawRight, lateLeft, lateRight;
		std::vector<float> accLeft(blockCount * block), accRight(blockCount * block), blkLeft(accLeft.size()), blkRight(accLeft.size());
		md::engine::Engine::Output out;
		std::vector<std::array<std::array<int32_t, 2>, 3>> buses(shift);	// the buses' last frames, shift of them, oldest first
		for(int b = 0; b < blockCount; ++b)
		{
			if(b % 3 == 0)
			{
				const auto track = (b / 3) % mdDrums::Engine::TrackCount;
				const auto velocity = 40 + (b * 7) % 88;
				raw.host().trigger(track, velocity);
				accurate.trigger(track, velocity);
				blocks.trigger(track, velocity);
			}
			require(raw.render(out), "the engine faulted");
			md::engine::HostModel<>::MasterWords words;
			if(raw.host().takeMasterWords(words))
			{
				onBlocks.setWords(words.y, words.words.data(), words.count);
				late.setWords(words.y, words.words.data(), words.count);
			}
			md::engine::Mixer::Stereo main{}, lateDry{}, lateRev{}, lateDel{};
			require(onBlocks.process(out.mix.main, out.mix.rev, out.mix.del, main), "the master effects faulted");
			for(size_t i = 0; i < block; ++i)
			{
				buses.push_back({out.mix.main[i], out.mix.rev[i], out.mix.del[i]});
				lateDry[i] = buses.front()[0];
				lateRev[i] = buses.front()[1];
				lateDel[i] = buses.front()[2];
				buses.erase(buses.begin());
			}
			md::engine::Mixer::Stereo lateMain{};
			require(late.process(lateDry, lateRev, lateDel, lateMain), "the master effects faulted");
			for(size_t i = 0; i < block; ++i)
			{
				rawLeft.push_back(main[i][0]);
				rawRight.push_back(main[i][1]);
				lateLeft.push_back(lateMain[i][0]);
				lateRight.push_back(lateMain[i][1]);
			}
			accurate.render(accLeft.data() + b * block, accRight.data() + b * block, block);
			blocks.render(blkLeft.data() + b * block, blkRight.data() + b * block, block);
		}
		const auto al = toSamples(accLeft), ar = toSamples(accRight), bl = toSamples(blkLeft), br = toSamples(blkRight);
		size_t blockDiffs = 0, accurateDiffs = 0;
		for(size_t i = 0; i < rawLeft.size(); ++i)
		{
			blockDiffs += (bl[i] != rawLeft[i]) + (br[i] != rawRight[i]);
			accurateDiffs += (al[i] != lateLeft[i]) + (ar[i] != lateRight[i]);
		}
		std::printf("Main against the engine's own mixer and master effects, 200 Hits on its blocks, %zu samples: %zu differ"
			" on the engine's blocks, %zu sample-accurate (buses %zu samples later)\n", rawLeft.size(), blockDiffs,
			accurateDiffs, shift);
		require(blockDiffs == 0 && accurateDiffs == 0, "the delayed mix is not the engine's mix");
	}

	// The tables Engine::machineDefaults copies, as OS 1.63 holds them (its ASSIGN MACHINE handler, $2052fe)
	void osTables(const md::fw::FlashOs& _os)
	{
		using Engine = mdDrums::Engine;
		constexpr uint32_t base = 0x200000;	// the OS image's first address
		const auto same = [&](const uint32_t _address, const auto& _table, const char* _name)
		{
			require(_address >= base && _address - base + _table.size() <= _os.osImage.size(),
				(std::string(_name) + " lies past the OS image").c_str());
			const auto* os = _os.osImage.data() + (_address - base);
			require(std::equal(_table.begin(), _table.end(), os), (std::string(_name) + " is not the OS's").c_str());
		};
		same(Engine::OsEffectsAddress, Engine::OsEffects, "OsEffects");
		same(Engine::OsEffectsAddress + 8, Engine::OsRouting, "OsRouting");
		same(Engine::OsMidEffectsAddress, Engine::OsMidEffects, "OsMidEffects");
		same(Engine::OsMidEffectsAddress + 8, Engine::OsMidRouting, "OsMidRouting");
		same(Engine::OsCtrAlAddress, Engine::OsCtrAl, "OsCtrAl");
		same(Engine::Os8pAddress, Engine::Os8pEffects, "Os8pEffects");
		same(Engine::Os8pAddress + 8, Engine::Os8pRouting, "Os8pRouting");
		same(Engine::Os8pAddress + 16, Engine::Os8pTargets, "Os8pTargets");
		same(Engine::OsCtrBlocksAddress, Engine::OsCtrBlocks, "OsCtrBlocks");
		std::printf("ASSIGN MACHINE's tables are OS 1.63's\n");
	}

	// Where the voices start (ticket 36): every machine that sounds, at velocity 127 with the Kit's defaults, on its own
	// output (it does not go through the master effects), its first Hit after it is chosen and a Hit again once it is
	// silent. It prints what each voice takes after the block its Hit lands in, its Hit again's (Engine::voiceStart's
	// table). A Hit again of the machines it holds back sounds HitDelay samples after its trigger: exactly for the
	// voices that start a block late and for ROM, within 4 samples for the others, whose own sound sets their first
	// sample; TRX-XT, TRX-XC and TRX-CB, not held back, sound their own start later.
	void voiceStarts(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		constexpr size_t length = 2048;
		std::vector<float> own(length);
		std::array<float*, mdDrums::Engine::OutputCount> outputs{};
		outputs[2] = own.data();
		// The first sample that is not 0 on Track 1's own output, from a trigger now; -1 when silent
		const auto hit = [&](mdDrums::Engine& _engine)
		{
			_engine.trigger(0, 127);
			_engine.render(outputs.data(), length);
			for(size_t i = 0; i < length; ++i)
				if(own[i] != 0.0f)
					return static_cast<int>(i);
			return -1;
		};
		// Renders until the Track's output has been silent for a while (at most 4 s); false if it never is
		const auto quiet = [&](mdDrums::Engine& _engine)
		{
			size_t silent = 0;
			for(size_t done = 0; done < 44100 * 4; done += length)
			{
				_engine.render(outputs.data(), length);
				for(size_t i = 0; i < length; ++i)
					silent = own[i] == 0.0f ? silent + 1 : 0;
				if(silent >= 8192)
					return true;
			}
			return false;
		};
		// Where a Hit of _machine is expected to sound on its own output, its voice starting _start after its block
		const auto expected = [](const uint8_t _machine, const int _start)
		{
			const auto held = std::max(0, mdDrums::Engine::AlignedStart - mdDrums::Engine::voiceStart(_machine));
			return mdDrums::Engine::SampleAccurateDelay + held + _start + mdDrums::Engine::MasterDelay;
		};

		std::string silentNames, starts, wrong;
		int sounding = 0;
		const mdDrums::Engine machines(_os, _bank, false);
		for(const auto& machine : machines.machines())
		{
			mdDrums::Engine engine(_os, _bank, false);
			engine.setMachine(0, machine.id);
			engine.render(outputs.data(), mdDrums::Engine::BlockSize * 3);
			const auto first = hit(engine);
			if(first < 0)
			{
				silentNames += " " + machine.name;
				continue;
			}
			++sounding;
			const auto again = quiet(engine) ? hit(engine) : -1;
			// What the voice takes after its block: the sound less the 31, the hold and the look-ahead
			const auto start = [&](const int _at) { return _at < 0 ? -1 : _at - expected(machine.id, 0); };
			starts += " " + machine.name + " " + std::to_string(start(first)) + "/" + std::to_string(start(again));

			// The references: a machine of each family, the voices that start a block late, ROM, and the two not held
			const std::string name = machine.name;
			const bool exact = name == "TRXBD" || name == "TRXS2" || name == "ROM01" || name == "ROM16";
			const bool near = name == "TRXSD" || name == "TRXCH" || name == "EFMBD" || name == "EFMSD" || name == "E12BD"
				|| name == "E12SD" || name == "P-IBD" || name == "GNDSN";
			const bool late = name == "TRXXT" || name == "TRXXC" || name == "TRXCB";
			if(!exact && !near && !late)
				continue;
			const auto want = late ? expected(machine.id, mdDrums::Engine::voiceStart(machine.id))
				: mdDrums::Engine::HitDelay;
			const auto tolerance = exact ? 0 : near ? 4 : 1;
			// A sound that never goes silent (GND-SN, a sine) gives its first Hit
			const auto at = again >= 0 ? again : first;
			if(at < want || at > want + tolerance)
				wrong += " " + name + " (id " + std::to_string(machine.id) + ") at " + std::to_string(at)
					+ ", not " + std::to_string(want) + (tolerance ? " to " + std::to_string(want + tolerance) : "") + ";";
		}
		std::printf("voice starts on their own output, after the block a Hit lands in (first Hit/again):%s\n", starts.c_str());
		std::printf("voice starts: %d machines sound, silent:%s; held back, a Hit again sounds %d samples after its trigger\n",
			sounding, silentNames.c_str(), mdDrums::Engine::HitDelay);
		if(!wrong.empty())
			std::printf("voice starts elsewhere:%s\n", wrong.c_str());
		require(sounding > 60 && wrong.empty(), "a machine's Hit does not sound where its voice's start says");
	}

	// Where a Choke cuts, sample-accurate (ticket 38 of the editor map): its target, a sustained E12-RC on its own output,
	// sounds as it would without the Choke up to where the choking Hit sounds, 31 samples after that Hit's note, and is
	// silent from there, whether it waits more or less than that Hit; through a Link's own Choke too, and after a Choke
	// onto a Track already choked. A lower target goes silent at the engine's next tick instead, as the Machinedrum orders
	// its tick: later, at its own delay, and without a gap before. Each case against the same engine with its Chokes off.
	void chokes(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		struct Case
		{
			const char* name;
			int target;
			std::vector<int> chokers;					// the Tracks whose Choke is the target
			int linkFrom;								// a Track that Links to linkTo, or -1
			int linkTo;
			std::vector<std::pair<size_t, int>> hits;	// a note's host sample and Track; the last one cuts
			size_t from;								// where the comparison starts
			bool lower;									// the target is below the Track that cuts it
		};
		constexpr size_t block = mdDrums::Engine::BlockSize;
		// A note at sample _offset of host block _block: its Hit waits _offset - 1 samples (31 at offset 0)
		const auto at = [](const size_t _block, const size_t _offset) { return _block * block + _offset; };
		const std::vector<Case> cases{
			{"a higher target waiting longer than the Hit", 5, {2}, -1, -1, {{at(138, 30), 5}, {at(300, 3), 2}}, at(138, 30), false},
			{"a higher target waiting less than the Hit", 5, {2}, -1, -1, {{at(138, 3), 5}, {at(300, 30), 2}}, at(138, 3), false},
			{"a lower target", 2, {5}, -1, -1, {{at(138, 30), 2}, {at(300, 3), 5}}, at(138, 30), true},
			{"a Link's Choke", 6, {3}, 1, 3, {{at(138, 30), 6}, {at(300, 3), 1}}, at(138, 30), false},
			{"a Choke after one onto a Track already choked", 5, {2, 3}, 1, 3,
				{{at(138, 30), 5}, {at(300, 30), 2}, {at(400, 30), 2}, {at(500, 30), 5}, {at(800, 3), 1}}, at(600, 0), false},
		};

		const auto run = [&](const Case& _case, const bool _choke)
		{
			mdDrums::Engine engine(_os, _bank, true);
			engine.setMachine(_case.target, machineId(engine, "E12RC"));
			engine.setParam(_case.target, 1, 127);
			engine.setParam(_case.target, 2, 127);
			engine.setSeparateOutputs(1u << _case.target);
			if(_case.linkFrom >= 0)
				engine.setLink(_case.linkFrom, _case.linkTo);
			if(_choke)
				for(const auto track : _case.chokers)
					engine.setChoke(track, _case.target);
			std::vector<float> own(_case.hits.back().first + 4410);
			size_t rendered = 0;
			const auto renderTo = [&](const size_t _at)
			{
				std::array<float*, mdDrums::Engine::OutputCount> outputs{};
				outputs[2 + _case.target] = own.data() + rendered;
				engine.render(outputs.data(), _at - rendered);
				rendered = _at;
			};
			for(const auto& [note, track] : _case.hits)
			{
				renderTo(note);
				engine.trigger(track, 100);
			}
			renderTo(own.size());
			return own;
		};

		for(const auto& c : cases)
		{
			const auto on = run(c, true), off = run(c, false);
			// The cutting Hit sounds 31 samples after its note, held back as its machine's voice is quicker than the
			// latest, on the target's own output Dynamix's look-ahead later (as on Main)
			const auto last = c.hits.back().second;
			const auto cutter = last == c.linkFrom ? c.linkTo : last;
			const auto cut = c.hits.back().first + mdDrums::Engine::SampleAccurateDelay + mdDrums::Engine::MasterDelay
				+ std::max(0, mdDrums::Engine::AlignedStart
					- mdDrums::Engine::voiceStart(mdDrums::Engine::defaultMachine(cutter)));
			// Where the target goes silent for good, and what differs before
			auto silent = on.size();
			while(silent > c.from && on[silent - 1] == 0.0f)
				--silent;
			size_t differ = 0;
			for(auto i = c.from; i < silent; ++i)
				differ += on[i] != off[i];
			float sounding = 0;
			for(auto i = cut; i < cut + 256; ++i)
				sounding = std::max(sounding, std::abs(off[i]));
			std::printf("Choke, %s: silent %+d samples from where the cutting Hit sounds, %zu samples differ before\n",
				c.name, static_cast<int>(silent) - static_cast<int>(cut), differ);
			require(sounding > 0.001f, "the target was already silent where the Choke cuts it");
			require(differ == 0, "a Choke changed its target before it cut it");
			require(c.lower ? silent > cut : silent == cut, "a Choke did not cut its target where the Hit sounds");
		}
	}

	// A sustained sound (E12-RC, HOLD and DEC at 127) retriggered at each of the 32 places of a block: the delays change
	// at the Hit, which drops or doubles a few samples of the old tail. Threshold set before measuring: within 128
	// samples from the new Hit's trigger, no sample step steeper than 1.5 times the steepest one of the same retrigger
	// on the engine's blocks.
	void retrigger(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank)
	{
		const auto steepest = [&](const bool _accurate, const int _place)
		{
			mdDrums::Engine engine(_os, _bank, true);
			engine.setSampleAccurate(_accurate);
			engine.setMachine(0, machineId(engine, "E12RC"));
			engine.setParam(0, 1, 127);
			engine.setParam(0, 2, 127);
			std::vector<float> left(44100), right(44100);
			size_t rendered = 0;
			const auto renderTo = [&](const size_t _count)
			{
				engine.render(left.data(), right.data(), _count);
				rendered += _count;
			};
			renderTo(4416);
			renderTo(7);
			engine.trigger(0, 127);
			renderTo(4410 - 7);
			renderTo((_place - rendered % 32 + 64) % 32);
			engine.trigger(0, 127);
			engine.render(left.data(), right.data(), 128);
			float step = 0;
			for(size_t i = 1; i < 128; ++i)
				step = std::max(step, std::abs(left[i] - left[i - 1]));
			return step;
		};
		float worstRatio = 0;
		int worstPlace = 0;
		for(int place = 0; place < 32; ++place)
		{
			const auto ratio = steepest(true, place) / std::max(1e-9f, steepest(false, place));
			if(ratio > worstRatio)
			{
				worstRatio = ratio;
				worstPlace = place;
			}
		}
		std::printf("retrigger of E12-RC at the 32 places of a block: steepest step at most %.3f times the one on the"
			" engine's blocks (place %d)\n", worstRatio, worstPlace);
		require(worstRatio <= 1.5f, "a retrigger clicks with the sample-accurate delays");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsEngineTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		mdDrums::Engine engine(flash);
		require(engine.machines().size() > 100, "the OS's machine table is incomplete");

		// The scenarios on engines of their own build them from the OS and the UW bank read once, without the
		// machines and the master compiled ahead, which leaves the samples as they are (mdDrumsFirstHitTest)
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		const auto bank = md::fw::loadRomBankFromFlash(flash);
		osTables(os);
		// First: the checks below take where the Hits sound from it
		voiceStarts(os, bank);

		engine.setMachine(0, machineId(engine, "TRXBD"));
		engine.setLevel(0, 100);
		settle(engine);
		require(listen(engine).second == 0.0f, "a track sounds without a trigger");

		// A trigger sounds HitDelay samples later on Main, as every machine's (TRX-BD's voice sounds a block after its
		// trigger, the latest), and reaches -60 dBFS within a block
		engine.trigger(0, 127);
		const auto [first, loud] = listen(engine);
		std::printf("TRXBD velocity 127: first audible sample %d, peak %.4f\n", first, loud);
		require(first >= mdDrums::Engine::HitDelay && first < mdDrums::Engine::HitDelay + mdDrums::Engine::BlockSize,
			"the trigger does not sound HitDelay samples later on Main");
		require(loud > 0.01f, "the trigger is too quiet");

		settle(engine);
		engine.trigger(0, 40);
		const auto soft = listen(engine).second;
		std::printf("TRXBD velocity 40: peak %.4f\n", soft);
		require(soft < loud * 0.8f && soft > 0.0f, "the velocity does not scale the level");

		// On engines of their own: 48 Hits leave their track's voice in another state than the tests below expect
		for(const bool accurate : {false, true})
		{
			mdDrums::Engine fresh(os, bank, true);
			fresh.setSampleAccurate(accurate);
			fresh.setMachine(0, machineId(fresh, "TRXBD"));
			fresh.setLevel(0, 100);
			const auto jitter = latency(fresh);
			require(!accurate || jitter == 0, "sample-accurate Hits jitter");
		}
		equivalence(flash, os, bank);
		retrigger(os, bank);
		chokes(os, bank);

		// Track 1 on its own output: it leaves the main mix and sounds there alone.
		{
			settle(engine);
			engine.setSeparateOutputs(1);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(44100));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			engine.trigger(0, 127);
			engine.render(outputs.data(), 44100);
			const auto peak = [&](const int _output)
			{
				float result = 0;
				for(const auto value : buffers[_output])
					result = std::max(result, std::abs(value));
				return result;
			};
			std::printf("track 1 separated: main peak %.4f, own output %.4f, track 2 output %.4f\n",
				std::max(peak(0), peak(1)), peak(2), peak(3));
			require(peak(0) < g_silence && peak(1) < g_silence, "a separated track still plays in the main mix");
			require(peak(2) > 0.01f, "a separated track is silent on its own output");
			require(peak(3) == 0.0f, "another track's output carries track 1");
			engine.setSeparateOutputs(0);
		}

		// Links, Chokes and Mute, on tracks 2 to 6, each on its own output (mdEngineFirmwareTest compares them with
		// the firmware's)
		{
			for(int track = 1; track < 6; ++track)
			{
				engine.setMachine(track, machineId(engine, "EFMCB"));
				engine.setLevel(track, 100);
			}
			engine.setSeparateOutputs(0x3e);
			settle(engine);
			std::vector<std::vector<float>> buffers(mdDrums::Engine::OutputCount, std::vector<float>(4410));
			std::array<float*, mdDrums::Engine::OutputCount> outputs{};
			for(int i = 0; i < mdDrums::Engine::OutputCount; ++i)
				outputs[i] = buffers[i].data();
			const auto peak = [&](const int _track, const size_t _from = 0)
			{
				float result = 0;
				for(size_t i = _from; i < buffers[2 + _track].size(); ++i)
					result = std::max(result, std::abs(buffers[2 + _track][i]));
				return result;
			};

			// A Link: track 2 also hits track 3, whose own Link to track 4 does not fire
			engine.setLink(1, 2);
			engine.setLink(2, 3);
			engine.trigger(1, 100);
			engine.render(outputs.data(), 4410);
			std::printf("Link 2 to 3: track 2 %.4f, track 3 %.4f, track 4 %.4f\n", peak(1), peak(2), peak(3));
			require(peak(1) > 0.01f && std::abs(peak(2) - peak(1)) < 1e-6f, "a Link did not hit its target as hard");
			require(peak(3) == 0.0f, "a Link went two steps");
			engine.setLink(1, mdDrums::Engine::NoTrack);
			engine.setLink(2, mdDrums::Engine::NoTrack);

			// A Choke: track 4 sounding, track 3's Hit silences it where it sounds (HitDelay after its trigger) and until
			// its own next Hit
			settle(engine);
			engine.setChoke(2, 3);
			engine.trigger(3, 100);
			engine.render(outputs.data(), 4410);
			const auto before = peak(3);
			engine.trigger(2, 100);
			engine.render(outputs.data(), 4410);
			std::printf("Choke 3 to 4: track 4 %.4f before, %.4f after where the Hit sounds\n", before,
				peak(3, mdDrums::Engine::HitDelay));
			require(before > 0.01f && peak(3, mdDrums::Engine::HitDelay) == 0.0f, "a Choke did not silence its target");
			engine.trigger(3, 100);
			engine.render(outputs.data(), 4410);
			require(peak(3) > 0.01f, "a choked track stayed silent after its own Hit");
			engine.setChoke(2, mdDrums::Engine::NoTrack);

			// Mute: track 6 drops its Hits, firing neither its Link nor its Choke
			settle(engine);
			engine.trigger(1, 100);
			engine.render(outputs.data(), 441);
			engine.setMute(5, true);
			engine.setLink(5, 4);
			engine.setChoke(5, 1);
			engine.trigger(5, 100);
			engine.render(outputs.data(), 4410);
			std::printf("muted track 6 struck: track 6 %.4f, its Link %.4f, its Choke %.4f\n", peak(5), peak(4), peak(1));
			require(peak(5) == 0.0f && peak(4) == 0.0f, "a muted track's Hit sounded");
			require(peak(1) > 0.01f, "a muted track's Choke fired");
			engine.setMute(5, false);
			engine.setLink(5, mdDrums::Engine::NoTrack);
			engine.setChoke(5, mdDrums::Engine::NoTrack);
			for(int track = 1; track < 6; ++track)
				engine.setLevel(track, 0);
			engine.setSeparateOutputs(0);
		}

		// The ROM machines play the UW bank, which fills ROM-01 to -32: the whole sample once (PTCH 64, DEC and HOLD
		// 127, BRR 0, STRT 0, END 127, no retrig). ROM-33 holds no sample and stays silent.
		{
			const int whole[8] = {64, 127, 127, 0, 0, 127, 0, 0};
			for(const char* name : {"ROM01", "ROM32", "ROM33"})
			{
				engine.setMachine(0, machineId(engine, name));
				for(int param = 0; param < 8; ++param)
					engine.setParam(0, param, whole[param]);
				settle(engine);
				engine.trigger(0, 127);
				const auto peak = listen(engine).second;
				std::printf("%s velocity 127: peak %.4f\n", name, peak);
				if(std::string(name) == "ROM33")
					require(peak < g_silence, "an empty ROM slot sounds");
				else
					require(peak > 0.01f, "a ROM machine of the factory bank is silent");
			}
		}

		// All 16 tracks, each on a TRX machine, triggered every 64th of a second for 8 seconds.
		const char* kit[16] = {"TRXBD", "TRXSD", "TRXXT", "TRXCP", "TRXRS", "TRXCB", "TRXCH", "TRXOH",
			"TRXCY", "TRXMA", "TRXCL", "TRXXC", "TRXB2", "TRXS2", "EFMBD", "EFMSD"};
		for(int track = 0; track < 16; ++track)
		{
			engine.setMachine(track, machineId(engine, kit[track]));
			engine.setLevel(track, 100);
		}
		std::vector<float> left(689), right(689);
		for(const bool accurate : {false, true})
		{
			engine.setSampleAccurate(accurate);
			const auto start = std::chrono::steady_clock::now();
			for(int step = 0; step < 512; ++step)
			{
				engine.trigger(step % 16, 100);
				engine.render(left.data(), right.data(), left.size());
			}
			const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			const double audio = 512.0 * 689.0 / 44100.0;
			std::printf("16 tracks and the master effects%s: %.2f s of audio in %.2f s (%.1f%% of one core)\n",
				accurate ? ", sample-accurate" : "", audio, seconds, seconds / audio * 100.0);
			// The audio thread's budget (ticket 09): what MD Drums renders, at most 30 % of real time on the reference PC
			require(seconds < audio * 0.3, "the engine takes more than 30 % of real time for 16 tracks and the master");
		}

		std::cout << "mdDrumsEngineTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsEngineTest: " << _error.what() << '\n';
		return 1;
	}
}
