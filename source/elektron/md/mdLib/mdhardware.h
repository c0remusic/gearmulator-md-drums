#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mddsp.h"
#include "mdfrontpanel.h"
#include "mdhostaudioqueue.h"
#include "mdmc.h"
#include "mdpanel.h"
#include "mdrealtimemidiqueue.h"
#include "mdrom.h"
#include "mdscheduledmidi.h"
#include "mdstate.h"
#include "mdsysextransfer.h"
#include "mdtimedlinkring.h"
#include "mdtransportsignal.h"
#include "mdturbomidi.h"
#include "mdtransportdiagnostics.h"
#include "mdtypes.h"

#include "synthLib/audioTypes.h"
#include "synthLib/midiTypes.h"

namespace md
{
	// One scheduler slice can finish its current JIT block after crossing a cycle
	// target. Keep a small, reported input look-ahead so those codec reads never
	// need samples from a future host callback.
	inline constexpr uint32_t g_hostAudioInputSafetyFrames = 64;

	class Device;

	// Which execution adapter drives the DSPs (parallel-transport spec §9,
	// MDMM_TRANSPORT): Serial = the deterministic interleave scheduler runs
	// everything on the calling thread and may execute a DSP inline for a
	// host access; Parallel = a worker thread owns the producer DSP and
	// every such access waits on a published position instead; Pair = one
	// worker owns BOTH DSPs, which keep their serial interleave and link
	// catch-ups between them, while the calling thread keeps only the UC.
	// Pair suits the Monomachine: its link is a strobe request/burst
	// response the two DSPs must run in lockstep, and its cost splits
	// evenly between the UC and the DSPs.
	enum class TransportMode
	{
		Serial,
		Parallel,
		Pair,
	};

	// MDMM_TRANSPORT's value (std::getenv's result): serial, parallel or pair.
	// Anything else counts as unset, so the caller keeps its default: unset,
	// empty (PowerShell's [Environment]::SetEnvironmentVariable($name, $null)
	// leaves an empty variable that child processes see) and any other value,
	// which also prints a warning. A typo such as "pairs" used to select
	// Serial without a word.
	inline std::optional<TransportMode> parseTransportMode(const char* _mode)
	{
		if(!_mode || !*_mode)
			return std::nullopt;
		if(std::strcmp(_mode, "serial") == 0)
			return TransportMode::Serial;
		if(std::strcmp(_mode, "parallel") == 0)
			return TransportMode::Parallel;
		if(std::strcmp(_mode, "pair") == 0)
			return TransportMode::Pair;
		std::fprintf(stderr, "[MD] MDMM_TRANSPORT=\"%s\" ignored: not serial, parallel or pair\n", _mode);
		return std::nullopt;
	}

	struct FactoryFlashSnapshot
	{
		std::vector<uint8_t> cache;
		std::vector<uint8_t> baseline;
	};

	// md::Hardware models the Elektron ColdFire MCU and two DSP56303s. A single
	// deterministic interleave scheduler advances all three processors against a
	// shared machine clock on the calling thread.
	//
	// Topology: DSP2 (voice PRODUCER, 0x600000) -> DSP1 (MIXER/codec, 0x500000) -> DAC.
	// The mixer is the "main" DSP: it drives the audio output and its ESSI frame
	// counter is the master clock the MCU is paced against.
	// -------------------------------------------------------------------------
	class Hardware
	{
	public:
		using AudioOutputs = std::array<std::vector<dsp56k::TWord>, 6>;
		Hardware(const std::vector<uint8_t>& _romData = {}, const std::string& _romName = {},
			MachineModel _model = MachineModel::Machinedrum,
			const std::vector<uint8_t>& _initialPatchRam = {},
			std::shared_ptr<FrontPanelPublisher> _frontPanelPublisher = {},
			const std::vector<uint8_t>& _initialFlash = {},
			const std::vector<uint8_t>& _factoryFlashCache = {},
			const FlashSectorOverlay& _pendingFlashOverlay = {});
		Hardware(const std::vector<uint8_t>& _romData, const std::string& _romName,
			MachineModel _model, const std::vector<uint8_t>& _initialPatchRam,
			std::shared_ptr<FrontPanelPublisher> _frontPanelPublisher,
			const std::vector<uint8_t>& _initialFlash,
			const std::vector<uint8_t>& _factoryFlashCache,
			const FlashSectorOverlay& _pendingFlashOverlay,
			const std::vector<uint8_t>& _initialUserFlash);
		~Hardware();

