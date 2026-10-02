#include "mdLib/mdasyncrender.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

// AsyncRender control-access contract: a control thread must be able to
// pause a device that renders slower than real time. The former contract
// waited for an idle device, which never comes while the host hands over a
// block every period: Ableton's UI thread hung on the Monomachine that way.

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr size_t g_frames = 64;
	constexpr uint32_t g_latency = 2 * g_frames;

	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Rig
	{
		std::atomic<uint64_t> rendered{0};
		std::atomic<bool> rendering{false};
		std::chrono::microseconds renderCost{0};
		std::atomic<float> level{1.0f};

		md::AsyncRender render{[this](const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outs,
			const size_t _frames, const std::vector<synthLib::SMidiEvent>&, std::vector<synthLib::SMidiEvent>&)
		{
			rendering = true;
			const auto until = Clock::now() + renderCost;
			while(Clock::now() < until)
				std::this_thread::yield();
			for(size_t c = 0; c < md::AsyncRender::ChannelsOut; ++c)
				for(size_t i = 0; i < _frames; ++i)
					_outs[c][i] = level;
			rendering = false;
			++rendered;
		}};

		std::array<std::vector<float>, md::AsyncRender::ChannelsOut> out;
		std::vector<synthLib::SMidiEvent> midiIn, midiOut;

		Rig()
		{
			for(auto& o : out)
				o.assign(g_frames, -1.0f);
		}

		// One host callback. Returns the first output sample.
		float process(const bool _realtime = true)
		{
			synthLib::TAudioInputs ins{};
			synthLib::TAudioOutputs outs{};
			for(size_t c = 0; c < out.size(); ++c)
				outs[c] = out[c].data();
			render.process(ins, outs, g_frames, midiIn, midiOut, _realtime);
			return out[0][0];
		}
	};

	// A render thread far slower than real time: in real time the host never waits longer than half a
	// block for it and plays what is missing as silence; offline it waits for every block.
	void verifyRealtimeHostWaitsBounded()
	{
		constexpr double sampleRate = 44100.0;
		const auto halfBlock = std::chrono::duration<double>(0.5 * g_frames / sampleRate);
		{
			Rig rig;
			rig.renderCost = std::chrono::milliseconds(100);
			rig.render.start(g_latency, sampleRate);
			auto worst = Clock::duration::zero();
			for(int i = 0; i < 60; ++i)
			{
				const auto start = Clock::now();
				rig.process(true);
				worst = std::max(worst, Clock::now() - start);
			}
			const auto worstMs = std::chrono::duration<double, std::milli>(worst).count();
			std::cout << "mdAsyncRenderTest: longest real time host call with a render 70 times slower than real time: "
				<< worstMs << " ms (half a block: " << std::chrono::duration<double, std::milli>(halfBlock).count() << " ms)\n";
			// The bound plus generous scheduling slack (other tests load the machine), far below the
			// 100 ms a block takes to render
			require(worst < std::chrono::milliseconds(30), "a real time host waited for a render far behind");
			require(rig.render.missedBlocks() > 0, "late blocks were not counted as missed");
			require(rig.render.droppedBlocks() > 0, "a render JobCount blocks behind did not make the host drop blocks");
			rig.render.stop();
		}
		{
			Rig rig;
			rig.renderCost = std::chrono::milliseconds(5);
			rig.render.start(g_latency, sampleRate);
			// The latency's silence, then every block rendered: the host waited for each
			bool rendered = true;
			for(int i = 0; i < 8; ++i)
			{
				const auto level = rig.process(false);
				if(i >= 2)
					rendered &= level == 1.0f;
			}
			require(rendered, "an offline host did not wait for the rendered blocks");
			require(rig.render.missedBlocks() == 0 && rig.render.droppedBlocks() == 0, "an offline host missed or dropped a block");
			rig.render.stop();
		}
	}

	// Blocks handed over before finish() are rendered when it returns.
	void verifyFinishCoversHandedOverBlocks()
	{
		Rig rig;
		rig.render.start(g_latency);
		for(int i = 0; i < 10; ++i)
			rig.process();
		rig.render.finish();
		require(rig.rendered == 10, "finish() returned before the handed-over blocks were rendered");
		rig.render.stop();
	}

	// A pause holds the render thread between blocks, and a host callback in
	// the meantime plays what is missing as silence instead of waiting.
	void verifyPauseHoldsAndHostDoesNotWait()
	{
		Rig rig;
		rig.render.start(g_latency);
		rig.render.pause();
		require(!rig.rendering, "render thread still inside a block after pause()");
		const auto before = rig.rendered.load();
		// The latency's worth of silence, then blocks nobody rendered.
		for(int i = 0; i < 4; ++i)
			require(rig.process() == 0.0f, "host did not play silence while the render thread was paused");
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		require(rig.rendered == before, "render thread rendered while paused");
		// Pauses nest.
		rig.render.pause();
		rig.render.resume();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		require(rig.rendered == before, "inner resume() ended the outer pause");
		// A pause longer than the job slots: the host must drop blocks
		// rather than wait for a slot while it holds the pausing thread's lock.
		for(int i = 0; i < 40; ++i)
			require(rig.process() == 0.0f, "host did not play silence while the render thread was paused");
		require(rig.render.droppedBlocks() > 0, "a pause longer than the job slots dropped no block");
		rig.render.resume();
		rig.render.finish();
		require(rig.rendered > before, "blocks handed over during the pause were not rendered after it");
		// Dropped blocks play as silence, then the rendered audio comes back. A real time host does not
		// wait for the render thread: it gets the time of a block to catch up, which finish() gives here.
		bool back = false;
		for(int i = 0; i < 200 && !back; ++i)
		{
			back = rig.process() == 1.0f;
			rig.render.finish();
		}
		require(back, "rendered audio did not come back after the dropped blocks");
		rig.render.stop();
	}

	// The Plugin pattern against a device twice slower than real time: the
	// host thread holds the lock during process() and hands over a block
	// every period; a control thread finishes and pauses unlocked, then
	// locks. Every control access must complete promptly.
	void verifyControlAccessWithSlowDevice()
	{
		Rig rig;
		constexpr auto period = std::chrono::microseconds(1000);
		rig.renderCost = 2 * period;
		rig.render.start(g_latency);

		std::recursive_mutex lock;
		std::atomic<bool> stop{false};
		// Paced like an audio device: a late callback does not make the next
		// one come early, it moves to the following period.
		std::thread host([&]
		{
			auto next = Clock::now();
			while(!stop)
			{
				{
					std::lock_guard guard(lock);
					rig.process();
				}
				do
					next += period;
				while(next <= Clock::now());
				while(Clock::now() < next)
					std::this_thread::yield();
			}
		});

		auto worst = Clock::duration::zero();
		for(int i = 0; i < 20; ++i)
		{
			const auto start = Clock::now();
			rig.render.finish();
			rig.render.pause();
			{
				std::lock_guard guard(lock);
				require(!rig.rendering, "device rendered during a control access");
			}
			rig.render.resume();
			worst = std::max(worst, Clock::now() - start);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		stop = true;
		host.join();
		rig.render.stop();
		const auto worstMs = std::chrono::duration_cast<std::chrono::milliseconds>(worst).count();
		std::cout << "mdAsyncRenderTest: slowest control access with a device at 200% of real time: "
			<< worstMs << " ms\n";
		require(worst < std::chrono::milliseconds(500), "control access waited too long for a slow device");
	}
}

int main()
{
	// A regression here hangs rather than fails: turn the hang into a failure.
	std::thread([]
	{
		std::this_thread::sleep_for(std::chrono::seconds(30));
		std::fputs("mdAsyncRenderTest: FAIL (hang: a control access never returned)\n", stderr);
		std::_Exit(1);
	}).detach();
	try
	{
		std::cout << "mdAsyncRenderTest: finish" << std::endl;
		verifyFinishCoversHandedOverBlocks();
		std::cout << "mdAsyncRenderTest: pause" << std::endl;
		verifyPauseHoldsAndHostDoesNotWait();
		std::cout << "mdAsyncRenderTest: slow device" << std::endl;
		verifyControlAccessWithSlowDevice();
		std::cout << "mdAsyncRenderTest: bounded host wait" << std::endl;
		verifyRealtimeHostWaitsBounded();
		std::cout << "mdAsyncRenderTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdAsyncRenderTest: " << _error.what() << '\n';
		return 1;
	}
}
