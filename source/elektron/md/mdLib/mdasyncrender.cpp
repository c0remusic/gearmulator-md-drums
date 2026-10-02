#include "mdasyncrender.h"

#include <algorithm>
#include <chrono>

#include "mdenv.h"

#include "dsp56kBase/threadtools.h"

#include "synthLib/realtimeInstrumentation.h"

namespace md
{
	namespace
	{
		constexpr size_t g_reserveFrames = 8192;
		constexpr size_t g_reserveMidi = 256;
	}

	AsyncRender::AsyncRender(RenderFunc _render) : m_render(std::move(_render))
	{
		if(const auto spin = envCount<uint32_t>("MDMM_RENDER_SPIN_US"))
			m_idleSpin = std::chrono::microseconds(*spin);
		for(auto& job : m_jobs)
		{
			for(auto& in : job.in)
				in.reserve(g_reserveFrames);
			job.midiIn.reserve(g_reserveMidi);
			job.midiOut.reserve(g_reserveMidi);
		}
		for(auto& out : m_renderOut)
			out.reserve(g_reserveFrames);
		m_carryMidi.reserve(g_reserveMidi);
		for(auto& fifo : m_fifo)
			fifo.assign(FifoFrames, 0.0f);
	}

	AsyncRender::~AsyncRender()
	{
		stop();
	}

	void AsyncRender::start(const uint32_t _latency, const double _sampleRate)
	{
		stop();
		m_sampleRate = _sampleRate > 0.0 ? _sampleRate : 44100.0;
		m_submitted.store(0, std::memory_order_relaxed);
		m_done.store(0, std::memory_order_relaxed);
		m_harvested = 0;
		m_gapFrames = 0;
		m_carryMidi.clear();
		for(auto& fifo : m_fifo)
			std::fill(fifo.begin(), fifo.end(), 0.0f);
		// The latency is served as silence before the first rendered frame.
		const auto latency = std::min<size_t>(_latency, FifoFrames / 2);
		m_fifoRead.store(0, std::memory_order_relaxed);
		m_fifoWrite.store(latency, std::memory_order_relaxed);
		m_exit.store(false, std::memory_order_release);
		m_parked.store(false, std::memory_order_release);
		// A pause taken before the start (the device is paused whenever its
		// latency changes) holds the new thread too.
		m_running.store(true, std::memory_order_release);
		m_thread = std::thread([this] { threadFunc(); });
	}

	void AsyncRender::stop()
	{
		if(!m_thread.joinable())
			return;
		// The thread renders every job already handed over before it exits,
		// so the device is idle and consistent afterwards.
		m_exit.store(true, std::memory_order_release);
		m_jobSignal.notify();
		m_thread.join();
		m_running.store(false, std::memory_order_release);
		m_parked.store(false, std::memory_order_release);
		m_doneSignal.notify();
		for(auto& job : m_jobs)
		{
			job.midiIn.clear();
			job.midiOut.clear();
		}
	}

	void AsyncRender::finish()
	{
		// Blocks handed over later are concurrent with the caller; waiting for
		// them too would never end with a device slower than real time.
		const uint64_t target = m_submitted.load(std::memory_order_acquire);
		const auto rendered = [&]
		{
			return m_done.load(std::memory_order_acquire) >= target
				|| m_hold.load(std::memory_order_acquire) > 0 || !running();
		};
		while(!rendered())
			m_doneSignal.waitFor(std::chrono::milliseconds(10), rendered);
	}

	void AsyncRender::pause()
	{
		m_hold.fetch_add(1, std::memory_order_acq_rel);
		m_jobSignal.notify();
		const auto parked = [&] { return m_parked.load(std::memory_order_acquire) || !running(); };
		while(!parked())
			m_doneSignal.waitFor(std::chrono::milliseconds(10), parked);
	}