		bool isValid() const;
		MachineModel getModel() const { return m_model; }
		bool isMonomachine() const { return m_model == MachineModel::Monomachine; }
		bool isAudioReady() const
		{
			return m_dspMixer.booted() && m_dspProducer.booted();
		}
		// Explicit firmware boundary for host MIDI commands. DSP boot alone is too
		// early, while rendered LCD pixels are presentation data and vary by ROM.
		bool isFirmwareMidiReady() const
		{
			return isAudioReady() && m_uc.isPanelHandshakeComplete()
				&& m_uc.isMidiReceiveReady();
		}
		uint64_t firmwareFingerprint() const { return m_firmwareFingerprint; }
		bool supportsRamRecordingMode() const
		{
			return m_model == MachineModel::Machinedrum
				&& m_firmwareFingerprint == g_mdOs163Fingerprint;
		}
		void requestRamRecordingMode(RamRecordingMode _mode);
		RamRecordingMode requestedRamRecordingMode() const { return m_ramRecordingMode; }
		uint64_t hostAudioOverflowCount() const
		{
			return m_schedHostAudioOverflow.load(std::memory_order_relaxed);
		}
		// Host output frames the codec had not produced by the end of their
		// block: they were output silent.
		uint64_t hostAudioUnderrunCount() const
		{
			return m_schedHostAudioUnderrun.load(std::memory_order_relaxed);
		}
		uint64_t hostAudioInputUnderflowCount() const
		{
			return hostAudioInputUnderflowCount(0) + hostAudioInputUnderflowCount(1);
		}
		uint64_t hostAudioInputUnderflowCount(const size_t _dspIndex) const
		{
			return m_hostAudioInputUnderflow.at(_dspIndex).load(std::memory_order_relaxed);
		}
		uint64_t hostAudioInputOverflowCount() const
		{
			return hostAudioInputOverflowCount(0) + hostAudioInputOverflowCount(1);
		}
		uint64_t hostAudioInputOverflowCount(const size_t _dspIndex) const
		{
			return m_hostAudioInputOverflow.at(_dspIndex).load(std::memory_order_relaxed);
		}
		void resetHostAudioInputQueueTelemetry()
		{
			for(auto& counter : m_hostAudioInputUnderflow)
				counter.store(0, std::memory_order_relaxed);
			for(auto& counter : m_hostAudioInputOverflow)
				counter.store(0, std::memory_order_relaxed);
		}
		size_t queuedMidiRxBytes() const { return m_uc.queuedMidiRxBytes(); }
		size_t midiRxOverflowCount() const { return m_uc.midiRxOverflowCount(); }
		uint64_t midiRxConsumedCount() const { return m_uc.midiRxConsumedCount(); }

		Microcontroller& getUC() { return m_uc; }
		std::vector<uint8_t> copyPatchRam() const;
		std::vector<uint8_t> copyFlashData() const { return m_uc.copyFlashData(); }
		std::vector<uint8_t> copyUserFlash() const { return m_uc.copyUserFlash(); }
		const std::vector<uint8_t>& flashBaseline() const { return m_rom.data(); }
		bool flashDirty() const { return m_uc.flashDirty(); }
		bool factoryFlashCacheReady();
		bool isFactoryFlashCacheReady() const
		{
			return m_factoryFlashReady.load(std::memory_order_acquire);
		}
		bool isFactoryFlashInitializationExpected() const
		{
			return m_factoryFlashInitializationExpected;
		}
		bool isFactoryFlashReadyForReboot() const
		{
			return m_factoryFlashReady.load(std::memory_order_acquire)
				|| (m_externalInteraction.load(std::memory_order_acquire)
					&& m_factoryFlashPreparationReady.load(std::memory_order_acquire));
		}
		bool isFactoryFlashCaptureDisqualified() const
		{
			return m_externalInteraction.load(std::memory_order_acquire)
				&& !m_factoryFlashReady.load(std::memory_order_acquire);
		}
		bool isProjectStateRestorePending() const
		{
			return m_pendingFlashRestoreActive.load(std::memory_order_acquire);
		}
		bool copyFactoryFlashBaseline(std::vector<uint8_t>& _baseline);
		std::vector<uint8_t> copyFactoryFlashCache();
		// Capture immutable source bytes while the machine is pinned. Cache encoding
		// scans the complete flash image and belongs after the outer Device lock is
		// released.
		bool copyFactoryFlashSnapshot(FactoryFlashSnapshot& _snapshot) const;
		bool copyPendingFlashOverlay(FlashSectorOverlay& _overlay) const;
		void disqualifyFactoryFlashCache();
		bool replaceFactoryFlashCache(const std::vector<uint8_t>& _cache);
		bool replaceFlashData(const std::vector<uint8_t>& _data, const bool _dirty)
		{
			if(_dirty)
				registerExternalInteraction();
			return m_uc.replaceFlashData(_data, _dirty);
		}
		// Role accessors used by the HI08 bridge and scheduler.
		Dsp& getDspProducer() { return m_dspProducer; }	// DSP2, index 1
		Dsp& getDspMixer()    { return m_dspMixer; }	// DSP1, index 0 (main/output)

		void processUC();
		void processAudio(uint32_t _frames, uint32_t _latency);
		void processAudio(const synthLib::TAudioOutputs& _outputs, uint32_t _frames, uint32_t _latency);
		void processAudio(const synthLib::TAudioInputs& _inputs,
			const synthLib::TAudioOutputs& _outputs, uint32_t _frames,
			uint32_t _latency);

		// Advance the whole machine by _machineFrames codec frames of shared
		// machine time on the calling thread, with NO background threads. One frame = g_dsp1CyclesPer
		// EsaiFrame (2304) DSP cycles = g_ucClockHz/g_samplerate (about 907.03) UC cycles. UC + both DSPs are
		// stepped event-driven (advance the most-lagging against the shared clock; synchronous HI08
		// catch-up at every host access). Drains the codec output ring so the mixer never blocks.
		// processAudio retains the drained frames; headless callers discard them.
		void advance(uint32_t _machineFrames);

