// mdDrums::Engine as MD Drums drives it: it finds the flash image, plays a trigger within two 32-sample blocks,
// scales the velocity, plays the UW bank's samples, and renders all 16 tracks playing faster than real time. It
// reports the note-to-sound latency and its jitter, sample-accurate (ticket 19) and not, mixes as the engine's own
// mixer does, retriggers without a click of its own, and plays Links, Chokes and Mute as the Machinedrum does.

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
				if(left[i] != 0.0f || right[i] != 0.0f)
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
		// trigger, from its 5th sample
		require(maximum <= mdDrums::Engine::BlockSize * 2 + 5, "a note took more than two blocks to sound");
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
	// mdDrums::Engine with every Hit on an engine block, so that every Track waits as long: the same samples, at once
	// when not sample-accurate, SampleAccurateDelay later when it is
	void equivalence(const std::vector<uint8_t>& _flash)
	{
		auto os = md::fw::loadFirmwareFromFlash(_flash);
		md::engine::Engine raw(os.firmware, std::move(os.osImage));
		raw.loadRomBank(md::fw::loadRomBankFromFlash(_flash));
		for(int track = 0; track < mdDrums::Engine::TrackCount; ++track)
		{
			raw.host().setMachine(track, mdDrums::Engine::defaultMachine(track));
			for(int param = 8; param < mdDrums::Engine::ParamCount; ++param)
				raw.host().setParam(track, param, mdDrums::Engine::paramDefaults()[param]);
			raw.host().setLevel(track, mdDrums::Engine::DefaultLevel);
		}
		mdDrums::Engine accurate(_flash), blocks(_flash);
		blocks.setSampleAccurate(false);
		accurate.setParam(4, 18, 20);	// a panned Track, and one with a send
		blocks.setParam(4, 18, 20);
		raw.host().setParam(4, 18, 20);
		accurate.setParam(7, 19, 90);
		blocks.setParam(7, 19, 90);
		raw.host().setParam(7, 19, 90);

		constexpr int blockCount = 600;
		constexpr size_t block = mdDrums::Engine::BlockSize;
		std::vector<int32_t> rawLeft, rawRight;
		std::vector<float> accLeft(blockCount * block), accRight(blockCount * block), blkLeft(accLeft.size()), blkRight(accLeft.size());
		md::engine::Engine::Output out;
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
			for(const auto& frame : out.mix.main)
			{
				rawLeft.push_back(frame[0]);
				rawRight.push_back(frame[1]);
			}
			accurate.render(accLeft.data() + b * block, accRight.data() + b * block, block);
			blocks.render(blkLeft.data() + b * block, blkRight.data() + b * block, block);
		}
		const auto al = toSamples(accLeft), ar = toSamples(accRight), bl = toSamples(blkLeft), br = toSamples(blkRight);
		size_t blockDiffs = 0, accurateDiffs = 0;
		constexpr size_t shift = mdDrums::Engine::SampleAccurateDelay;
		for(size_t i = 0; i < rawLeft.size(); ++i)
		{
			blockDiffs += (bl[i] != rawLeft[i]) + (br[i] != rawRight[i]);
			if(i + shift < al.size())
				accurateDiffs += (al[i + shift] != rawLeft[i]) + (ar[i + shift] != rawRight[i]);
		}
		std::printf("mix against the engine's own mixer, 200 Hits on its blocks, %zu samples: %zu differ on the engine's"
			" blocks, %zu sample-accurate (%zu samples later)\n", rawLeft.size(), blockDiffs, accurateDiffs, shift);
		require(blockDiffs == 0 && accurateDiffs == 0, "the delayed mix is not the engine's mix");
	}

	// Where the voices start, sample-accurate: from the trigger to the first sample that is not 0, a machine of each
	// family. Every Hit waits 31 samples; what remains is the voice's own start, which the Device's reported latency
	// takes from the most common case.
	void voiceStarts(const std::vector<uint8_t>& _flash)
	{
		std::string line;
		for(const char* name : {"TRXBD", "TRXSD", "TRXCH", "TRXCP", "EFMBD", "EFMSD", "EFMHH", "E12BD", "E12SD", "E12CH",
			"P-IBD", "P-IHH", "ROM01", "GNDSN"})
		{
			mdDrums::Engine engine(_flash);
			engine.setMachine(0, machineId(engine, name));
			settle(engine);
			engine.trigger(0, 127);
			std::vector<float> left(4410), right(4410);
			engine.render(left.data(), right.data(), left.size());
			int first = -1;
			for(size_t i = 0; i < left.size() && first < 0; ++i)
				if(left[i] != 0.0f || right[i] != 0.0f)
					first = static_cast<int>(i);
			line += std::string(" ") + name + " " + std::to_string(first - mdDrums::Engine::SampleAccurateDelay);
		}
		std::printf("voice starts after the 31 samples:%s\n", line.c_str());
	}

	// A sustained sound (E12-RC, HOLD and DEC at 127) retriggered at each of the 32 places of a block: the delays change
	// at the Hit, which drops or doubles a few samples of the old tail. Threshold set before measuring: within 128
	// samples from the new Hit's trigger, no sample step steeper than 1.5 times the steepest one of the same retrigger
	// on the engine's blocks.
	void retrigger(const std::vector<uint8_t>& _flash)
	{
		const auto steepest = [&](const bool _accurate, const int _place)
		{
			mdDrums::Engine engine(_flash);
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

		engine.setMachine(0, machineId(engine, "TRXBD"));
		engine.setLevel(0, 100);
		settle(engine);
		require(listen(engine).second == 0.0f, "a track sounds without a trigger");

		// A trigger sounds 31 samples later, then TRX-BD's voice a block after it
		engine.trigger(0, 127);
		const auto [first, loud] = listen(engine);
		std::printf("TRXBD velocity 127: first audible sample %d, peak %.4f\n", first, loud);
		require(first >= 0 && first < mdDrums::Engine::BlockSize * 2 + mdDrums::Engine::SampleAccurateDelay,
			"the trigger took more than two blocks to sound");
		require(loud > 0.01f, "the trigger is too quiet");

		settle(engine);
		engine.trigger(0, 40);
		const auto soft = listen(engine).second;
		std::printf("TRXBD velocity 40: peak %.4f\n", soft);
		require(soft < loud * 0.8f && soft > 0.0f, "the velocity does not scale the level");

		// On engines of their own: 48 Hits leave their track's voice in another state than the tests below expect
		for(const bool accurate : {false, true})
		{
			mdDrums::Engine fresh(flash);
			fresh.setSampleAccurate(accurate);
			fresh.setMachine(0, machineId(fresh, "TRXBD"));
			fresh.setLevel(0, 100);
			const auto jitter = latency(fresh);
			require(!accurate || jitter == 0, "sample-accurate Hits jitter");
		}
		equivalence(flash);
		retrigger(flash);
		voiceStarts(flash);

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
			require(peak(0) == 0.0f && peak(1) == 0.0f, "a separated track still plays in the main mix");
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

			// A Choke: track 4 sounding, track 3's Hit silences it within the Hit's block and until its own next Hit
			settle(engine);
			engine.setChoke(2, 3);
			engine.trigger(3, 100);
			engine.render(outputs.data(), 4410);
			const auto before = peak(3);
			engine.trigger(2, 100);
			engine.render(outputs.data(), 4410);
			std::printf("Choke 3 to 4: track 4 %.4f before, %.4f after the first block\n", before,
				peak(3, mdDrums::Engine::BlockSize));
			require(before > 0.01f && peak(3, mdDrums::Engine::BlockSize) == 0.0f, "a Choke did not silence its target");
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
					require(peak == 0.0f, "an empty ROM slot sounds");
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
			std::printf("16 tracks%s: %.2f s of audio in %.2f s (%.0f%% of one core)\n", accurate ? ", sample-accurate" : "",
				audio, seconds, seconds / audio * 100.0);
			require(seconds < audio, "the engine renders 16 tracks slower than real time");
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
