#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace synthLib
{
	enum class RealtimeEventKind { PanelInput, PanelInputResult, PanelDelivery, HostTransport, DeviceAccess, RenderJob };
	struct RealtimeEvent
	{
		RealtimeEventKind kind = RealtimeEventKind::PanelInput;
		uint64_t timeNanoseconds = 0, sequence = 0, callbackIndex = 0, inputId = 0;
		// DeviceAccess: waiting for the device to pause, then holding it. RenderJob: rendering the block.
		uint64_t waitNanoseconds = 0, holdNanoseconds = 0, durationNanoseconds = 0;
		uint32_t model = 0, command = 0, argument = 0;
		uint32_t frames = 0, compilations = 0, deferredCompilations = 0;	// RenderJob
		bool accepted = false, deferred = false;
		bool playing = false, offline = false, bypassed = false, transportKnown = false, initial = false;
	};

	// One callback's correlated timings. All durations are nanoseconds; outer
	// timings include inner work. Inter-callback spacing is context, not an xrun.
	struct RealtimeSlowCallback
	{
		uint64_t index = 0, startNanoseconds = 0, durationNanoseconds = 0;
		uint64_t budgetNanoseconds = 0, interCallbackNanoseconds = 0;
		uint64_t synthNanoseconds = 0, lockWaitNanoseconds = 0;
		uint64_t resamplerNanoseconds = 0, deviceNanoseconds = 0;
		uint64_t deferredNanoseconds = 0, liveJitCompilations = 0, deferredJitCompilations = 0;
		// A device rendering on its own thread: waiting for a block it had not rendered yet, the blocks it
		// could not take (paused, or every slot busy), and the late ones played as silence rather than
		// waited for longer.
		uint64_t renderWaitNanoseconds = 0;
		uint32_t renderDroppedBlocks = 0;
		uint32_t renderMissedBlocks = 0;
		uint32_t frames = 0, outputBuses = 0, outputChannels = 0;
		uint32_t midiEvents = 0, midiBytes = 0, deviceSampleRate = 0;
		uint32_t resamplerMode = 0, dspClockPercent = 100;
		double sampleRate = 0;
		bool bypassed = false, playing = false, offline = false;
		bool resamplingActive = false, dualMachine = false;
	};

	struct RealtimeInstrumentationSnapshot
	{
		bool enabled = false;
		uint64_t offlineCallbackCount = 0;
		uint64_t slowCallbacksDropped = 0;
		uint64_t timelineEvents = 0, timelineEventsDropped = 0;
		// Realtime callback duration/budget: <25%, <50%, <75%, <100%, <150%, >=150%.
		std::array<uint64_t, 6> realtimeBudgetHistogram{};
		uint64_t outerHostCallbackCount = 0;
		uint64_t outerHostCallbackNanoseconds = 0;
		uint64_t outerHostCallbackMaxNanoseconds = 0;
		// Diagnostic deadline comparison only, excluding offline callbacks. These
		// estimates are not the host or audio driver's actual xrun count.
		uint64_t outerHostCallbackOverrunCount = 0;
		uint64_t outerHostCallbackMaxOverrunNanoseconds = 0;
		uint64_t callbacksWithJitCompilation = 0;
		uint64_t bypassedCallbackCount = 0;
		uint64_t synthProcessCount = 0;
		uint64_t synthProcessNanoseconds = 0;
		uint64_t synthProcessMaxNanoseconds = 0;
		uint64_t synthProcessLockWaitNanoseconds = 0;
		uint64_t synthProcessLockWaitMaxNanoseconds = 0;
		uint64_t resamplerCallCount = 0;
		uint64_t resamplerNanoseconds = 0;
		uint64_t resamplerMaxNanoseconds = 0;
		uint64_t resamplerHostFrames = 0;
		uint64_t resamplingActiveCallbackCount = 0;
		uint64_t deviceProcessNanoseconds = 0;
		uint64_t deviceProcessMaxNanoseconds = 0;
		uint64_t jitCompilationCount = 0;
		uint64_t liveJitCompilationCount = 0;
		uint64_t deferredCandidateJitCompilationCount = 0;
		uint64_t deferredDualMachineCallbackCount = 0;
		uint64_t deferredCandidateAdvanceCount = 0;
		uint64_t deferredCandidateFrames = 0;
		uint64_t deferredCandidateNanoseconds = 0;
		uint64_t deferredCandidateMaxNanoseconds = 0;
		uint64_t callbacksWithAuxOutputBuses = 0;
		uint32_t latestActiveOutputBuses = 0;
		uint32_t maximumActiveOutputBuses = 0;
		uint32_t latestActiveOutputChannels = 0;
		uint32_t maximumActiveOutputChannels = 0;
		// Control accesses to the device (Plugin::withDeviceLocked, state, settings), from any thread:
		// waiting for a device rendering on its own thread to finish and pause, then holding it.
		uint64_t deviceAccessCount = 0;
		uint64_t deviceAccessWaitNanoseconds = 0;
		uint64_t deviceAccessWaitMaxNanoseconds = 0;
		uint64_t deviceAccessHoldNanoseconds = 0;
		uint64_t deviceAccessHoldMaxNanoseconds = 0;
		// A device rendering on its own thread (RenderScope): its compilations, also counted in
		// jitCompilationCount, and what the host callbacks waited for it.
		uint64_t renderJitCompilationCount = 0;
		uint64_t renderJobsWithJitCompilation = 0;
		uint64_t renderLateBlockCount = 0;
		uint64_t renderWaitNanoseconds = 0;
		uint64_t renderWaitMaxNanoseconds = 0;
		uint64_t renderDroppedBlockCount = 0;
		uint64_t renderMissedBlockCount = 0;
	};

	// Opt-in counters for diagnosing work performed on the host audio callback.
	// Enable before audio starts with GEARMULATOR_RT_INSTRUMENTATION=1, or use
	// setEnabled() through Plugin::getRealtimeInstrumentation(). No reporting is
	// performed by this class. The MD/MM diagnostics writer drains records off-thread.
	// Disabled operation is a relaxed atomic load and a predictable branch at each
	// instrumented outer boundary. Clock reads and counter writes happen only while
	// explicitly enabled.
	class RealtimeInstrumentation final
	{
	public:
		class CallbackScope final
		{
		public:
			CallbackScope(RealtimeInstrumentation& _owner, size_t _frames,
				double _sampleRate, bool _bypassed = false) noexcept;
			~CallbackScope();

			CallbackScope(const CallbackScope&) = delete;
			CallbackScope& operator=(const CallbackScope&) = delete;

			bool isActive() const noexcept { return m_owner != nullptr; }
			void setActiveOutputLayout(uint32_t _buses, uint32_t _channels) noexcept;
			void setHostState(bool _playing, bool _offline, bool _transportKnown = true) noexcept;
			void setMidiInputSummary(uint32_t _events, uint32_t _bytes) noexcept;

		private:
			RealtimeInstrumentation* m_owner = nullptr;
			uint64_t m_startNanoseconds = 0;
			uint64_t m_budgetNanoseconds = 0;
			uint32_t m_activeOutputBuses = 0;
			uint32_t m_activeOutputChannels = 0;
			uint32_t m_frames = 0, m_midiEvents = 0, m_midiBytes = 0;
			double m_sampleRate = 0;
			bool m_bypassed = false, m_playing = false, m_offline = false;
		};

		class DeferredCandidateScope final
		{
		public:
			explicit DeferredCandidateScope(uint32_t _frames) noexcept;
			~DeferredCandidateScope();

			DeferredCandidateScope(const DeferredCandidateScope&) = delete;
			DeferredCandidateScope& operator=(const DeferredCandidateScope&) = delete;

		private:
			RealtimeInstrumentation* m_owner = nullptr;
			uint64_t m_startNanoseconds = 0;
			uint32_t m_frames = 0;
			bool m_previousCandidateRole = false;
			bool m_render = false;	// inside a RenderScope rather than a host callback
		};

		// A device that renders ahead of the host on a thread of its own (md::AsyncRender) renders
		// each block inside one: the JIT compilations it makes are counted, as live ones, and a block
		// that compiled becomes a render_job event. _owner is the instrumentation of the host callback
		// that handed the block over (current() on that thread); null records nothing.
		class RenderScope final
		{
		public:
			RenderScope(RealtimeInstrumentation* _owner, uint32_t _frames) noexcept;
			~RenderScope();

			RenderScope(const RenderScope&) = delete;
			RenderScope& operator=(const RenderScope&) = delete;

		private:
			RealtimeInstrumentation* m_owner = nullptr;
			uint64_t m_startNanoseconds = 0;
			uint32_t m_frames = 0;
		};

		RealtimeInstrumentation() noexcept;

		// The instrumentation recording the host callback this thread runs; null outside one, or when
		// the callback started with recording off.
		static RealtimeInstrumentation* current() noexcept;

		void setEnabled(bool _enabled) noexcept;
		bool isEnabled() const noexcept
		{
			return m_enabled.load(std::memory_order_relaxed);
		}
		void reset() noexcept;
		// Single off-audio-thread consumer. Does not reset the queue while the
		// producer is active; a full queue drops new records rather than waiting.
		bool popSlowCallback(RealtimeSlowCallback& _callback) noexcept;
		// Separate bounded MPSC timeline: producers make one nonblocking attempt.
		// Contention/full capacity drops an event and is counted, never waited on.
		static constexpr size_t TimelineCapacity = 1024;
		struct PanelInputToken { uint64_t id = 0, epoch = 0; };
		PanelInputToken beginPanelInput(uint32_t _model, uint8_t _command, uint8_t _argument) noexcept;
		void endPanelInput(PanelInputToken _token, uint32_t _model, uint8_t _command,
			uint8_t _argument, bool _accepted) noexcept;
		static void recordCurrentPanelDelivery(uint32_t _model, uint8_t _command, uint8_t _argument) noexcept;
		bool popTimelineEvent(RealtimeEvent& _event) noexcept;
		static constexpr size_t SlowCallbackCapacity = 512;
		static void setCurrentDeviceContext(uint32_t _rate, uint32_t _mode,
			uint32_t _clockPercent) noexcept;
		// Each field is race-free during concurrent recording/reset, but the result
		// is intentionally a relaxed, non-transactional diagnostic snapshot.
		RealtimeInstrumentationSnapshot snapshot() const noexcept;

		void recordSynthProcess(uint64_t _nanoseconds) noexcept;
		void recordSynthProcessLockWait(uint64_t _nanoseconds) noexcept;
		void recordResampler(uint64_t _nanoseconds, uint64_t _deviceNanoseconds,
			size_t _hostFrames, bool _resamplingActive) noexcept;

		// Called only from the MD DSP JIT's per-block configuration callback. An
		// event is attributed only when a host callback scope or a RenderScope is
		// active on this thread.
		static void recordCurrentCallbackJitCompilation() noexcept;

		// A control access to the device ended (synthLib::Plugin's device pause). Any thread.
		void recordDeviceAccess(uint64_t _waitNanoseconds, uint64_t _holdNanoseconds) noexcept;
		// The host callback on this thread waited for a block the device's render thread had not
		// delivered, or could not hand a block over while the device was paused.
		static void recordCurrentRenderWait(uint64_t _nanoseconds) noexcept;
		static void recordCurrentDroppedBlock() noexcept;
		static void recordCurrentMissedBlock() noexcept;

	private:
		friend class CallbackScope;
		friend class DeferredCandidateScope;
		friend class RenderScope;

		void recordHostCallback(RealtimeSlowCallback _callback) noexcept;
		friend struct RealtimeInstrumentationTestAccess;
		void recordDeferredCandidate(uint32_t _frames, uint64_t _nanoseconds) noexcept;
		bool recordTimelineEvent(RealtimeEvent _event) noexcept;
		void recordHostTransport(bool _playing, bool _offline, bool _bypassed, bool _known) noexcept;

		static_assert(std::atomic<uint64_t>::is_always_lock_free
			&& std::atomic<bool>::is_always_lock_free,
			"Performance capture requires lock-free atomics");
		struct Slot
		{
			std::atomic<bool> ready{false};
			RealtimeSlowCallback callback;
		};
		std::array<Slot, SlowCallbackCapacity> m_slowCallbacks{};
		struct EventSlot { std::atomic<bool> ready{false}; RealtimeEvent event; };
		std::array<EventSlot, TimelineCapacity> m_timeline{};
		std::atomic_flag m_eventProducer = ATOMIC_FLAG_INIT;
		size_t m_eventWritePosition = 0; // protected by the nonblocking producer gate
		size_t m_eventReadPosition = 0; // sole report consumer
		std::atomic<uint64_t> m_timelineEvents{0}, m_timelineEventsDropped{0};
		std::atomic<uint64_t> m_nextInputId{1}, m_captureEpoch{1};
		uint64_t m_transportEpoch = 0; // audio callback thread only
		uint32_t m_lastTransportFlags = 0;
		// Host callbacks are serialized per instance. Only the producer accesses
		// write position, only the report worker accesses read position.
		size_t m_writePosition = 0, m_readPosition = 0;
		uint64_t m_previousCallbackStart = 0;
		std::atomic<uint64_t> m_offlineCallbackCount{0}, m_slowCallbacksDropped{0};
		std::array<std::atomic<uint64_t>, 6> m_realtimeBudgetHistogram{};
		std::atomic<bool> m_enabled{false};
		std::atomic<uint64_t> m_outerHostCallbackCount{0};
		std::atomic<uint64_t> m_outerHostCallbackNanoseconds{0};
		std::atomic<uint64_t> m_outerHostCallbackMaxNanoseconds{0};
		std::atomic<uint64_t> m_outerHostCallbackOverrunCount{0};
		std::atomic<uint64_t> m_outerHostCallbackMaxOverrunNanoseconds{0};
		std::atomic<uint64_t> m_callbacksWithJitCompilation{0};
		std::atomic<uint64_t> m_bypassedCallbackCount{0};
		std::atomic<uint64_t> m_synthProcessCount{0};
		std::atomic<uint64_t> m_synthProcessNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessMaxNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessLockWaitNanoseconds{0};
		std::atomic<uint64_t> m_synthProcessLockWaitMaxNanoseconds{0};
		std::atomic<uint64_t> m_resamplerCallCount{0};
		std::atomic<uint64_t> m_resamplerNanoseconds{0};
		std::atomic<uint64_t> m_resamplerMaxNanoseconds{0};
		std::atomic<uint64_t> m_resamplerHostFrames{0};
		std::atomic<uint64_t> m_resamplingActiveCallbackCount{0};
		std::atomic<uint64_t> m_deviceProcessNanoseconds{0};
		std::atomic<uint64_t> m_deviceProcessMaxNanoseconds{0};
		std::atomic<uint64_t> m_jitCompilationCount{0};
		std::atomic<uint64_t> m_liveJitCompilationCount{0};
		std::atomic<uint64_t> m_deferredCandidateJitCompilationCount{0};
		std::atomic<uint64_t> m_deferredDualMachineCallbackCount{0};
		std::atomic<uint64_t> m_deferredCandidateAdvanceCount{0};
		std::atomic<uint64_t> m_deferredCandidateFrames{0};
		std::atomic<uint64_t> m_deferredCandidateNanoseconds{0};
		std::atomic<uint64_t> m_deferredCandidateMaxNanoseconds{0};
		std::atomic<uint64_t> m_callbacksWithAuxOutputBuses{0};
		std::atomic<uint32_t> m_latestActiveOutputBuses{0};
		std::atomic<uint32_t> m_maximumActiveOutputBuses{0};
		std::atomic<uint32_t> m_latestActiveOutputChannels{0};
		std::atomic<uint32_t> m_maximumActiveOutputChannels{0};
		std::atomic<uint64_t> m_deviceAccessCount{0};
		std::atomic<uint64_t> m_deviceAccessWaitNanoseconds{0};
		std::atomic<uint64_t> m_deviceAccessWaitMaxNanoseconds{0};
		std::atomic<uint64_t> m_deviceAccessHoldNanoseconds{0};
		std::atomic<uint64_t> m_deviceAccessHoldMaxNanoseconds{0};
		std::atomic<uint64_t> m_renderJitCompilationCount{0};
		std::atomic<uint64_t> m_renderJobsWithJitCompilation{0};
		std::atomic<uint64_t> m_renderLateBlockCount{0};
		std::atomic<uint64_t> m_renderWaitNanoseconds{0};
		std::atomic<uint64_t> m_renderWaitMaxNanoseconds{0};
		std::atomic<uint64_t> m_renderDroppedBlockCount{0};
		std::atomic<uint64_t> m_renderMissedBlockCount{0};
	};
}