		// Advance DSP _dspIndex to the UC's current machine-time position
		// before a host (HI08) access. Called from the DSP-side HI08
		// bridge so that every ColdFire read/write/CVR sees the target DSP at the same machine time,
		// which is what makes the boot handshake (UC poll <-> DSP reply) converge deterministically.
		void schedCatchUpDsp(uint32_t _dspIndex);
		// Execution seam of the parallel-transport spec (§5): make the target
		// DSP's state at the UC's current machine time observable before a
		// host access. Serial adapter = run it inline (schedCatchUpDsp); the
		// threaded adapter waits on the worker's published position instead.
		void waitForDspTime(uint32_t _dspIndex);
		TransportMode transportMode() const { return m_transportMode; }
		// The producer DSP runs on a worker thread (the parallel or pair
		// handoff has happened; it never goes back).
		bool isProducerThreaded() const { return m_dspThreaded[1].load(std::memory_order_acquire); }
		// Both DSPs run on one worker (TransportMode::Pair).
		bool isDspPairThreaded() const { return m_dspPairWorker.load(std::memory_order_acquire); }
		// The device tells, before each block, whether the scheduler runs on a
		// thread it owns (its render thread). Only then may the pair handoff pin
		// that thread to a core (see schedTryHandoffPair).
		void setPairPlacementAllowed(const bool _allowed) { m_pairPlacementAllowed.store(_allowed, std::memory_order_relaxed); }
		// The two DSPs run on the same thread, so one may run the other inline
		// for a link delivery: always, except with the producer alone on its
		// worker.
		bool dspsShareThread() const
		{
			return !m_dspThreaded[1].load(std::memory_order_acquire)
				|| m_dspPairWorker.load(std::memory_order_acquire);
		}
		// Pre-handoff (and always under the serial adapter) the HI08 bridge
		// may run a DSP inline in the caller's context. Once a DSP is owned by
		// a worker thread, every access waits on its published position.
		bool dspInlineRunAllowed(const uint32_t _dspIndex) const
		{
			return !m_dspThreaded[_dspIndex & 1].load(std::memory_order_acquire);
		}
		// Prepared (deferred-state) machines boot under the serial adapter and
		// never own threads; the committed machine re-enables the configured
		// mode (parallel-transport spec §7).
		void setTransportMode(const TransportMode _mode) { m_transportMode = _mode; }
		void enableParallelTransport();
		// Quiesce (spec §7): block until the producer worker has parked at the
		// current target, so its memory can be touched from this context.
		void waitProducerParked();
		// Dated UC->DSP stream support: the DSP cycle at which a host word or
		// command issued at _ucCycle becomes applicable.
		uint64_t hostToDspDeadline(uint32_t _dspIndex, uint64_t _ucCycle) const;
		TransportSignal& transportSignal() { return m_signal; }
		// UC context: the UC is stalled on a full host stream to this DSP, with
		// its time frozen, exactly where the serial bridge ran the DSP past the
		// UC in writeWordToDsp. Only then may the producer pass its UC gate.
		void setHostWriteBlocked(const uint32_t _dspIndex, const bool _blocked)
		{
			if(_blocked)
				m_hostWriteBlockEpisode[_dspIndex & 1].fetch_add(1, std::memory_order_release);
			m_hostWriteBlocked[_dspIndex & 1].store(_blocked, std::memory_order_release);
			m_signal.notify();
		}

		// Audio-thread wait on a worker condition. The audio thread owns the
		// UC and the mixer, and the producer's gates depend on the mixer's
		// position, so a plain wait could stall the very thing it waits for:
		// while the predicate is false the mixer keeps advancing in bounded
		// slices toward the block target, and only when it cannot advance any
		// further does the thread park on the signal. Expiry is telemetry
		// (per site), never a silent success.
		enum class TransportWaitSite : uint32_t
		{
			DspTime,
			LinkProducer,
			HostToDspRoom,
			ProducerParked,
			MixerGate,
			Count,
		};
		template<typename Predicate>
		bool waitTransport(const TransportWaitSite _site,
			const std::chrono::microseconds _timeout, Predicate&& _ready)
		{
			return waitTransportImpl(_site, _timeout,
				std::function<bool()>(std::forward<Predicate>(_ready)));
		}
		bool waitTransportImpl(TransportWaitSite _site, std::chrono::microseconds _timeout,
			const std::function<bool()>& _ready);
		uint64_t transportWaitClamps(const TransportWaitSite _site) const
		{
			return m_transportWaitClamps[static_cast<size_t>(_site)].load(std::memory_order_relaxed);
		}
		// Mirrored DMA channel enable bits, one per DSP and channel, kept by
		// the owning DSP's context through the Dma DE observer. The other
		// DSP's transport gates read these instead of the peer's registers.
		bool dmaEnabled(const uint32_t _dsp, const uint32_t _channel) const
		{
			return m_dmaEnabled[_dsp & 1].value[_channel].load(std::memory_order_acquire);
		}
		// UC-facing HI08 receive depth published from the UC context so a DSP
		// worker can evaluate its host-TX backlog without touching the UC's
		// register file. Stored only on change: every host pump publishes.
		void publishUcRxDepth(const uint32_t _dsp, const size_t _depth)
		{
			auto& depth = m_ucRxDepth[_dsp & 1].value;
			if(depth.load(std::memory_order_relaxed) != _depth)
				depth.store(_depth, std::memory_order_release);
		}
		size_t ucRxDepth(const uint32_t _dsp) const
		{
			return m_ucRxDepth[_dsp & 1].value.load(std::memory_order_acquire);
		}
		void notifyHostPumpStateChanged();
		uint64_t hostRxReadyCycle(uint32_t _dspIndex, uint64_t _dspCycle) const;
		uint64_t hostCurrentCycle() const { return m_schedUcCyclesDone; }
		// Diagnostic snapshot. The caller must serialize with the machine thread,
		// as Device does for its other control-plane observations.
		TransportScorecard getTransportScorecard() noexcept;
		void recordInlineHdi08Run(uint32_t _dspIndex, uint64_t _startCycle,
			uint64_t _clampCycle, bool _workComplete) noexcept;
		void recordMdLinkPurge(size_t _purgedFrames) noexcept;

		// Mark the start of a Machinedrum DMA receive window. No-op for MM.
		void mdLinkWindowFlushed();

