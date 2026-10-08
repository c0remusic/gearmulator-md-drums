// mdDrums::Engine as MD Drums drives it: it finds the flash image, plays a trigger within one 32-sample block,
// scales the velocity, plays the UW bank's samples, and renders all 16 tracks playing faster than real time. It
// reports the note-to-sound latency and its jitter, and plays Links, Chokes and Mute as the Machinedrum does.

#include "mdDrumsEngine.h"

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
	// the first one that is not 0, as the plug-in renders up to a note's offset and triggers there.
	void latency(mdDrums::Engine& _engine)
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
		std::printf("note to sound (TRXBD, 48 notes): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms)\n",
			minimum, maximum, sum / 48, sum / 48 * 1000.0 / 44100.0, maximum - minimum,
			(maximum - minimum) * 1000.0 / 44100.0);
		// The rest of the engine's block, then TRX-BD's voice, which sounds a block after its trigger, from its 5th sample
		require(maximum <= mdDrums::Engine::BlockSize * 2 + 5, "a note took more than two blocks to sound");
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

		// A trigger between two renders lands on the next 32-sample block.
		engine.trigger(0, 127);
		const auto [first, loud] = listen(engine);
		std::printf("TRXBD velocity 127: first audible sample %d, peak %.4f\n", first, loud);
		require(first >= 0 && first < mdDrums::Engine::BlockSize * 2, "the trigger took more than two blocks to sound");
		require(loud > 0.01f, "the trigger is too quiet");

		settle(engine);
		engine.trigger(0, 40);
		const auto soft = listen(engine).second;
		std::printf("TRXBD velocity 40: peak %.4f\n", soft);
		require(soft < loud * 0.8f && soft > 0.0f, "the velocity does not scale the level");

		// On an engine of its own: 48 Hits leave their track's voice in another state than the tests below expect
		{
			mdDrums::Engine fresh(flash);
			fresh.setMachine(0, machineId(fresh, "TRXBD"));
			fresh.setLevel(0, 100);
			latency(fresh);
		}

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
		const auto start = std::chrono::steady_clock::now();
		for(int step = 0; step < 512; ++step)
		{
			engine.trigger(step % 16, 100);
			engine.render(left.data(), right.data(), left.size());
		}
		const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		const double audio = 512.0 * 689.0 / 44100.0;
		std::printf("16 tracks: %.2f s of audio in %.2f s (%.0f%% of one core)\n", audio, seconds, seconds / audio * 100.0);
		require(seconds < audio, "the engine renders 16 tracks slower than real time");

		std::cout << "mdDrumsEngineTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsEngineTest: " << _error.what() << '\n';
		return 1;
	}
}
