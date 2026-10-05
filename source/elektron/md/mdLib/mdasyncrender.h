#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "mdtransportsignal.h"

#include "synthLib/audioTypes.h"
#include "synthLib/midiTypes.h"

namespace synthLib
{
	class RealtimeInstrumentation;
}

namespace md
{
	// Renders a device on its own thread, one step behind the host, the way the
	// other gearmulator synths run their DSPs. The host's audio callback only
	// hands over a block (audio input and MIDI) and takes back rendered audio
	// from a FIFO that starts with _latency frames of silence, so the emulation
	// of block N runs on another core while the host does its other work, and
	// the callback itself costs a copy - which is what a DAW's CPU meter sees.
	// The output is delayed by exactly the latency the plug-in reports; MIDI
	// keeps its offset inside the block it arrived with.
	//
	// Threads: process() is called by the host's audio thread only (callers
	// may rotate but never overlap). The render function runs on the render
	// thread only. finish()/pause()/resume() may be called from any other
	// thread; anybody touching the rendered device outside process() must
	// pause it first. A pause holds the render thread between two blocks
	// until the matching resume(); pauses nest. While it holds, process()
	// does not wait for audio that cannot come: a block the render thread
	// has not delivered yet plays as silence.
	class AsyncRender
	{
	public:
		using RenderFunc = std::function<void(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs&,
			size_t, const std::vector<synthLib::SMidiEvent>&, std::vector<synthLib::SMidiEvent>&)>;

		static constexpr uint32_t ChannelsIn = 2;
		static constexpr uint32_t ChannelsOut = 6;

		explicit AsyncRender(RenderFunc _render);
		~AsyncRender();

		AsyncRender(const AsyncRender&) = delete;
		AsyncRender& operator=(const AsyncRender&) = delete;

		// Starts the render thread with _latency frames of silence queued; frames are at _sampleRate.
		void start(uint32_t _latency, double _sampleRate = 44100.0);
		// Renders what was handed over, then stops the thread and drops the queue.
		void stop();
		bool running() const { return m_running.load(std::memory_order_acquire); }

		// A block the render thread has not delivered yet is waited for: offline (_realtime false)
		// until it comes, in real time only while the render thread is on it and for half a block at
		// most. Past that the host would be late with every other track, and for a render thread further
		// behind it would wait for audio it plays as silence anyway: what is missing plays as silence
		// (missedBlocks), and the render thread catches up on the blocks after it.
		void process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs,
			size_t _frames, const std::vector<synthLib::SMidiEvent>& _midiIn,
			std::vector<synthLib::SMidiEvent>& _midiOut, bool _realtime = true);

		// Returns once the blocks handed over before the call are rendered, or
		// at once while a pause holds (they cannot be rendered then).
		void finish();
		// Returns once the render thread holds between two blocks.
		void pause();
		void resume();

		// Host blocks whose output was not ready when the callback needed it.
		uint64_t lateBlocks() const { return m_lateBlocks.load(std::memory_order_relaxed); }
		// Host blocks dropped while paused with every job slot in use.
		uint64_t droppedBlocks() const { return m_droppedBlocks.load(std::memory_order_relaxed); }
		// Late host blocks played with silence in real time instead of waiting longer (see process()).
		uint64_t missedBlocks() const { return m_missedBlocks.load(std::memory_order_relaxed); }
		struct Stats
		{
			uint64_t jobs = 0;
			uint64_t renderNs = 0;		// render thread busy rendering
			uint64_t waitNs = 0;		// host blocked on output that was not ready
			uint64_t maxJobFrames = 0;
			uint64_t backlogSum = 0;	// jobs not yet rendered, summed at each host call
			uint64_t backlogMax = 0;
			uint64_t idleNs = 0;		// render thread waiting for a job
			uint64_t queueNs = 0;		// hand-over to render start, summed over jobs
		};
		Stats stats() const
		{
			return {m_statJobs.load(std::memory_order_relaxed), m_statRenderNs.load(std::memory_order_relaxed),
				m_statWaitNs.load(std::memory_order_relaxed), m_statMaxFrames.load(std::memory_order_relaxed),
				m_statBacklogSum.load(std::memory_order_relaxed), m_statBacklogMax.load(std::memory_order_relaxed),
				m_statIdleNs.load(std::memory_order_relaxed), m_statQueueNs.load(std::memory_order_relaxed)};
		}
		// Render time of the last JobTimeCount jobs (diagnostics; read while idle).
		static constexpr size_t JobTimeCount = 65536;
		const std::vector<uint32_t>& jobTimesNs() const { return m_jobTimesNs; }
		// Per-job timeline (diagnostics, steady_clock ns): host call entry, host
		// resumes with its output, render start, render end.
		struct Timeline { int64_t hostEntry = 0, hostResume = 0, renderStart = 0, renderEnd = 0; };
		const std::vector<Timeline>& timeline() const { return m_timeline; }