		// Dated inter-DSP link rings, indexed by the CONSUMING DSP (0 = mixer
		// input, 1 = producer input). They replace the ESSI0 audio-input
		// rings as the link's word store (parallel-transport spec §4).
		TimedLinkRing& linkRing(const uint32_t _consumer)
		{
			return m_linkRing[_consumer];
		}
		// Availability as the consuming DSP's receiver sees it: stale heads
		// are disposed of first (spec §4 pop rule), so an all-stale ring
		// reports empty and the ESSI takes its hardware skip-on-empty path.
		// A word destroyed on an uncollected RX register (ROE) consumes this
		// wire tick: report no data so the RX exec early-returns, exactly
		// like the old push-side drop. With a threaded producer, an empty
		// ring is only reported empty once the producer's published position
		// proves it (pop rule step (c)); otherwise the mixer waits (step (d)).
		bool linkRxAvailable(uint32_t _consumer);

		bool sendMidi(const synthLib::SMidiEvent& _ev);
		// Audio-owner entry point: _ev.offset is relative to the next native block.
		// Host pad mapping and UART admission happen only at the resulting deadline.
		void retimeMidi(uint32_t _extraLatency);
		bool scheduleMidi(const synthLib::SMidiEvent& _ev, uint32_t _extraLatency);
		uint64_t scheduledMidiOverflowCount() const { return m_scheduledMidiOverflow.load(); }
		// Audio-thread-only producer path for small, already-encoded semantic
		// commands. Unlike sendMidi(), this never allocates or waits for space.
		bool trySendRealtimeMidi(const uint8_t* _bytes, size_t _count)
		{
			registerExternalInteraction();
			return m_realtimeMidiIn.tryPush(_bytes, _count);
		}
		template<size_t Count>
		bool trySendRealtimeMidi(const std::array<uint8_t, Count>& _bytes)
		{
			registerExternalInteraction();
			return m_realtimeMidiIn.tryPush(_bytes);
		}
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
		{
			m_uc.readMidiOut(_midiOut, m_midiOutputNativeOrigin.load(std::memory_order_relaxed));
		}
		// Queue a validated file-sized stream for paced delivery through the
		// emulated MIDI UART. The caller must hold the owning Plugin device lock.
		bool startMidiSysexTransfer(PreparedMidiSysexTransfer& _transfer);
		bool resumeMidiSysexReceiveMode(uint32_t _transferId, size_t _step)
		{
			return m_midiSysexTransfer.resumeReceiveMode(_transferId, _step);
		}
		bool cancelMidiSysexTransfer(std::vector<uint8_t>& _retiredPayload)
		{
			return m_midiSysexTransfer.cancel(_retiredPayload);
		}
		bool retireMidiSysexTransferPayload(std::vector<uint8_t>& _retiredPayload)
		{
			return m_midiSysexTransfer.retirePayload(_retiredPayload);
		}
		MidiSysexTransferProgress getMidiSysexTransferProgress() const
		{
			return m_midiSysexTransfer.progress();
		}
		bool isMidiSysexTransferActive() const { return m_midiSysexTransfer.ownsMidiWire(); }
		// Diagnostic/control-plane observation. The caller must serialize with the
		// machine thread (the Plugin device lock does this in product code).
		bool isMidiIngressIdle() const
		{
			return !m_midiSysexTransfer.ownsMidiWire()
				&& m_midiInByteCursor == 0 && m_midiIn.empty()
				&& m_realtimeMidiIn.size() == 0
				&& m_uc.queuedMidiRxBytes() == 0;
		}
		// Queue a front-panel button/encoder event ([row][mask]). trySendPanelEvent is bounded,
		// thread-safe, allocation-free, and never waits; false reports that the FIFO
		// rejected the packet. Row state is retained for recovery, while pulse commands
		// are best effort and may be retried. Both outcomes are visible via telemetry.
		bool trySendPanelEvent(uint8_t _cmd, uint8_t _arg);
		void sendPanelEvent(uint8_t _cmd, uint8_t _arg)
		{
			(void)trySendPanelEvent(_cmd, _arg);
		}
		size_t getPendingPanelInputBytes() const;
		size_t getPanelInputOverflowCount() const;
		PanelInputQueueStatus getPanelInputStatus() const;
		// Machine frames the scheduler was asked to reach, published before each
		// block: an emulated clock that other threads may read.
		uint64_t getEmulatedFrames() const
		{
			return m_schedTargetFrames.load(std::memory_order_acquire);
		}

		const auto& getAudioOutputs() const { return m_audioOutputs; }
		const std::string& getRomFilename() const { return m_rom.getFilename(); }

		// Last complete front-panel value published by the emulation thread. Returning
		// by value prevents consumers from retaining a reference to live decoder state.
		FrontPanel getFrontPanelSnapshot() const { return m_frontPanelPublisher->read(); }
		bool tryGetFrontPanelSnapshot(FrontPanel& _panel) const
		{
			return m_frontPanelPublisher->tryRead(_panel);
		}

	private:
		friend class Device;
		friend struct DevicePreparedStateTestAccess;
		friend struct HostRxFirmwareTestAccess;
		friend struct MidiTimingTestAccess;
		// Transfer the live sample-flash image and its factory-capture bookkeeping
		// into a prepared cold-boot machine without copying their backing stores.
		bool exchangePersistentFlashState(Hardware& _other);
		// Prepared machines publish into a private queue while they boot. Rebinding
		// happens only while Device is exclusively locked, immediately before commit,
		// so the live SPSC publisher always has exactly one producer.
		void setFrontPanelPublisher(std::shared_ptr<FrontPanelPublisher> _publisher);