	void AsyncRender::resume()
	{
		// A render started inside a pause scope never saw that pause.
		uint32_t hold = m_hold.load(std::memory_order_relaxed);
		while(hold > 0 && !m_hold.compare_exchange_weak(hold, hold - 1, std::memory_order_acq_rel,
			std::memory_order_relaxed))
		{
		}
		m_jobSignal.notify();
	}

	void AsyncRender::harvest(const size_t _frames, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		// MIDI produced while rendering an earlier block leaves with the block
		// that plays its audio; its offset is clamped into this block.
		const uint64_t done = m_done.load(std::memory_order_acquire);
		while(m_harvested < done)
		{
			auto& job = m_jobs[m_harvested % JobCount];
			for(auto& ev : job.midiOut)
			{
				ev.offset = std::min<uint32_t>(ev.offset, _frames ? static_cast<uint32_t>(_frames - 1) : 0);
				_midiOut.push_back(std::move(ev));
			}
			job.midiOut.clear();
			++m_harvested;
		}
	}

	void AsyncRender::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs,
		const size_t _frames, const std::vector<synthLib::SMidiEvent>& _midiIn,
		std::vector<synthLib::SMidiEvent>& _midiOut, const bool _realtime)
	{
		const auto nowNs = []
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		};
		auto& timeline = m_timeline[m_submitted.load(std::memory_order_relaxed) % JobTimeCount];
		timeline.hostEntry = nowNs();
		_midiOut.clear();
		harvest(_frames, _midiOut);
		{
			const uint64_t backlog = m_submitted.load(std::memory_order_relaxed) - m_done.load(std::memory_order_acquire);
			m_statBacklogSum.fetch_add(backlog, std::memory_order_relaxed);
			if(backlog > m_statBacklogMax.load(std::memory_order_relaxed))
				m_statBacklogMax.store(backlog, std::memory_order_relaxed);
		}

		// Hand this block over. A slot is reused only once its MIDI output was
		// harvested; all slots busy means the renderer is JobCount blocks late.
		// If it is paused besides, no slot frees up before the pausing thread
		// gets the lock this call runs under; in real time, waiting for one would
		// hold the host for a block's render: the block is dropped then. Its
		// MIDI goes with the next block, and the next block's output starts
		// after as many silent frames, so the audio stays aligned.
		const uint64_t submitted = m_submitted.load(std::memory_order_relaxed);
		bool drop = false;
		while(submitted - m_harvested >= JobCount)
		{
			if(_realtime || m_parked.load(std::memory_order_acquire))
			{
				drop = true;
				break;
			}
			m_doneSignal.waitFor(std::chrono::milliseconds(10), [&]
			{
				return m_done.load(std::memory_order_acquire) > m_harvested
					|| m_parked.load(std::memory_order_acquire);
			});
			harvest(_frames, _midiOut);
		}
		if(drop)
		{
			m_droppedBlocks.fetch_add(1, std::memory_order_relaxed);
			synthLib::RealtimeInstrumentation::recordCurrentDroppedBlock();
			m_gapFrames = std::min(m_gapFrames + _frames, FifoFrames / 2);
			for(const auto& ev : _midiIn)
			{
				m_carryMidi.push_back(ev);
				m_carryMidi.back().offset = 0;
			}
		}
		else
		{
			auto& job = m_jobs[submitted % JobCount];
			for(size_t c = 0; c < ChannelsIn; ++c)
			{
				job.in[c].resize(_frames);
				if(_inputs[c])
					std::copy_n(_inputs[c], _frames, job.in[c].data());
				else
					std::fill_n(job.in[c].data(), _frames, 0.0f);
			}
			job.midiIn.assign(m_carryMidi.begin(), m_carryMidi.end());
			job.midiIn.insert(job.midiIn.end(), _midiIn.begin(), _midiIn.end());
			m_carryMidi.clear();
			job.frames = _frames;
			job.gap = m_gapFrames;
			m_gapFrames = 0;
			job.submittedAt = std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			job.instrumentation = synthLib::RealtimeInstrumentation::current();
			m_submitted.store(submitted + 1, std::memory_order_release);
			m_jobSignal.notify();
		}

		// Take this block's audio: rendered earlier, or the initial silence. A
		// paused render thread delivers nothing until the pause ends, and the
		// pausing thread may be waiting for the lock this call runs under.
		if(fifoAvailable() < _frames)
		{
			m_lateBlocks.fetch_add(1, std::memory_order_relaxed);
			const auto waitStart = std::chrono::steady_clock::now();
			const auto ready = [&]
			{
				return fifoAvailable() >= _frames || m_parked.load(std::memory_order_acquire);
			};
			if(!_realtime)
			{
				while(!ready())
					m_doneSignal.waitFor(std::chrono::milliseconds(10), ready);
			}
			else
			{
				// In real time, only for the block the render thread is on (the frames missing fit in one
				// block), and half a block's time at most (see the declaration)
				const uint64_t needed = m_fifoRead.load(std::memory_order_relaxed) + _frames;
				const uint64_t written = m_fifoWrite.load(std::memory_order_acquire);
				if(needed <= written + _frames)
				{
					const auto deadline = waitStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
						std::chrono::duration<double>(0.5 * static_cast<double>(_frames) / m_sampleRate));
					// Spinning: a kernel wait this short can sleep a whole timer tick (15.6 ms on Windows)
					while(!ready() && std::chrono::steady_clock::now() < deadline)
					{
						for(int i = 0; i < 64 && !ready(); ++i)
							TransportSignal::cpuPause();
					}
				}
				if(!ready())
				{
					m_missedBlocks.fetch_add(1, std::memory_order_relaxed);
					synthLib::RealtimeInstrumentation::recordCurrentMissedBlock();
				}
			}
			const auto waitNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - waitStart).count());
			m_statWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
			synthLib::RealtimeInstrumentation::recordCurrentRenderWait(waitNs);
		}
		timeline.hostResume = nowNs();
		const uint64_t read = m_fifoRead.load(std::memory_order_relaxed);
		const size_t available = std::min(fifoAvailable(), _frames);
		for(size_t c = 0; c < ChannelsOut; ++c)
		{
			if(!_outputs[c])
				continue;
			const auto& fifo = m_fifo[c];
			for(size_t i = 0; i < available; ++i)
				_outputs[c][i] = fifo[(read + i) & (FifoFrames - 1)];
			std::fill(_outputs[c] + available, _outputs[c] + _frames, 0.0f);
		}
		m_fifoRead.store(read + _frames, std::memory_order_release);
		m_jobSignal.notify();	// room in the FIFO for a renderer waiting on it
		harvest(_frames, _midiOut);
	}

	void AsyncRender::threadFunc()
	{
		// Same scheduling band as the host's audio threads (MMCSS "Pro Audio"):
		// the host waits on this thread whenever it falls behind.
		void* const task = dsp56k::ThreadTools::joinProAudioTask();
		dsp56k::ThreadTools::setCurrentThreadName("MD render");
		const auto held = [this]
		{
			return m_hold.load(std::memory_order_acquire) > 0 && !m_exit.load(std::memory_order_acquire);
		};
		for(;;)
		{
			if(held())
			{
				m_parked.store(true, std::memory_order_release);
				m_doneSignal.notify();
				while(held())
					m_jobSignal.waitFor(std::chrono::milliseconds(100), [&] { return !held(); });
				m_parked.store(false, std::memory_order_release);
				continue;
			}
			const uint64_t index = m_done.load(std::memory_order_relaxed);
			if(m_submitted.load(std::memory_order_acquire) == index)
			{
				if(m_exit.load(std::memory_order_acquire))
					break;
				const auto idleStart = std::chrono::steady_clock::now();
				const auto jobReady = [&]
				{
					return m_exit.load(std::memory_order_acquire) || held()
						|| m_submitted.load(std::memory_order_acquire) != index;
				};
				// The next block arrives within one host period: waiting for it
				// on a CPU costs that core but no wake-up latency, which would
				// otherwise eat into the one period the render has.
				while(!jobReady() && std::chrono::steady_clock::now() - idleStart < m_idleSpin)
				{
					for(int i = 0; i < 64 && !jobReady(); ++i)
						TransportSignal::cpuPause();
				}
				if(!jobReady())
					m_jobSignal.waitFor(std::chrono::milliseconds(100), jobReady);
				m_statIdleNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - idleStart).count()), std::memory_order_relaxed);
				continue;
			}
			auto& job = m_jobs[index % JobCount];
			synthLib::TAudioInputs ins{};
			for(size_t c = 0; c < ChannelsIn; ++c)
				ins[c] = job.in[c].data();
			synthLib::TAudioOutputs outs{};
			for(size_t c = 0; c < ChannelsOut; ++c)
			{
				m_renderOut[c].resize(job.frames);
				outs[c] = m_renderOut[c].data();
			}
			job.midiOut.clear();
			const auto renderStart = std::chrono::steady_clock::now();
			m_statQueueNs.fetch_add(static_cast<uint64_t>(std::max<int64_t>(0,
				std::chrono::duration_cast<std::chrono::nanoseconds>(renderStart.time_since_epoch()).count()
				- job.submittedAt)), std::memory_order_relaxed);
			{
				synthLib::RealtimeInstrumentation::RenderScope instrumentation(job.instrumentation,
					static_cast<uint32_t>(job.frames));
				m_render(ins, outs, job.frames, job.midiIn, job.midiOut);
			}
			const auto renderEnd = std::chrono::steady_clock::now();
			const auto renderNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				renderEnd - renderStart).count());
			{
				auto& timeline = m_timeline[index % JobTimeCount];
				timeline.renderStart = std::chrono::duration_cast<std::chrono::nanoseconds>(renderStart.time_since_epoch()).count();
				timeline.renderEnd = std::chrono::duration_cast<std::chrono::nanoseconds>(renderEnd.time_since_epoch()).count();
			}
			m_statRenderNs.fetch_add(renderNs, std::memory_order_relaxed);
			const auto jobNumber = m_statJobs.fetch_add(1, std::memory_order_relaxed);
			m_jobTimesNs[jobNumber % JobTimeCount] = static_cast<uint32_t>(std::min<uint64_t>(renderNs, 0xffffffffu));
			if(job.frames > m_statMaxFrames.load(std::memory_order_relaxed))
				m_statMaxFrames.store(job.frames, std::memory_order_relaxed);

			// The host drains the FIFO every block; it only fills up if the host
			// stops calling, and then the thread waits (or leaves on exit).
			const size_t frames = job.gap + job.frames;
			while(FifoFrames - fifoAvailable() < frames && !m_exit.load(std::memory_order_acquire))
			{
				m_jobSignal.waitFor(std::chrono::milliseconds(10), [&]
				{
					return FifoFrames - fifoAvailable() >= frames || m_exit.load(std::memory_order_acquire);
				});
			}
			// Blocks dropped before this one play as silence (see process()).
			const uint64_t write = m_fifoWrite.load(std::memory_order_relaxed);
			for(size_t c = 0; c < ChannelsOut; ++c)
			{
				auto& fifo = m_fifo[c];
				const auto& out = m_renderOut[c];
				for(size_t i = 0; i < job.gap; ++i)
					fifo[(write + i) & (FifoFrames - 1)] = 0.0f;
				for(size_t i = 0; i < job.frames; ++i)
					fifo[(write + job.gap + i) & (FifoFrames - 1)] = out[i];
			}
			m_fifoWrite.store(write + frames, std::memory_order_release);
			m_done.store(index + 1, std::memory_order_release);
			m_doneSignal.notify();
		}
		dsp56k::ThreadTools::leaveProAudioTask(task);
	}
}