	private:
		static constexpr size_t JobCount = 16;
		static constexpr size_t FifoFrames = 65536;	// power of two

		struct Job
		{
			std::array<std::vector<float>, ChannelsIn> in;
			std::vector<synthLib::SMidiEvent> midiIn;
			std::vector<synthLib::SMidiEvent> midiOut;
			size_t frames = 0;
			size_t gap = 0;				// silent frames before this block's output
			int64_t submittedAt = 0;	// steady_clock ns (diagnostics)
			// The performance capture recording the host callback that handed the block over, if any
			synthLib::RealtimeInstrumentation* instrumentation = nullptr;
		};

		void threadFunc();
		void harvest(size_t _frames, std::vector<synthLib::SMidiEvent>& _midiOut);
		// The host reads past the render thread when it plays a block that was
		// not rendered yet as silence; the frames rendered for it later are
		// written but never read.
		size_t fifoAvailable() const
		{
			const uint64_t read = m_fifoRead.load(std::memory_order_acquire);
			const uint64_t write = m_fifoWrite.load(std::memory_order_acquire);
			return write > read ? static_cast<size_t>(write - read) : 0;
		}

		RenderFunc m_render;
		std::array<Job, JobCount> m_jobs;
		std::array<std::vector<float>, ChannelsOut> m_renderOut;
		std::array<std::vector<float>, ChannelsOut> m_fifo;

		// Job indices, monotonic: host submits, render thread completes, host
		// harvests the completed jobs' MIDI output and then reuses their slot.
		alignas(64) std::atomic<uint64_t> m_submitted{0};
		alignas(64) std::atomic<uint64_t> m_done{0};
		uint64_t m_harvested = 0;
		// Host thread only: blocks dropped while paused (see process()).
		size_t m_gapFrames = 0;
		std::vector<synthLib::SMidiEvent> m_carryMidi;

		// Frame FIFO of rendered audio: render thread writes, host reads.
		alignas(64) std::atomic<uint64_t> m_fifoWrite{0};
		alignas(64) std::atomic<uint64_t> m_fifoRead{0};

		alignas(64) std::atomic<bool> m_exit{false};
		std::atomic<bool> m_running{false};
		std::atomic<uint32_t> m_hold{0};	// pauses in effect
		std::atomic<bool> m_parked{false};	// render thread holds for a pause
		std::chrono::microseconds m_idleSpin{0};	// extra spin for the next job before parking (MDMM_RENDER_SPIN_US)
		std::atomic<uint64_t> m_lateBlocks{0};
		std::atomic<uint64_t> m_droppedBlocks{0};
		std::atomic<uint64_t> m_missedBlocks{0};
		double m_sampleRate = 44100.0;		// of the frames handed over; set by start()
		std::atomic<uint64_t> m_statJobs{0};
		std::atomic<uint64_t> m_statRenderNs{0};
		std::atomic<uint64_t> m_statWaitNs{0};
		std::atomic<uint64_t> m_statMaxFrames{0};
		std::atomic<uint64_t> m_statBacklogSum{0};
		std::atomic<uint64_t> m_statBacklogMax{0};
		std::atomic<uint64_t> m_statIdleNs{0};
		std::atomic<uint64_t> m_statQueueNs{0};
		std::vector<uint32_t> m_jobTimesNs = std::vector<uint32_t>(JobTimeCount, 0);
		std::vector<Timeline> m_timeline = std::vector<Timeline>(JobTimeCount);
		TransportSignal m_jobSignal;		// host -> render thread
		TransportSignal m_doneSignal;		// render thread -> host / finish / pause
		std::thread m_thread;
	};
}