		void ensureBufferSize(uint32_t _frames);
		void setHostAudioInputLatency(uint32_t _latency);
		void queueHostAudioInput(uint32_t _frames);
		void advanceFactoryFlashCapture();
		void serviceRamRecordingMode();
		void registerExternalInteraction();
		void pumpDsp2HostRequest();		// DSP2 HI08 HREQ -> ColdFire external IRQ4 (see .cpp)
		// Runs UC instructions for up to about _maxCycles as one batch when nothing
		// processUC checks per instruction can act before then. Returns the cycles
		// run, 0 when the UC must step through processUC.
		uint32_t processUCBatch(uint64_t _maxCycles);
		// Dated pop-side disposal (see .cpp). _rxTick = called from the RX
		// availability probe, the only site where the ROE arrival semantics
		// may destroy a word; returns true when it did.
		bool linkDisposeAtConsumer(uint32_t _consumer, bool _rxTick);
		// The consumer's machine-frame position for due-time checks;
		// +infinity before its origin is latched (undated boot traffic).
		double linkConsumerNow(uint32_t _consumer);
		bool linkHeadDue(uint32_t _consumer);
		bool linkHeadDue(uint32_t _consumer, double _now);	// _now = linkConsumerNow(_consumer)
		void onEssiCallbackMixer();		// master clock: advance the ESSI frame counter
		void pumpMidiIngress();

		const MachineModel m_model;
		Rom m_rom;
		const uint64_t m_firmwareFingerprint;
		bool m_factoryFlashInitializationExpected;
		Microcontroller m_uc;
		std::atomic<bool> m_externalInteraction{false};
		std::atomic<bool> m_factoryFlashReady{false};
		std::atomic<bool> m_factoryFlashPreparationReady{false};
		mutable std::mutex m_factoryFlashMutex;
		std::vector<uint8_t> m_factoryFlashCache;
		std::vector<uint8_t> m_factoryFlashBaseline;
		std::vector<uint8_t> m_pendingFlashImage;
		FlashSectorOverlay m_pendingFlashOverlay;
		std::vector<uint8_t> m_pendingPatchRam;
		std::atomic<bool> m_pendingFlashRestoreActive{false};
		std::atomic<bool> m_pendingFlashRestoreFailed{false};
		size_t m_factoryFlashCaptureOffset = 0;
		uint64_t m_factoryFlashCaptureFingerprint = 14695981039346656037ull;
		bool m_factoryFlashCaptureComplete = false;
		size_t m_pendingFlashSectorIndex = 0;
		FrontPanel m_frontPanel;	// writer-owned UART2 LCD/LED decoder
		std::shared_ptr<FrontPanelPublisher> m_frontPanelPublisher;
		TurboMidiTransfer m_midiSysexTransfer;
		Dsp m_dspMixer;		// index 0 = DSP1 (0x500000), receives the ring, drives the DAC
		alignas(64) Dsp m_dspProducer;	// index 1 = DSP2 (0x600000), produces voices into the ring
		RamRecordingMode m_ramRecordingMode = RamRecordingMode::Original;
		bool m_ramRecordingModePending = false;

		AudioOutputs m_audioOutputs;
		// Link word store, indexed by consumer DSP; see linkRing().
		std::array<TimedLinkRing, 2> m_linkRing;
		// Per-DSP state below lives one element per cache line: the mixer and
		// the producer each touch their own element at link-slot rate (about
		// 1 M/s on the MD), and two threads storing into one line bounce it
		// between their cores on every slot.
		template<typename T> struct alignas(64) PerDsp { T value{}; };
		// Receiver-enable edge detector per consumer (consumer context): a
		// serial wire has no memory, so everything queued while the receiver
		// was disabled dies when it enables.
		std::array<PerDsp<bool>, 2> m_linkRxWasEnabled{};
		// What the RX availability probe of a receive slot leaves to the pop
		// that follows it in the same slot, when one thread runs both DSPs:
		// the disposal is done, and the consumer's position is this.
		struct LinkRxTick
		{
			uint64_t cycles = ~0ull;	// consumer getCycles() at the probe, ~0 = none
			double now = 0.0;			// linkConsumerNow() there
		};
		std::array<PerDsp<LinkRxTick>, 2> m_linkRxTick{};
		std::array<PerDsp<std::array<std::atomic<bool>, 6>>, 2> m_dmaEnabled{};
		std::array<PerDsp<std::atomic<size_t>>, 2> m_ucRxDepth{};
		// Producer->mixer content offset in codec frames (TransportPolicy,
		// MD_LINK_PIPELINE_DEPTH override).
		double m_linkPipelineDepthFrames = 0.0;
		// Per-machine age of the last shallow link ring. This participates in the
		// MM stall-purge decision, so it must never be shared by concurrently
		// running Hardware instances (as it was when this lived as a static local).
		std::array<PerDsp<uint64_t>, 2> m_linkLastShallow{};
#if MD_TRANSPORT_DIAGNOSTICS
		TransportScorecard m_transportScorecard;
#endif

		// Codec frames produced by the mixer. Written in mixer context, read
		// by the purge age and the drain; atomic so those reads stay valid
		// once the mixer owns a worker thread (spec §4).
		alignas(64) std::atomic<uint32_t> m_esaiFrameIndex{0};	// codec frames produced

