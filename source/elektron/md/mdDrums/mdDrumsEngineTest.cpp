// mdDrums::Engine as MD Drums drives it: it finds the flash image, plays a trigger within one 32-sample block,
// scales the velocity, plays the UW bank's samples, and renders all 16 tracks playing faster than real time.

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