		// ---------------------------------------------------------------------------------------
		// Deterministic interleave scheduler state. advance() maintains a shared machine clock in codec
		// frames and steps UC + both DSPs event-driven against it. Each DSP's cycle counter is
		// rate-locked to the clock from the frame it becomes runnable (origin latched here). See
		// advance() in mdhardware.cpp for the loop and timing constants.
		// ---------------------------------------------------------------------------------------
		bool     schedStep();					// one advance() event-loop iteration; false once all caught up
		void     schedRunDspSlice(uint32_t _dspIndex, double _subTarget);	// one bounded DSP slice (serial adapter)
		void     schedTryHandoffProducer();		// start the DSP2 worker once the boot-era protocol is armed
		void     schedTryHandoffPair();			// start the worker of both DSPs (TransportMode::Pair)
		// Worker of both DSPs: the serial scheduler's laggard-first DSP choice,
		// in one-frame chunks toward the published block target, each DSP gated
		// by the published UC position like the lone producer; parks when both
		// are gated.
		void     pairWorkerLoop();
		// A DSP's gate on the pair worker (worker context). With _update it
		// keeps the MM backpressure state and publishes it; without, it only
		// reads it - the form a wait predicate may use, since predicates run
		// under the signal's mutex and must not notify.
		uint64_t pairGateCycles(uint32_t _dspIndex, bool _update = true);
		// Pair worker: the DSPs' lead over the UC at UC cycle _ucCycles, in UC
		// cycles. 0 during a held DSP2 round trip (MD_PAIR_HOLD_DSP2_US).
		uint64_t pairDspLeadUc(uint64_t _ucCycles) const;
		// One chunk of a DSP toward _targetCyc on the pair worker.
		void     runPairChunk(uint32_t _dspIndex, uint64_t _targetCyc);
		// Audio thread, pair mode: wait until the mixer has produced the codec
		// frames of the block target (or is held by MM backpressure).
		void     waitMixerAtTarget();
		double   schedDspFramePos(uint32_t _dspIndex);	// a runnable DSP's machine-frame position
		uint64_t schedFrameToDspCycles(uint32_t _dspIndex, double _frames) const;
		// DSP2 worker (parallel-transport spec §3): free-runs in one-frame
		// chunks toward the published block target, gated by the published
		// UC and mixer positions plus L_lead, and parks when gated.
		void     producerWorkerLoop();
		// The producer's gates from published positions only: any thread may
		// evaluate it, whoever is executing the producer.
		uint64_t producerGateHint() const;
		// The gates plus a blocked host write's allowance, which reads the
		// host stream head: only under m_producerExec.
		uint64_t producerTargetCycles() const;
		// One producer chunk toward _targetCyc; the caller holds m_producerExec.
		// Returns false when the producer is already at its gates.
		bool     runProducerChunk(uint64_t _targetCyc);
		// Audio thread only: run a producer chunk here if the worker is not
		// executing one, see waitSignalOrHelp.
		bool     tryHelpProducer();
		bool     waitSignalOrHelp(std::chrono::microseconds _timeout, const std::function<bool()>& _ready);
		void     stopProducerWorker();
		void     schedDrainCodecOutput();		// pop the mixer ESSI1 output ring so its TX never blocks
		// _producerPos: schedDspFramePos(_producer), when the caller has it
		void     schedCatchUpDspToDsp(uint32_t _consumer, uint32_t _producer, std::optional<double> _producerPos = {});
		// Compact, preallocated host-facing storage keeps codec draining bounded.
		// Overflow retains the newest frames and is explicit telemetry; processAudio
		// drains the queue every callback so stale audio cannot accumulate between blocks.
		RealtimeHostAudioQueue m_schedHostAudio;
		std::atomic<uint64_t> m_schedHostAudioOverflow{0};
		std::atomic<uint64_t> m_schedHostAudioUnderrun{0};
		// Pair worker: the date of each codec frame the mixer emits, the codec
		// clock's tick that sent it, in the order of its ESSI1 output ring
		// (onEssiCallbackMixer, schedDrainCodecOutput).
		dsp56k::RingBuffer<uint64_t, dsp56k::Audio::RingBufferSize, false, false> m_codecFrameCycles;
		bool     m_schedHostAudioActive = false;	// retain drained frames for a host callback
		bool     m_schedBoundedJit = true;		// cycle-bounded DSP background slices
		TransportMode m_transportMode = TransportMode::Serial;
		// Threaded-transport state. m_dspThreaded flips once at handoff and
		// never back. The worker runs the producer alone (Parallel) or both
		// DSPs (Pair, m_dspPairWorker).
		std::array<std::atomic<bool>, 2> m_dspThreaded{};
		alignas(64) std::atomic<bool> m_dspPairWorker{false};
		// Pair worker: a DSP whose gate MM backpressure holds shut. The UC's
		// waits on that DSP give way to it, as the serial catch-up stopped at
		// the backpressure threshold instead of running the DSP to the UC.
		alignas(64) std::array<std::atomic<bool>, 2> m_pairBackpressured{};
		// Pair worker only: UC cycle + 1 at which a DSP's backpressure park began.
		std::array<uint64_t, 2> m_pairBpSince{};
		// Gate constants of the pair mode, fixed at the handoff: the gates are
		// evaluated in the worker's wait loop.
		double   m_pairQuantumFrames = 0.0;		// block-target slack
		double   m_pairDspMinCache = 0.0;		// audio thread: last read minimum of the unheld DSPs, frames
		// Audio thread waiting on the pair worker (any transport wait): the
		// worker runs even the short chunks it otherwise gathers into longer ones.
		alignas(64) std::atomic<uint32_t> m_pairUrgent{0};
		uint64_t m_pairMinChunkCycles = 2304 / 2;	// half a codec frame of DSP cycles (MD_PAIR_MIN_CHUNK)
		uint64_t m_pairWorkerAffinity = 0;			// logical CPU mask of the pair worker, 0 = free (MDMM_PAIR_AFFINITY)
		std::atomic<bool> m_pairPlacementAllowed{false};	// the scheduler runs on a thread the device owns
		uint64_t m_pairDspLeadUc = 0;			// DSP lead over the UC, in UC cycles (MD_PAIR_LEAD_US)
		double   m_pairUcLeadFrames = 0.0;		// UC lead over the slower DSP (MD_PAIR_UC_LEAD_US)
		bool     m_pairHoldDsp2 = false;		// hold both DSPs at the UC during DSP2's round trips (MD_PAIR_HOLD_DSP2_US)
		uint64_t m_pairHoldDsp2Uc = 0;			// hold window after the UC takes a DSP2 word, in UC cycles
		size_t   m_pairBpThreshold = 0;
		uint64_t m_pairBpRelease = 0;
		alignas(64) std::atomic<bool> m_producerParked{false};
		// Right to execute the producer DSP. The worker holds it for each
		// chunk. The audio thread only try-locks it, to run chunks itself
		// when it waits on a producer that is not being scheduled (all cores
		// busy): the pair then degrades to the serial cost instead of
		// stalling behind the OS scheduler.
		alignas(64) std::mutex m_producerExec;
		uint32_t m_producerHelpDelayUs = 50;	// MDMM_PRODUCER_HELP_US, 0 disables helping
		bool m_audioHelpsProducer = false;		// audio thread only: helped on its last wait
		alignas(64) std::atomic<uint64_t> m_producerHelpedChunks{0};
		alignas(64) std::atomic<bool> m_workerExit{false};
		alignas(64) std::atomic<uint64_t> m_schedTargetFrames{0};	// published block target (whole frames)
		std::array<std::atomic<bool>, 2> m_hostWriteBlocked{};	// see setHostWriteBlocked
		std::array<std::atomic<uint32_t>, 2> m_hostWriteBlockEpisode{};	// bumped by every new blocked write
		std::array<std::atomic<uint64_t>, 5> m_transportWaitClamps{};	// expired bounded waits per site
		bool m_transportTrace = false;			// MDMM_TRANSPORT_TRACE: stderr progress from both sides
		uint64_t m_schedStepCount = 0;
		// Trace-only watchdog: where the audio thread is (phase/site) and
		// what the worker does, printed every 2 s from a helper thread so a
		// stall is visible without a debugger.
		alignas(64) std::atomic<int32_t> m_audioPhase{0};	// 0 idle, 1 UC slice, 2 mixer slice, 10+site while waiting
		alignas(64) std::atomic<uint64_t> m_workerChunks{0};
		// Trace-only wall-time accounting (nanoseconds), printed as per-interval
		// shares by the watchdog: where each thread's wall time goes.
		struct TransportTimeTrace
		{
			std::atomic<uint64_t> ucSliceNs{0};		// UC slices, including waits issued inside them
			std::atomic<uint64_t> mixerSliceNs{0};	// mixer slices, wherever they run
			std::array<std::atomic<uint64_t>, 5> blockedNs{};	// audio thread parked on the signal, per site
			std::array<std::atomic<uint64_t>, 5> waits{};		// waitTransport calls that had to wait
			std::array<std::atomic<uint64_t>, 5> timedOutRounds{};	// signal waits that ran to their timeout
			alignas(64) std::atomic<uint64_t> workerExecNs{0};
			std::atomic<uint64_t> workerParkNs{0};
			std::atomic<uint64_t> workerParks{0};
			std::atomic<uint64_t> workerParkTimeouts{0};	// park rounds that ran to their timeout
			std::atomic<uint64_t> ucSliceCycles{0};		// UC cycles advanced by UC slices
			std::atomic<uint64_t> mixerSlices{0};
			std::atomic<uint64_t> mixerSliceCycles{0};
			std::atomic<uint64_t> producerSliceNs{0};	// serial-mode producer slices
			std::atomic<uint64_t> producerSliceCycles{0};
			alignas(64) std::atomic<uint64_t> workerExecCycles{0};
			std::atomic<uint64_t> ucSlices{0};
		};
		alignas(64) TransportTimeTrace m_timeTrace;
		// Trace-only accounting of the pair transport: where each of the two
		// threads' wall time goes, and why the worker stops.
		enum class PairIdle : uint32_t { UcGate, BlockTarget, Backpressure, Gathering, Count };
		struct PairTrace
		{
			alignas(64) std::atomic<uint64_t> chunks{0};
			std::atomic<uint64_t> chunkNs{0};			// inside runPairChunk
			std::atomic<uint64_t> chunkCycles{0};		// DSP cycles advanced by chunks (both DSPs)
			std::atomic<uint64_t> turnNs{0};			// worker between chunks: services and gates
			std::array<std::atomic<uint64_t>, static_cast<size_t>(PairIdle::Count)> idles{};
			std::array<std::atomic<uint64_t>, static_cast<size_t>(PairIdle::Count)> idleNs{};
			alignas(64) std::atomic<uint64_t> leadWaits{0};		// UC at its lead over the slower DSP
			std::atomic<uint64_t> leadWaitNs{0};
			std::atomic<uint64_t> dspTimeWaits{0};		// exact reads (waitForDspTime)
			std::atomic<uint64_t> dspTimeWaitNs{0};
			std::atomic<uint64_t> mixerWaits{0};		// block end, codec frames
			std::atomic<uint64_t> mixerWaitNs{0};
		};
		PairTrace m_pairTrace;
		// Pair worker: why a DSP cannot run now (for m_pairTrace).
		PairIdle pairIdleReason(uint32_t _dspIndex, uint64_t _gate);
		std::atomic<bool> m_watchdogExit{false};
		std::thread m_watchdog;
		void startWatchdog();
		// Published producer position in machine frames (threaded producer);
		// the mixer's slices are capped at this + L_lead (spec §3, gate 2).
		double producerPublishedFrames() const;
		alignas(64) TransportSignal m_signal;
		std::thread m_producerWorker;
		std::array<RealtimeHostAudioInputTimeline, 2> m_hostAudioInput;
		std::array<int64_t, 2> m_hostAudioInputClockOrigin{};
		std::array<uint64_t, 2> m_hostAudioInputNextRxIndex{};
		// Receiver-context clock bookkeeping re-originates whenever the audio
		// thread republishes the input latency (generation bump), so the
		// audio thread never writes receiver-owned state.
		std::array<uint64_t, 2> m_hostAudioInputClockGeneration{};
		std::atomic<uint64_t> m_hostAudioInputGeneration{1};
		std::atomic<bool> m_hostAudioInputHasSource{false};
		static constexpr uint32_t g_hostAudioInputLatencyUnset = 0xffffffffu;
		std::atomic<uint32_t> m_hostAudioInputLatencyPublished{g_hostAudioInputLatencyUnset};
		// Counts are receiver-frame events; aggregate accessors sum both DSPs.
		std::array<std::atomic<uint64_t>, 2> m_hostAudioInputUnderflow{};
		std::array<std::atomic<uint64_t>, 2> m_hostAudioInputOverflow{};
		// Trace-only receiver bookkeeping (MDMM_TRANSPORT_TRACE), printed by the
		// watchdog: how each receiver's reads resolve against its timeline.
		struct alignas(64) HostAudioInputTrace
		{
			std::atomic<uint64_t> calls{0};		// codec receive callbacks
			std::atomic<uint64_t> gated{0};		// no source, latency unset or origin not latched
			std::atomic<uint64_t> hits{0};
			std::atomic<uint64_t> behind{0};	// requested frame older than the timeline head
			std::atomic<uint64_t> ahead{0};		// requested frame not appended yet
			std::atomic<uint64_t> beforeStart{0};
			std::atomic<uint64_t> reorigins{0};
			std::atomic<int64_t> lastSample{0};
			std::atomic<int64_t> lastOrigin{0};
		};
		std::array<HostAudioInputTrace, 2> m_hostAudioInputTrace;
		synthLib::TAudioInputs m_hostAudioInputSource{};
		uint32_t m_hostAudioInputSourceFrames = 0;
		uint32_t m_hostAudioInputSourceCursor = 0;
		uint32_t m_hostAudioInputLatency = 0;
		bool m_hostAudioInputLatencyInitialized = false;
		bool     m_schedInLinkDelivery = false;	// reentrancy guard for cross-DSP catch-up
		double   m_schedFramesTotal   = 0.0;	// machine-time target, accumulated codec frames
		alignas(64) uint64_t m_schedUcCyclesDone  = 0;		// UC cycles executed under the scheduler (processUC)
		uint64_t m_mmBpSinceUcCycles[2] = {0,0};// MM backpressure: UC cycle+1 when a DSP's stall began (0 = none)
		// MD link-transport state grouped by owning execution context and
		// promoted to atomics (parallel-transport spec, memory-visibility
		// pass). The serial scheduler runs every context on one thread, so
		// the acquire/release pairs cost nothing and change no behavior;
		// the grouping records which worker owns each field once the
		// transport goes parallel. The MM equivalents below were atomic
		// already.
		struct MdMixerLinkState				// written in mixer (DSP1) context
		{
			std::atomic<bool> roeEngaged{false};	// latched at the first DMA4 receive window
			std::atomic<bool> awaitFresh{false};	// waits for DSP2's first word in a receive window
			std::atomic<bool> rendezvousArmPending{false};
			std::atomic<bool> rendezvousActive{false};
			std::atomic<uint64_t> flushEpoch{0};
		};
		// Port C mailbox: the mixer's Port C write pends an edge, the
		// producer's hostInputSource releases it against the DMA4 window.
		struct MdPortCMailbox
		{
			std::atomic<bool> pending{false};
			std::atomic<dsp56k::TWord> visible{0};
			std::atomic<dsp56k::TWord> pendingLevel{0};
			std::atomic<uint64_t> pendingEpoch{0};
			std::atomic<uint64_t> releaseEpoch{0};
		};
		alignas(64) MdMixerLinkState m_mdLink;
		alignas(64) MdPortCMailbox m_mdPortC;
		// Published machine positions (parallel-transport spec §2): one
		// release-store per scheduler slice. Serial mode only publishes;
		// consumers appear with the parallel transport.
		struct alignas(64) PaddedPosition : std::atomic<uint64_t>
		{
			PaddedPosition() : std::atomic<uint64_t>(0) {}
		};
		struct PublishedPositions
		{
			PaddedPosition ucCycles;
			std::array<PaddedPosition, 2> dspCycles{};
		};
		PublishedPositions m_schedPublished;
		std::atomic<bool> m_mmLinkAwaitFresh{false};	// PDRC edge awaits DSP2's DMA reply
		std::atomic<uint64_t> m_mmLinkStrobeEpoch{0};	// cancels delivery after nested catch-up
		uint32_t m_mmLinkStrobeLevel = 2;		// mixer-context edge detector; 2 = no level observed yet
		bool     m_schedDspOriginLatched[2] = { false, false };	// [0]=mixer/DSP1, [1]=producer/DSP2
		double   m_schedDspOriginFrame [2]  = { 0.0, 0.0 };		// machine-frame at runnable transition
		uint64_t m_schedDspOriginCycles[2]  = { 0, 0 };			// getCycles() at that transition
		uint64_t m_schedDspOriginUcCycles[2] = { 0, 0 };		// exact host clock at that transition
		alignas(64) std::atomic<bool> m_schedulerHostPumpDirty{true};
		alignas(64) uint8_t m_padAfterPumpDirty = 0;
		// MIDI
		void pumpScheduledMidi();
		std::atomic<uint64_t> m_midiOutputNativeOrigin{0};
		ScheduledMidiQueue<16384> m_scheduledMidi;
		std::atomic<uint64_t> m_scheduledMidiOverflow{0};
		dsp56k::RingBuffer<synthLib::SMidiEvent, 16384, true> m_midiIn;
		size_t m_midiInByteCursor = 0;
		RealtimeMidiByteQueue<64> m_realtimeMidiIn;

		// Panel input events pending delivery to UART2 RX.
		PanelInputQueue m_panelIn;

	};
}
