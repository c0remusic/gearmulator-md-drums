#include "mdhardware.h"
#include "mdenv.h"
#include "mdhostclock.h"
#include "mdrampacking.h"
#include "mdtransportpolicy.h"

#include "mdsysexautomation.h"
#include "synthLib/realtimeInstrumentation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

#include "mdromloader.h"
#include "mdtypes.h"

#include "dsp56kEmu/jitblockinfo.h"
#include "dsp56kBase/threadtools.h"

#if MD_TRANSPORT_DIAGNOSTICS
#define MD_TRANSPORT_RECORD(...) do { __VA_ARGS__; } while(false)
#else
#define MD_TRANSPORT_RECORD(...) do {} while(false)
#endif

namespace md
{
	// The current 40 MHz ColdFire clock is consistent with the firmware's timer
	// and UART divisors. It converts mixer-DSP execution into a UC cycle budget.

	// One codec (ESSI1) stereo frame corresponds to a fixed number of DSP1-executed cycles. The
	// firmware configures a 96-cycle base link slot; the ESSI1 divider and two stereo slots produce
	// 2304 cycles per codec frame at the 101.6064 MHz DSP clock.
	// The UC is granted g_ucClockHz/44100, about 907.03 cycles per frame.
	constexpr uint64_t g_dsp1CyclesPerEsaiFrame  = 2304;

	// Pair mode: a block takes the mixer's frames dated up to this many frames
	// before its target (schedDrainCodecOutput), 45 us of latency. Taking
	// whatever the worker had emitted made the count depend on how far ahead
	// it ran: with the worker right at the target, the block's last frame
	// could still be missing and the block came out one frame short (GND SIN
	// clicks). A frame reaches the ring up to 0.7 frame after its date.
	constexpr uint64_t g_pairCodecCutFrames = 2;

	Rom initRom(const std::vector<uint8_t>& _romData, const std::string& _romName,
		const MachineModel _model)
	{
		if(_romData.empty())
			return RomLoader::findROM(_model);
		Rom rom(_romData, _romName);
		if(rom.isValid() && RomLoader::isRomForModel(rom.data(), _model))
			return rom;
		return RomLoader::findROM(_model);
	}

	uint64_t fingerprintRom(const std::vector<uint8_t>& _data)
	{
		uint64_t result = 14695981039346656037ull;
		for(const auto byte : _data)
		{
			result ^= byte;
			result *= 1099511628211ull;
		}
		return result;
	}

	dsp56k::TWord hostAudioInputSample(const synthLib::TAudioInputs& _inputs,
		const uint32_t _frames, const uint32_t _cursor, const size_t _channel)
	{
		if(_channel >= 2 || _cursor >= _frames || !_inputs[_channel])
			return 0;
		return dsp56k::sample2dsp(_inputs[_channel][_cursor]);
	}

	Hardware::Hardware(const std::vector<uint8_t>& _romData,
		const std::string& _romName, const MachineModel _model,
		const std::vector<uint8_t>& _initialPatchRam,
		std::shared_ptr<FrontPanelPublisher> _frontPanelPublisher,
		const std::vector<uint8_t>& _initialFlash,
		const std::vector<uint8_t>& _factoryFlashCache,
		const FlashSectorOverlay& _pendingFlashOverlay)
		: Hardware(_romData, _romName, _model, _initialPatchRam,
			std::move(_frontPanelPublisher), _initialFlash, _factoryFlashCache,
			_pendingFlashOverlay, {})
	{
	}

	Hardware::Hardware(const std::vector<uint8_t>& _romData, const std::string& _romName,
		const MachineModel _model, const std::vector<uint8_t>& _initialPatchRam,
		std::shared_ptr<FrontPanelPublisher> _frontPanelPublisher,
		const std::vector<uint8_t>& _initialFlash,
		const std::vector<uint8_t>& _factoryFlashCache,
		const FlashSectorOverlay& _pendingFlashOverlay,
		const std::vector<uint8_t>& _initialUserFlash)
		: m_model(_model)
		, m_rom(initRom(_romData, _romName, _model))
		, m_firmwareFingerprint(fingerprintRom(m_rom.data()))
		, m_factoryFlashInitializationExpected(_model == MachineModel::Machinedrum
			&& _initialFlash.empty() && _factoryFlashCache.empty())
		, m_uc(m_rom, m_model,
			_pendingFlashOverlay.valid ? std::vector<uint8_t>{} : _initialPatchRam,
			_initialFlash, _initialUserFlash)
		// A complete project image can boot directly without a local factory cache,
		// but it must never become the machine-local factory baseline itself.
		, m_externalInteraction(_model == MachineModel::Machinedrum
			&& !_initialFlash.empty() && _factoryFlashCache.empty()
			&& !_pendingFlashOverlay.valid)
		, m_factoryFlashReady(!_factoryFlashCache.empty())
		, m_factoryFlashPreparationReady(!_factoryFlashCache.empty()
			|| !_initialFlash.empty())
		, m_factoryFlashCache(_factoryFlashCache)
		, m_factoryFlashBaseline(_model == MachineModel::Machinedrum
			&& _factoryFlashCache.empty() ? g_romSize : 0)
		, m_pendingFlashImage(_pendingFlashOverlay.valid ? g_romSize : 0)
		, m_pendingFlashOverlay(_pendingFlashOverlay)
		, m_pendingPatchRam(_pendingFlashOverlay.valid
			? _initialPatchRam : std::vector<uint8_t>{})
		, m_pendingFlashRestoreActive(_pendingFlashOverlay.valid)
		, m_frontPanelPublisher(_frontPanelPublisher
			? std::move(_frontPanelPublisher)
			: std::make_shared<FrontPanelPublisher>())
		, m_midiSysexTransfer(g_ucClockHz)
		, m_dspMixer(*this, m_uc.getHdi08Dsp1(), 0)		// DSP1, mixer/main
		, m_dspProducer(*this, m_uc.getHdi08Dsp2(), 1)	// DSP2, producer
	{
		// Ship the validated bounded dispatcher by default while retaining the
		// established path as a field fallback and exact A/B control.
		const auto* const boundedJit = std::getenv("GEARMULATOR_MDMM_BOUNDED_JIT");
		m_schedBoundedJit = boundedJit == nullptr || std::strcmp(boundedJit, "0") != 0;
		m_linkPipelineDepthFrames = transportPolicy(m_model).linkPipelineDepthFrames;
		if(const auto depth = envNumber("MD_LINK_PIPELINE_DEPTH"))
			m_linkPipelineDepthFrames = std::max(0.0, *depth);
		if(const auto mode = parseTransportMode(std::getenv("MDMM_TRANSPORT")))
			m_transportMode = *mode;
		if(const auto help = envCount<uint32_t>("MDMM_PRODUCER_HELP_US"))
			m_producerHelpDelayUs = *help;
		m_transportTrace = envOverride("MDMM_TRANSPORT_TRACE") != nullptr;
		if(m_transportTrace)
			startWatchdog();

		if(!m_rom.isValid())
			return;
		m_uc.setMidiTransmitTap([this](const uint8_t _byte)
		{
			m_midiSysexTransfer.observeTransmitByte(_byte);
		});
		m_mdLink.rendezvousArmPending.store(!isMonomachine()
			&& m_firmwareFingerprint == g_mdOs163Fingerprint,
			std::memory_order_release);

		// Wake the scheduler host pump when either DSP produces a host word or
		// the UC-side port state changes. The pump itself runs only on a wake
		// (see pumpDsp2HostRequest), so an idle UC step skips both drains and
		// the HREQ recomputation entirely.
		const auto wake = [this] { notifyHostPumpStateChanged(); };
		m_dspMixer.setHostPumpWakeCallback(wake);
		m_dspProducer.setHostPumpWakeCallback(wake);

		// Feed the OS's host->panel UART2 stream into the front-panel LCD/LED decoder.
		m_uc.setFrontPanel(&m_frontPanel);
		setFrontPanelPublisher(m_frontPanelPublisher);

		// Inter-DSP ESSI0 link. Each DSP's ESSI0 TX pushes a dated entry into
		// the OTHER DSP's TimedLinkRing (32768 deep, non-blocking); each
		// DSP's ESSI0 RX pops its OWN ring via the read callback. A consumer
		// NEVER reads fabricated silence - the hardware-true skip-on-empty
		// link RX keeps consumption equal to production. The rings are
		// prefilled below with 64 empty entries so neither side of the
		// FULL-DUPLEX link starves at startup, and execTX runs before execRX
		// each slot (esaiclock), so each DSP feeds its neighbour before it
		// can starve on its own RX - no deadlock, provided the two ESSI0
		// clock rates match (they do: same divider config on both DSPs).
		// Codec ESSI1 RX is callback-fed and remains non-blocking:
		// out-of-block reads receive silence.
		for(auto& ring : m_linkRing)
			for(uint32_t i = 0; i < 64; ++i)
				ring.push_back({});
		// Both DSPs run on one scheduler thread, so a blocking ring push/pop
		// (which parks the calling thread until the peer thread drains/fills) would deadlock. Under
		// the scheduler the inter-DSP link ring is non-blocking: drop-on-full for the producer
		// push, silence-on-empty for the consumer pop. Ordering/level correctness comes from the
		// scheduler advancing the peer before delivery and, when it lands, the hardware-true
		// skip-on-empty link RX.
		// The word store is the dated TimedLinkRing (parallel-transport spec
		// §4); every drop gate still runs at this push site, the dating
		// fields are captured for the later pop-side relocation.
		const auto pushToInput = [this](const uint32_t _selfDsp)
		{
			return [this, _selfDsp](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _values)
			{
				MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp].transmitFrames;);
				auto& producerDsp = (_selfDsp == 0) ? m_dspMixer : m_dspProducer;
				TimedLinkEntry entry;
				entry.fromTx(_values);
				entry.producerCycles = producerDsp.dsp().getCycles();
				entry.fresh = producerDsp.getPeriph().getEssi0()
					.getLastTxWrittenMask() != 0;
				// Content offset applies to the producer->mixer direction only;
				// the back-channel is causal. Boot traffic (origin not latched
				// yet) stays undated. The catch-up below reuses the producer's
				// position: nothing runs in between.
				std::optional<double> producerPos;
				if(_selfDsp == 1 && m_schedDspOriginLatched[1])
					producerPos = schedDspFramePos(1);
				entry.dueFrames = producerPos ? *producerPos + m_linkPipelineDepthFrames : 0.0;
				auto& ring = m_linkRing[1u - _selfDsp];
				const bool mdProducerToMixer = _selfDsp == 1 && !isMonomachine();
				const bool rendezvousActiveBefore = mdProducerToMixer
					&& m_mdLink.rendezvousActive.load(std::memory_order_acquire);
				const uint64_t flushEpochBefore = mdProducerToMixer
					? m_mdLink.flushEpoch.load(std::memory_order_acquire) : 0;
					// The legacy delivery path advances the consumer (the DSP whose input ring this
					// is, index 1-_selfDsp) to the producer's current machine time BEFORE enqueueing, so
					// the frame lands at the right point in the consumer's timeline (edge-preserving,
					// no stale/early consumption). Then enqueue non-blocking (drop-on-full; the ring
					// stays shallow because the consumer was just caught up). Gated to the post-boot
					// audio phase so it can never perturb the loader handshake (see the floor const).
					// The on-demand path below deliberately pre-enqueues its wire edge.
					// A serial wire has no memory: a word clocked out while the
					// consumer's receiver is disabled is gone on real hardware.
					// The consumer applies that rule in its own context (see
					// linkDisposeAtConsumer: everything queued before its
					// receiver enables dies at that edge), so the producer
					// never reads the peer's control register here.
					// The MD link is a synchronous on-demand wire. Once the
					// request edge has been released for this DMA4 window, put
					// each fresh word on the receiver wire before advancing DSP1 to the
					// producer's timestamp. Rejected/stale callbacks still advance time but
					// never become a later wire edge.
					if(rendezvousActiveBefore)
					{
						const bool releasedForWindow =
							!m_mdPortC.pending.load(std::memory_order_acquire)
							&& m_mdPortC.releaseEpoch.load(std::memory_order_acquire)
								== flushEpochBefore;
						// Mixer DMA4 window state via the mirror: producer context.
						const bool dma4Active = dmaEnabled(0, 4);
						if(!entry.fresh)
							MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp]
								.mdRendezvousRetainedDrops;);
						else if(!releasedForWindow)
							MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp]
								.mdRendezvousUnreleasedDrops;);
						else if(!dma4Active)
							MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp]
								.mdRendezvousDmaInactiveDrops;);
						else if(ring.full())
							MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp]
								.mdRendezvousRingFullDrops;);
						else
						{
							entry.epoch = flushEpochBefore;
							ring.push_back(entry);
							MD_TRANSPORT_RECORD(auto& score = m_transportScorecard.link[_selfDsp];
								++score.acceptedFrames;
								score.currentRingDepth = ring.size();
								score.maximumRingDepth = std::max(score.maximumRingDepth, ring.size()););
						}
						if(dspsShareThread())
							schedCatchUpDspToDsp(1u - _selfDsp, _selfDsp, producerPos);
						++_frameIndex;
						return;
					}

					// Typed early-link-catch-up construction selects floor 0; otherwise the post-boot
					// gate is 256. Floor 0 runs consumer catch-up during the boot window.
					const uint64_t strobeEpoch = (isMonomachine() && _selfDsp == 1)
						? m_mmLinkStrobeEpoch.load(std::memory_order_acquire) : 0;
					// With an explicit floor, fire when esaiFrameIndex >= floor (floor=0 => ALWAYS,
					// including the boot window). Default keeps the strict post-boot gate (> 256).
					// Rendezvous (catch-up-before-delivery): advance the CONSUMER toward the producer's
					// shared-clock time before delivery. Left under the existing floor control - per-word
					// when the floor is 0, per-interaction when raised. The 2048-deep transport
					// below absorbs a slice's burst, so per-word catch-up is NOT required for throughput;
					// the floor controls transport fidelity versus rendezvous tightness.
					// With the producer alone on its worker neither DSP runs the
					// other inline: the dated ring and the pop-side wait replace
					// the rendezvous. Two DSPs sharing a thread keep it.
					if(dspsShareThread())
						schedCatchUpDspToDsp(1u - _selfDsp, _selfDsp, producerPos);

					// Every gate that used to read CONSUMER state here (receive
					// window epochs, receiver-overrun on RDF, post-flush
					// retained disposal) now runs in the consumer's own
					// context: see linkDisposeAtConsumer(). The entry carries
					// what those gates need (stamp, epoch, fresh).
					if(!ring.full())
					{
						entry.epoch = (isMonomachine() && _selfDsp == 1)
							? strobeEpoch : flushEpochBefore;
						ring.push_back(entry);
						MD_TRANSPORT_RECORD(auto& score = m_transportScorecard.link[_selfDsp];
							++score.acceptedFrames;
							score.currentRingDepth = ring.size();
							score.maximumRingDepth = std::max(score.maximumRingDepth, ring.size()););
					}
					else
						MD_TRANSPORT_RECORD(++m_transportScorecard.link[_selfDsp].ringFullDrops;);
				++_frameIndex;
			};
		};

		const auto blockingPop = [this](const uint32_t _selfDsp)
		{
			return [this, _selfDsp](uint64_t& _frameIndex, dsp56k::Audio::RxFrame& _frame)
			{
				MD_TRANSPORT_RECORD(++m_transportScorecard.link[1u - _selfDsp]
					.receiveCallbacks;);
				// The mixer path normally disposes in the availability
				// callback; this covers consumers polled without one. Never
				// an RX-tick ROE site: the availability probe owns that.
				// When that probe ran in this very slot (linkRxAvailable),
				// disposing again would change nothing: take its position.
				auto& tick = m_linkRxTick[_selfDsp].value;
				double now;
				if(tick.cycles == ((_selfDsp == 0) ? m_dspMixer : m_dspProducer).dsp().getCycles())
				{
					now = tick.now;
				}
				else
				{
					linkDisposeAtConsumer(_selfDsp, false);
					now = linkConsumerNow(_selfDsp);
				}
				tick.cycles = ~0ull;
				auto& ring = m_linkRing[_selfDsp];
					// Stall recovery: under scheduler link catch-up the consumer is
					// advanced to the producer's time before every enqueue, so this ring can only be
					// DEEP if the consumer's RX stopped clocking for a while as the wire kept running
					// (constructor prefill, receive-enable transient, or ESSI reconfiguration).
					// Real hardware loses stalled words by receiver overrun and
					// resumes at the CURRENT stream position; a deep ring instead replays the stall
					// backlog forever. Purge only well beyond normal lockstep variation.
					// A raw depth>16 check cannot tell the two apart, because the MM's flow-controlled
					// link legitimately bursts. The distinguishing behavior is that a stall residue never drains
					// (the offset is permanent), while a burst drains to empty before the next strobe.
					// So purge only when the ring's MINIMUM depth over a window of pops stays deep.
					{
						// The MD streams in word lockstep, while the MM uses flow-controlled
						// bursts. For MM,
						// purge only rings that have not been shallow for >1024 codec frames (a burst
						// is shallow again within ~1 block period; a genuine backlog is not). The
						// active MD rendezvous instead carries future edges directly and bypasses
						// this legacy purge; strict skip-on-empty RX supplies the hardware boundary.
						const bool s_immediate = !isMonomachine();
						const bool preserveRendezvousFutureEdges =
							m_mdLink.rendezvousActive.load(std::memory_order_acquire)
							&& !isMonomachine();
						const uint64_t esaiNow =
							m_esaiFrameIndex.load(std::memory_order_acquire);
						auto& lastShallow = m_linkLastShallow[_selfDsp].value;
						// Depth as the wire sees it: words already due. The
						// in-flight (future-dated) lead is invisible to stall
						// recovery - a stall residue is made of PAST words.
						size_t dueDepth = 0;
						while(dueDepth < ring.size() && dueDepth <= 16
							&& ring[dueDepth].dueFrames <= now)
							++dueDepth;
						if(dueDepth <= 16)
							lastShallow = esaiNow;
						else if(!preserveRendezvousFutureEdges
							&& (s_immediate || esaiNow - lastShallow > 1024))
						{
							size_t purged = 0;
							while(!ring.empty() && ring.front().dueFrames <= now)
							{
								ring.pop_front();
								++purged;
							}
							MD_TRANSPORT_RECORD(m_transportScorecard.link[1u - _selfDsp]
								.stallPurgedFrames += purged;
								m_transportScorecard.link[1u - _selfDsp].currentRingDepth = ring.size(););
							(void)purged;
							lastShallow = esaiNow;	// due backlog gone (shallow)
						}
					}
					// Non-blocking on the single scheduler thread: silence on empty rather than park.
					// Hardware-true skip-on-empty link receive replaces this with matched consumption
					// and production once the frame has landed. A head that is
					// not due yet counts as empty: nothing has arrived.
					if(ring.empty() || ring.front().dueFrames > now)
					{
						_frame.clear();
						MD_TRANSPORT_RECORD(++m_transportScorecard.link[1u - _selfDsp].emptyReads;);
					}
					else
					{
						ring.pop_front().toRx(_frame);
						MD_TRANSPORT_RECORD(auto& score = m_transportScorecard.link[1u - _selfDsp];
							++score.poppedFrames;
							score.currentRingDepth = ring.size(););
					}
				++_frameIndex;
			};
		};

		const auto codecInput = [this](const size_t _dspIndex)
		{
			return [this, _dspIndex](uint64_t& _frameIndex,
				dsp56k::Audio::RxFrame& _frame)
			{
				RealtimeHostAudioInputQueue::Frame input{};
				auto& queue = m_hostAudioInput[_dspIndex];
				// Receiver context (possibly a worker thread): only published
				// atomics of the audio thread are read here.
				const bool hasSource = m_hostAudioInputHasSource.load(std::memory_order_acquire);
				const auto latency = m_hostAudioInputLatencyPublished.load(std::memory_order_acquire);
				const auto generation = m_hostAudioInputGeneration.load(std::memory_order_acquire);
				auto& trace = m_hostAudioInputTrace[_dspIndex];
				if(m_transportTrace)
					trace.calls.fetch_add(1, std::memory_order_relaxed);
				if((hasSource || queue.size()!=0) && latency != g_hostAudioInputLatencyUnset
					&& m_schedDspOriginLatched[_dspIndex])
				{
					if(m_hostAudioInputClockGeneration[_dspIndex] != generation
						|| _frameIndex != m_hostAudioInputNextRxIndex[_dspIndex])
					{
						m_hostAudioInputClockOrigin[_dspIndex] = static_cast<int64_t>(schedDspFramePos(
							static_cast<uint32_t>(_dspIndex))) - static_cast<int64_t>(_frameIndex);
						m_hostAudioInputClockGeneration[_dspIndex] = generation;
						if(m_transportTrace)
						{
							trace.reorigins.fetch_add(1, std::memory_order_relaxed);
							trace.lastOrigin.store(m_hostAudioInputClockOrigin[_dspIndex], std::memory_order_relaxed);
						}
					}
					const auto sample = m_hostAudioInputClockOrigin[_dspIndex]
						+ static_cast<int64_t>(_frameIndex) - static_cast<int64_t>(latency);
					const bool hit = queue.readAt(sample,input);
					if(!hit && hasSource && !queue.beforeStart(sample))
					{
						m_hostAudioInputUnderflow[_dspIndex].fetch_add(1, std::memory_order_relaxed);
						if(m_transportTrace)
							std::fprintf(stderr, "[adc%zu] underflow sample=%lld dspFrames=%.1f target=%llu uc=%llu blocked=%d/%d\n",
								_dspIndex, static_cast<long long>(sample), schedDspFramePos(static_cast<uint32_t>(_dspIndex)),
								static_cast<unsigned long long>(m_schedTargetFrames.load()),
								static_cast<unsigned long long>(m_schedPublished.ucCycles.load()),
								m_hostWriteBlocked[0].load() ? 1 : 0, m_hostWriteBlocked[1].load() ? 1 : 0);
					}
					if(m_transportTrace)
					{
						trace.lastSample.store(sample, std::memory_order_relaxed);
						if(hit)
							trace.hits.fetch_add(1, std::memory_order_relaxed);
						else if(queue.beforeStart(sample))
							trace.beforeStart.fetch_add(1, std::memory_order_relaxed);
						else if(queue.size() == 0)
							trace.ahead.fetch_add(1, std::memory_order_relaxed);
						else
							trace.behind.fetch_add(1, std::memory_order_relaxed);
					}
				}
				else if(m_transportTrace)
					trace.gated.fetch_add(1, std::memory_order_relaxed);
				m_hostAudioInputNextRxIndex[_dspIndex] = _frameIndex + 1;
				_frame.resize(2);
				_frame[0] = dsp56k::Audio::RxSlot{input[0]};
				_frame[1] = dsp56k::Audio::RxSlot{input[1]};
				++_frameIndex;
			};
		};

		// ESSI0 inter-DSP ring, full-duplex: DSP2 TX -> DSP1 input and vice versa.
		{
			auto fwd = pushToInput(1);
			m_dspProducer.getPeriph().getEssi0().setWriteTxCallback(
				[this, fwd = std::move(fwd)](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _values)
				{
					// MM PDRC strobe rendezvous: make the request/response boundary atomic in
					// emulated time. Under the coarse single-thread scheduler DSP2 can already
					// be tens of thousands of cycles ahead when DSP1 raises the request, then
					// emit retained-register underruns before its polling loop observes the new
					// level. Real DSPs observe the edge concurrently. Suppress only that
					// scheduler-created prefix; after the first DMA-fed word, normal ESSI
					// underrun/retransmit semantics apply again.
					if(isMonomachine() && m_mmLinkAwaitFresh.load(std::memory_order_acquire))
					{
						// Producer context: the mixer's DMA4 state comes from the
						// mirror, DMA1 is this DSP's own register.
						const bool dma4Active = dmaEnabled(0, 4);
						const bool dma1Active =
							(m_dspProducer.getPeriph().getDMA().getDCR(1) &
								(1u << dsp56k::DmaChannel::De)) != 0;
						const auto writtenMask =
							m_dspProducer.getPeriph().getEssi0().getLastTxWrittenMask();
						if(!dma4Active || !dma1Active || writtenMask == 0)
						{
							MD_TRANSPORT_RECORD(++m_transportScorecard.link[1].transmitFrames;);
							if(!dma4Active)
								MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
									.mmMixerDmaInactiveDrops;);
							else if(!dma1Active)
								MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
									.mmProducerDmaInactiveDrops;);
							else
								MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
									.mmRetainedPrefixDrops;);
							++_frameIndex;
							return;
						}
						m_mmLinkAwaitFresh.store(false, std::memory_order_release);
					}

					fwd(_frameIndex, _values);
				});
		}
		m_dspMixer.getPeriph().getEssi0().setWriteTxCallback(pushToInput(0));
		m_dspMixer.getPeriph().getEssi0().setReadRxCallback(blockingPop(0));
		m_dspProducer.getPeriph().getEssi0().setReadRxCallback(blockingPop(1));
		// The Machinedrum codec ADC bus reaches both DSPs. DSP1 meters it; DSP2
		// consumes it directly for UW RAM recording. Each receiver gets an
		// independent copy so scheduler order cannot steal the peer's frame.
		m_dspMixer.getPeriph().getEssi1().setReadRxCallback(codecInput(0));
		// Both DSPs need the ADC stream. MM tracks 1–3 run on the producer;
		// feeding silence there left their FX THRU machines disconnected.
		m_dspProducer.getPeriph().getEssi1().setReadRxCallback(codecInput(1));

		// Each mixer ESSI1 output frame advances the codec frame counter used by
		// the audio plumbing.
		m_dspMixer.getPeriph().getEssi1().setCallback([this](dsp56k::Audio*){ onEssiCallbackMixer(); });

		// Inter-DSP clock wiring. Each DSP runs the same program, which probes its ESSI1
		// pins (Port D bits 2/3 = SC12 frame sync / SCK1 bit clock, read as GPIO) to decide
		// its role: quiet pins -> "I am the clock master" (ESSI1 TX on, internal clock),
		// running clock -> slave. On the board the mixer (DSP1) drives the codec clock and
		// the producer (DSP2) receives it, so the mixer's inputs stay quiet (nothing sets a
		// host input source -> reads 0) and the producer sees a running clock. The producer's
		// ESSI0 SC01 pin (Port C bit 1) additionally carries the link frame sync the program
		// paces itself against. Earlier firmware disassembly and timing sweeps identified
		// one edge per 147456 DSP cycles, or 128 codec-word periods. The current model
		// forwards DSP1's Port C edge below. Port D remains instruction-counter derived
		// and is evaluated in the reading DSP's execution context.
		{
			const auto& cnt = m_dspProducer.dsp().getInstructionCounter();

			m_dspProducer.getPeriph().getPortD().setHostInputSource([&cnt]() -> dsp56k::TWord
			{
				const auto c = cnt;
				dsp56k::TWord v = 0;
				if((c >> 4) & 1)		v |= (1<<3);	// SCK1: codec bit clock
				if((c / 522) & 1)		v |= (1<<2);	// SC12: codec frame sync (~1x sample rate)
				return v;
			});

			// Port C bit 1 carries block sync. Delay a transition until the mixer
			// opens its corresponding DMA receive window.
			m_dspMixer.getPeriph().getPortC().setCallbackDspWrite([this]
			{
				const dsp56k::TWord level = m_dspMixer.getPeriph().getPortC().hostRead() & (1u << 1);
				if(!isMonomachine()
					&& m_mdLink.rendezvousActive.load(std::memory_order_acquire))
				{
					const auto desiredLevel =
						m_mdPortC.pending.load(std::memory_order_acquire)
						? m_mdPortC.pendingLevel.load(std::memory_order_acquire)
						: m_mdPortC.visible.load(std::memory_order_acquire);
					if(level == desiredLevel)
						return;
					m_mdPortC.pendingLevel.store(level, std::memory_order_release);
					m_mdPortC.pendingEpoch.store(
						m_mdLink.flushEpoch.load(std::memory_order_acquire),
						std::memory_order_release);
					m_mdPortC.pending.store(true, std::memory_order_release);
					return;
				}
				// Both models: the producer reads the pin through the mailbox
				// (its Port C host-input source), never through a cross-context
				// register write.
				m_mdPortC.visible.store(level, std::memory_order_release);
				const uint32_t strobeLevel = level ? 1u : 0u;
				if(isMonomachine() && strobeLevel != m_mmLinkStrobeLevel)
				{
					m_mmLinkStrobeLevel = strobeLevel;
					// Mixer context: own DMA4 register, producer DMA1 via mirror.
					const bool dma4Idle =
						(m_dspMixer.getPeriph().getDMA().getDCR(4) &
							(1u << dsp56k::DmaChannel::De)) == 0;
					const bool dma1Idle = !dmaEnabled(1, 1);
					if(dma4Idle && dma1Idle)
					{
						// The RX register is one word deep, not an archival FIFO.
						// Anything queued before this new request belongs to the
						// completed/idle wire interval and cannot precede DSP2's
						// response in the new DMA4 window.
						auto& ring = m_linkRing[0];
						MD_TRANSPORT_RECORD(m_transportScorecard.link[1]
							.mmStrobePurgedFrames += ring.size();
							m_transportScorecard.link[1].currentRingDepth = 0;);
						while(!ring.empty())
							ring.pop_front();
						m_mmLinkAwaitFresh.store(true, std::memory_order_release);
						m_mmLinkStrobeEpoch.fetch_add(1, std::memory_order_acq_rel);
					}
				}
			});
			// The producer samples the mailbox in its own context. The MD
			// rendezvous arming later replaces this source with the
			// pending/release variant (mdLinkWindowFlushed).
			m_dspProducer.getPeriph().getPortC().setHostInputSource([this]() -> dsp56k::TWord
			{
				return m_mdPortC.visible.load(std::memory_order_acquire);
			});
		}

		// DMA enable mirrors: each DSP publishes its own channels' DE bit
		// from its own context; the transport gates of the OTHER DSP read the
		// mirror (parallel-transport spec §4, cross DCR mirrors).
		for(uint32_t dspIndex = 0; dspIndex < 2; ++dspIndex)
		{
			auto& d = dspIndex == 0 ? m_dspMixer : m_dspProducer;
			d.getPeriph().getDMA().setDeChangedCallback(
				[this, dspIndex](const dsp56k::TWord _channel, const bool _enabled)
			{
				if(_channel < m_dmaEnabled[dspIndex].value.size())
					m_dmaEnabled[dspIndex].value[_channel].store(_enabled, std::memory_order_release);
			});
		}

		MD_TRANSPORT_RECORD(m_transportScorecard.link[0].currentRingDepth =
			m_linkRing[1].size();
		m_transportScorecard.link[1].currentRingDepth =
			m_linkRing[0].size();
		for(auto& score : m_transportScorecard.link)
		{
			score.initialRingDepth = score.currentRingDepth;
			score.maximumRingDepth = score.currentRingDepth;
		});

		// Load SP/PC from the reset vectors before scheduled UC execution starts.
		m_uc.reset();
		m_uc.exec();	// prefetch warm-up (retires nothing; matches the synchronous harness)

	}

	void Hardware::setFrontPanelPublisher(
		std::shared_ptr<FrontPanelPublisher> _publisher)
	{
		if(!_publisher)
			_publisher = std::make_shared<FrontPanelPublisher>();
		m_frontPanelPublisher = std::move(_publisher);
		const auto panelPublisher = m_frontPanelPublisher;
		m_uc.setPanelLedTransitionCallback(
			[panelPublisher](const uint8_t _command, const uint8_t _value,
				const uint64_t _emulationCycles)
			{
				(void)panelPublisher->tryPushLedTransition(
					_command, _value, _emulationCycles);
			});
		(void)m_frontPanelPublisher->tryPublish(m_frontPanel);
	}

	Hardware::~Hardware()
	{
		stopProducerWorker();
		if(m_watchdog.joinable())
		{
			m_watchdogExit.store(true, std::memory_order_release);
			m_watchdog.join();
		}
		m_uc.setMidiTransmitTap({});
	}

	void Hardware::startWatchdog()
	{
		m_watchdog = std::thread([this]
		{
			uint64_t lastChunks = 0, lastSteps = 0;
			// Previous totals of the wall-time accounting, for per-interval shares.
			constexpr size_t timeSlots = 21;
			std::array<uint64_t, timeSlots> lastTime{};
			std::array<uint64_t, 10> lastCost{};
			auto lastWall = std::chrono::steady_clock::now();
			std::array<uint64_t, 18> lastPair{};
			auto lastPairWall = lastWall;
			while(!m_watchdogExit.load(std::memory_order_acquire))
			{
				for(int i = 0; i < 20 && !m_watchdogExit.load(std::memory_order_acquire); ++i)
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				{
					const auto& t = m_timeTrace;
					const std::array<uint64_t, timeSlots> now{
						t.ucSliceNs.load(), t.mixerSliceNs.load(),
						t.blockedNs[0].load(), t.blockedNs[1].load(), t.blockedNs[2].load(),
						t.blockedNs[3].load(), t.blockedNs[4].load(),
						t.waits[0].load(), t.waits[1].load(), t.waits[2].load(), t.waits[3].load(), t.waits[4].load(),
						t.timedOutRounds[0].load(), t.timedOutRounds[1].load(), t.timedOutRounds[2].load(),
						t.timedOutRounds[3].load(), t.timedOutRounds[4].load(),
						t.workerExecNs.load(), t.workerParkNs.load(), t.workerParks.load(), t.workerParkTimeouts.load()};
					const auto wallNow = std::chrono::steady_clock::now();
					const double wallNs = static_cast<double>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(wallNow - lastWall).count());
					const auto pct = [&](const size_t _slot)
					{
						return 100.0 * static_cast<double>(now[_slot] - lastTime[_slot]) / wallNs;
					};
					const auto delta = [&](const size_t _slot)
					{
						return static_cast<unsigned long long>(now[_slot] - lastTime[_slot]);
					};
					std::fprintf(stderr, "[watchdog]   wall%% uc=%.1f mixer=%.1f blocked dsp=%.1f link=%.1f room=%.1f "
						"parked=%.1f mixerGate=%.1f | waits dsp=%llu/%llu link=%llu/%llu room=%llu/%llu "
						"parked=%llu/%llu (timeouts) | worker exec=%.1f park=%.1f parks=%llu parkTimeouts=%llu\n",
						pct(0), pct(1), pct(2), pct(3), pct(4), pct(5), pct(6),
						delta(7), delta(12), delta(8), delta(13), delta(9), delta(14), delta(10), delta(15),
						pct(17), pct(18), delta(19), delta(20));
					lastTime = now;
					lastWall = wallNow;

					// Cost per executed cycle, per component, over the same interval.
					const std::array<uint64_t, 10> cost{
						t.ucSliceNs.load(), t.ucSliceCycles.load(), t.mixerSliceNs.load(), t.mixerSliceCycles.load(),
						t.producerSliceNs.load(), t.producerSliceCycles.load(), t.workerExecNs.load(),
						t.workerExecCycles.load(), t.mixerSlices.load(), t.ucSlices.load()};
					const auto perCycle = [&](const size_t _ns, const size_t _cycles)
					{
						const auto c = cost[_cycles] - lastCost[_cycles];
						return c ? static_cast<double>(cost[_ns] - lastCost[_ns]) / static_cast<double>(c) : 0.0;
					};
					const auto perSlice = [&](const size_t _cycles, const size_t _count)
					{
						const auto n = cost[_count] - lastCost[_count];
						return n ? static_cast<double>(cost[_cycles] - lastCost[_cycles]) / static_cast<double>(n) : 0.0;
					};
					std::fprintf(stderr, "[watchdog]   ns/cycle uc=%.2f mixer=%.2f producerSlices=%.2f worker=%.2f | "
						"slices uc=%llu (%.0f cyc) mixer=%llu (%.0f cyc) | cycles uc=%llu mixer=%llu producer=%llu worker=%llu\n",
						perCycle(0, 1), perCycle(2, 3), perCycle(4, 5), perCycle(6, 7),
						static_cast<unsigned long long>(cost[9] - lastCost[9]), perSlice(1, 9),
						static_cast<unsigned long long>(cost[8] - lastCost[8]), perSlice(3, 8),
						static_cast<unsigned long long>(cost[1] - lastCost[1]),
						static_cast<unsigned long long>(cost[3] - lastCost[3]),
						static_cast<unsigned long long>(cost[5] - lastCost[5]),
						static_cast<unsigned long long>(cost[7] - lastCost[7]));
					lastCost = cost;
				}
				if(m_dspPairWorker.load(std::memory_order_acquire))
				{
					const auto& p = m_pairTrace;
					const std::array<uint64_t, 18> now{
						p.chunks.load(), p.chunkNs.load(), p.chunkCycles.load(), p.turnNs.load(),
						p.idles[0].load(), p.idles[1].load(), p.idles[2].load(), p.idles[3].load(),
						p.idleNs[0].load(), p.idleNs[1].load(), p.idleNs[2].load(), p.idleNs[3].load(),
						p.leadWaits.load(), p.leadWaitNs.load(), p.dspTimeWaits.load(), p.dspTimeWaitNs.load(),
						p.mixerWaits.load(), p.mixerWaitNs.load()};
					const auto wallNow = std::chrono::steady_clock::now();
					const double wallNs = static_cast<double>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(wallNow - lastPairWall).count());
					const auto d = [&](const size_t _k) { return static_cast<double>(now[_k] - lastPair[_k]); };
					const auto pct = [&](const size_t _k) { return 100.0 * d(_k) / wallNs; };
					const auto rate = [&](const size_t _k) { return d(_k) * 1.0e9 / wallNs; };
					std::fprintf(stderr, "[pair] worker chunk=%.1f%% (%.0f/s, %.0f cyc, %.2f ns/cyc) turn=%.1f%% idle "
						"ucGate=%.1f%%(%.0f/s) target=%.1f%%(%.0f/s) bp=%.1f%%(%.0f/s) gather=%.1f%%(%.0f/s) | UC waits "
						"lead=%.1f%%(%.0f/s) dspTime=%.1f%%(%.0f/s) mixer=%.1f%%(%.0f/s)\n",
						pct(1), rate(0), d(0) > 0 ? d(2) / d(0) : 0.0, d(2) > 0 ? d(1) / d(2) : 0.0, pct(3),
						pct(8), rate(4), pct(9), rate(5), pct(10), rate(6), pct(11), rate(7),
						pct(13), rate(12), pct(15), rate(14), pct(17), rate(16));
					lastPair = now;
					lastPairWall = wallNow;
				}
				const auto chunks = m_workerChunks.load(std::memory_order_relaxed);
				const auto steps = m_schedStepCount;
				std::fprintf(stderr, "[watchdog] audioPhase=%d steps=%llu(+%llu) chunks=%llu(+%llu) "
					"threaded=%d parked=%d uc=%.1f mixer=%.1f producer=%.1f target=%llu "
					"clamps dsp=%llu link=%llu room=%llu parked=%llu mixerGate=%llu\n",
					m_audioPhase.load(), static_cast<unsigned long long>(steps),
					static_cast<unsigned long long>(steps - lastSteps),
					static_cast<unsigned long long>(chunks),
					static_cast<unsigned long long>(chunks - lastChunks),
					m_dspThreaded[1].load() ? 1 : 0, m_producerParked.load() ? 1 : 0,
					static_cast<double>(m_schedUcCyclesDone)
						/ (static_cast<double>(g_ucClockHz) / static_cast<double>(g_samplerate)),
					m_schedDspOriginLatched[0] ? schedDspFramePos(0) : -1.0,
					producerPublishedFrames(),
					static_cast<unsigned long long>(m_schedTargetFrames.load()),
					static_cast<unsigned long long>(m_transportWaitClamps[0].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[1].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[2].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[3].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[4].load()));
				if(m_dspThreaded[1].load())
					m_dspProducer.traceHostStream("[watchdog]   producer");
				for(size_t r = 0; r < m_hostAudioInputTrace.size(); ++r)
				{
					const auto& t = m_hostAudioInputTrace[r];
					std::fprintf(stderr, "[watchdog]   adc%zu calls=%llu gated=%llu hits=%llu behind=%llu ahead=%llu "
						"beforeStart=%llu reorigins=%llu lastSample=%lld origin=%lld depth=%zu appendedTo=%.0f "
						"overflow=%llu underflow=%llu\n", r,
						static_cast<unsigned long long>(t.calls.load()), static_cast<unsigned long long>(t.gated.load()),
						static_cast<unsigned long long>(t.hits.load()), static_cast<unsigned long long>(t.behind.load()),
						static_cast<unsigned long long>(t.ahead.load()),
						static_cast<unsigned long long>(t.beforeStart.load()),
						static_cast<unsigned long long>(t.reorigins.load()),
						static_cast<long long>(t.lastSample.load()), static_cast<long long>(t.lastOrigin.load()),
						m_hostAudioInput[r].size(), m_schedFramesTotal,
						static_cast<unsigned long long>(m_hostAudioInputOverflow[r].load()),
						static_cast<unsigned long long>(m_hostAudioInputUnderflow[r].load()));
				}
				lastChunks = chunks;
				lastSteps = steps;
			}
		});
	}

	bool Hardware::isValid() const
	{
		return m_rom.isValid()
			&& !m_pendingFlashRestoreFailed.load(std::memory_order_acquire);
	}

	std::vector<uint8_t> Hardware::copyPatchRam() const
	{
		std::lock_guard lock(m_factoryFlashMutex);
		return m_pendingFlashOverlay.valid ? m_pendingPatchRam : m_uc.copyPatchRam();
	}

	void Hardware::advanceFactoryFlashCapture()
	{
		if(m_model != MachineModel::Machinedrum
			|| m_factoryFlashReady.load(std::memory_order_acquire)
			|| m_pendingFlashRestoreFailed.load(std::memory_order_acquire))
			return;

		constexpr uint64_t minimumAge = g_ucClockHz * 10;
		constexpr uint64_t quietPeriod = g_ucClockHz * 2;
		const bool preparationReady = m_uc.flashDirty()
			&& m_uc.getCycles() >= minimumAge
			&& m_uc.flashIdleCycles() >= quietPeriod;
		if(preparationReady)
			m_factoryFlashPreparationReady.store(true, std::memory_order_release);

		// Interaction makes this boot unsuitable as a reusable machine-local
		// baseline, but it must not strand the firmware at PLEASE REBOOT. The
		// processor can still reboot from the complete project-owned flash image.
		if(m_externalInteraction.load(std::memory_order_acquire))
			return;

		constexpr size_t sliceSize = g_uwFlashSectorSize;
		if(!m_factoryFlashCaptureComplete)
		{
			if(!preparationReady)
			{
				m_factoryFlashCaptureOffset = 0;
				m_factoryFlashCaptureFingerprint = 14695981039346656037ull;
				return;
			}

			const auto remaining = m_factoryFlashBaseline.size()
				- m_factoryFlashCaptureOffset;
			const auto count = std::min(sliceSize, remaining);
			auto* const destination = m_factoryFlashBaseline.data()
				+ m_factoryFlashCaptureOffset;
			if(!m_uc.copyFlashDataRangeRealtime(destination,
				m_factoryFlashCaptureOffset, count))
				return;
			if(m_pendingFlashOverlay.valid)
				std::copy_n(destination, count, m_pendingFlashImage.begin()
					+ m_factoryFlashCaptureOffset);
			for(size_t i = 0; i < count; ++i)
			{
				m_factoryFlashCaptureFingerprint ^= destination[i];
				m_factoryFlashCaptureFingerprint *= 1099511628211ull;
			}
			m_factoryFlashCaptureOffset += count;
			if(m_factoryFlashCaptureOffset != m_factoryFlashBaseline.size())
				return;
			m_factoryFlashCaptureComplete = true;

			if(m_pendingFlashOverlay.valid
				&& m_pendingFlashOverlay.baselineFingerprint
					!= m_factoryFlashCaptureFingerprint
				&& m_pendingFlashOverlay.baselineFingerprint != fingerprintRom(m_rom.data()))
			{
				m_pendingFlashRestoreFailed.store(true, std::memory_order_release);
				m_pendingFlashRestoreActive.store(false, std::memory_order_release);
				m_externalInteraction.store(true, std::memory_order_relaxed);
				std::fprintf(stderr,
					"[MD] project flash does not match the initialized factory baseline\n");
				return;
			}
		}

		if(!m_pendingFlashOverlay.valid)
		{
			m_factoryFlashReady.store(true, std::memory_order_release);
			return;
		}

		if(m_pendingFlashSectorIndex < m_pendingFlashOverlay.sectors.size())
		{
			const auto index = m_pendingFlashSectorIndex++;
			const auto destination = static_cast<size_t>(
				m_pendingFlashOverlay.sectors[index]) * g_uwFlashSectorSize;
			const auto source = index * static_cast<size_t>(g_uwFlashSectorSize);
			std::copy_n(m_pendingFlashOverlay.data.data() + source,
				g_uwFlashSectorSize, m_pendingFlashImage.begin() + destination);
			return;
		}

		// Host snapshots hold this mutex while selecting pending or published state.
		// Never make the scheduler wait for one; retry at the next callback instead.
		std::unique_lock stateLock(m_factoryFlashMutex, std::try_to_lock);
		if(!stateLock.owns_lock())
			return;
		const auto publishResult = m_uc.publishStateImagesRealtime(
			m_pendingFlashImage, m_pendingPatchRam,
			!m_pendingFlashOverlay.data.empty());
		if(publishResult == Microcontroller::StateImagePublishResult::Busy)
			return;
		if(publishResult != Microcontroller::StateImagePublishResult::Published)
		{
			m_pendingFlashRestoreFailed.store(true, std::memory_order_release);
			m_pendingFlashRestoreActive.store(false, std::memory_order_release);
			m_externalInteraction.store(true, std::memory_order_relaxed);
			return;
		}

		// Retain the backing allocations until Hardware destruction; releasing a
		// multi-megabyte overlay or patch image here would move allocator work back
		// onto the audio callback we just made bounded.
		m_pendingFlashOverlay.valid = false;
		m_pendingFlashRestoreActive.store(false, std::memory_order_release);
		m_externalInteraction.store(true, std::memory_order_relaxed);
		m_factoryFlashReady.store(true, std::memory_order_release);
	}

	bool Hardware::factoryFlashCacheReady()
	{
		return m_factoryFlashReady.load(std::memory_order_acquire);
	}

	bool Hardware::copyFactoryFlashBaseline(std::vector<uint8_t>& _baseline)
	{
		FactoryFlashSnapshot snapshot;
		if(!copyFactoryFlashSnapshot(snapshot))
			return false;
		if(!snapshot.baseline.empty())
		{
			_baseline = std::move(snapshot.baseline);
			return true;
		}
		return decodeFactoryFlashCache(_baseline, snapshot.cache, m_rom.data());
	}

	std::vector<uint8_t> Hardware::copyFactoryFlashCache()
	{
		FactoryFlashSnapshot snapshot;
		if(!copyFactoryFlashSnapshot(snapshot))
			return {};
		if(snapshot.cache.empty()
			&& !encodeFactoryFlashCache(snapshot.cache,
				snapshot.baseline, m_rom.data()))
			return {};
		return snapshot.cache;
	}

	bool Hardware::copyFactoryFlashSnapshot(FactoryFlashSnapshot& _snapshot) const
	{
		_snapshot = {};
		if(!m_factoryFlashReady.load(std::memory_order_acquire))
			return false;
		std::lock_guard lock(m_factoryFlashMutex);
		_snapshot.cache = m_factoryFlashCache;
		if(_snapshot.cache.empty())
			_snapshot.baseline = m_factoryFlashBaseline;
		return !_snapshot.cache.empty() || !_snapshot.baseline.empty();
	}

	bool Hardware::copyPendingFlashOverlay(FlashSectorOverlay& _overlay) const
	{
		std::lock_guard lock(m_factoryFlashMutex);
		if(!m_pendingFlashOverlay.valid)
			return false;
		_overlay = m_pendingFlashOverlay;
		return true;
	}

	bool Hardware::replaceFactoryFlashCache(const std::vector<uint8_t>& _cache)
	{
		std::vector<uint8_t> ignored;
		if(_cache.empty() || !decodeFactoryFlashCache(ignored, _cache, m_rom.data()))
			return false;
		std::lock_guard lock(m_factoryFlashMutex);
		m_factoryFlashCache = _cache;
		m_factoryFlashBaseline.clear();
		m_factoryFlashReady.store(true, std::memory_order_release);
		return true;
	}

	bool Hardware::exchangePersistentFlashState(Hardware& _other)
	{
		waitProducerParked();
		_other.waitProducerParked();
		if(this == &_other)
			return true;
		if(m_model != MachineModel::Machinedrum || m_model != _other.m_model
			|| m_firmwareFingerprint != _other.m_firmwareFingerprint)
			return false;
		if(!m_uc.exchangeFlashState(_other.m_uc))
			return false;

		std::scoped_lock lock(m_factoryFlashMutex, _other.m_factoryFlashMutex);
		m_factoryFlashCache.swap(_other.m_factoryFlashCache);
		m_factoryFlashBaseline.swap(_other.m_factoryFlashBaseline);
		std::swap(m_factoryFlashInitializationExpected,
			_other.m_factoryFlashInitializationExpected);
		std::swap(m_factoryFlashCaptureOffset,
			_other.m_factoryFlashCaptureOffset);
		std::swap(m_factoryFlashCaptureFingerprint,
			_other.m_factoryFlashCaptureFingerprint);
		std::swap(m_factoryFlashCaptureComplete,
			_other.m_factoryFlashCaptureComplete);

		const auto exchangeAtomic = [](auto& _left, auto& _right)
		{
			const auto left = _left.load(std::memory_order_acquire);
			const auto right = _right.load(std::memory_order_acquire);
			_left.store(right, std::memory_order_release);
			_right.store(left, std::memory_order_release);
		};
		exchangeAtomic(m_externalInteraction, _other.m_externalInteraction);
		exchangeAtomic(m_factoryFlashReady, _other.m_factoryFlashReady);
		exchangeAtomic(m_factoryFlashPreparationReady,
			_other.m_factoryFlashPreparationReady);
		return true;
	}

	void Hardware::registerExternalInteraction()
	{
		// Pending project data must be installed before external traffic can make
		// the freshly initialized flash authoritative. This path is called from
		// real-time MIDI ingress and therefore remains lock-free and bounded.
		if(!m_pendingFlashRestoreActive.load(std::memory_order_acquire))
			m_externalInteraction.store(true, std::memory_order_relaxed);
	}

	void Hardware::disqualifyFactoryFlashCache()
	{
		registerExternalInteraction();
	}

	TransportScorecard Hardware::getTransportScorecard() noexcept
	{
#if MD_TRANSPORT_DIAGNOSTICS
		auto result = m_transportScorecard;
		// Sample the actual queues, independently of the recording counters, so
		// queue-conservation checks can detect an unaccounted mutation.
		result.link[0].currentRingDepth = m_linkRing[1].size();
		result.link[1].currentRingDepth = m_linkRing[0].size();
		result.mdRendezvousActive =
			m_mdLink.rendezvousActive.load(std::memory_order_acquire);
		result.mdPortCEdgePending =
			m_mdPortC.pending.load(std::memory_order_acquire);
		result.mdFlushEpoch = m_mdLink.flushEpoch.load(std::memory_order_acquire);
		result.mdPortCReleaseEpoch =
			m_mdPortC.releaseEpoch.load(std::memory_order_acquire);
		result.mmAwaitingFreshResponse =
			m_mmLinkAwaitFresh.load(std::memory_order_relaxed);
		result.mmStrobeEpoch = m_mmLinkStrobeEpoch.load(std::memory_order_relaxed);
		return result;
#else
		return {};
#endif
	}

	void Hardware::recordInlineHdi08Run(const uint32_t _dspIndex,
		const uint64_t _startCycle, const uint64_t _clampCycle,
		const bool _workComplete) noexcept
	{
#if MD_TRANSPORT_DIAGNOSTICS
		auto& score = m_transportScorecard.inlineHdi08[_dspIndex & 1];
		const auto endCycle = (_dspIndex & 1) == 0
			? m_dspMixer.dsp().getCycles() : m_dspProducer.dsp().getCycles();
		const auto requested = _clampCycle - _startCycle;
		const auto executed = endCycle - _startCycle;
		++score.calls;
		score.requestedCycles += requested;
		score.executedCycles += executed;
		score.maximumRequestedCycles = std::max(
			score.maximumRequestedCycles, requested);
		score.maximumExecutedCycles = std::max(
			score.maximumExecutedCycles, executed);
		if(_workComplete)
			++score.reachedTarget;
		else if(endCycle >= _clampCycle)
			++score.hitClamp;
		else
			++score.unexpectedShort;
#else
		(void)_dspIndex;
		(void)_startCycle;
		(void)_clampCycle;
		(void)_workComplete;
#endif
	}

	void Hardware::recordMdLinkPurge(const size_t _purgedFrames) noexcept
	{
		MD_TRANSPORT_RECORD(m_transportScorecard.link[1].mdWindowPurgedFrames
			+= _purgedFrames;
			m_transportScorecard.link[1].currentRingDepth -= std::min(
				m_transportScorecard.link[1].currentRingDepth, _purgedFrames););
		(void)_purgedFrames;
	}

	bool Hardware::linkDisposeAtConsumer(const uint32_t _consumer,
		const bool _rxTick)
	{
		// Pop-side dated disposal (parallel-transport spec §4). Runs in the
		// consuming DSP's context, so every check is local once the DSPs own
		// worker threads; the entries carry the producer-context facts.
		auto& ring = m_linkRing[_consumer];

		// A serial wire has no memory: everything that reached this receiver
		// while it was disabled is gone on real hardware. Detect the enable
		// edge here, in the receiver's own context, and drop that backlog.
		{
			auto& essi = (_consumer == 0 ? m_dspMixer : m_dspProducer)
				.getPeriph().getEssi0();
			const bool enabled = essi.hasEnabledReceivers();
			auto& wasEnabled = m_linkRxWasEnabled[_consumer].value;
			if(enabled && !wasEnabled)
			{
				size_t purged = 0;
				while(!ring.empty())
				{
					ring.pop_front();
					++purged;
				}
				MD_TRANSPORT_RECORD(m_transportScorecard.link[1u - _consumer]
					.receiverDisabledDrops += purged;);
				(void)purged;
			}
			// Store on change only: this runs on every link RX slot.
			if(wasEnabled != enabled)
				wasEnabled = enabled;
			if(!enabled)
				return false;
		}

		if(_consumer != 0)
			return false;	// no further consumer-side gates on the mixer->producer direction

		if(!isMonomachine())
		{
			// Words of a completed receive window die when the next window has
			// already opened (epoch mismatch) - the dated form of the old
			// "window opened during catch-up" push gate.
			const bool rendezvous =
				m_mdLink.rendezvousActive.load(std::memory_order_acquire);
			const auto flushEpoch =
				m_mdLink.flushEpoch.load(std::memory_order_acquire);
			const double now = linkConsumerNow(_consumer);
			for(;;)
			{
				if(ring.empty())
					return false;
				const auto& head = ring.front();
				if(rendezvous && head.epoch != flushEpoch)
				{
					ring.pop_front();
					MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
						.mdWindowOpenedDuringCatchUpDrops;);
					continue;
				}
				// Nothing arrives on the wire before the head's due time:
				// the receiver takes its skip-on-empty path meanwhile.
				if(head.dueFrames > now)
					return false;
				// Between a receive-window flush and DSP2's first DMA-fed TX
				// slot, idle retransmits of the retained register die with the
				// flush; the first DMA-fed word disarms this path. Judged by
				// the word's own TX-time underrun status carried in the entry.
				if(m_mdLink.awaitFresh.load(std::memory_order_acquire))
				{
					if(!head.fresh)
					{
						ring.pop_front();
						MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
							.mdPostFlushRetainedDrops;);
						continue;
					}
					m_mdLink.awaitFresh.store(false, std::memory_order_release);
				}
				break;
			}
			// Receiver-overrun semantics on the mixer link RX: silicon holds
			// at most ONE uncollected word (RX register, RDF set); the word
			// arriving on the wire while RDF is still set fails the shift->RX
			// transfer and is DESTROYED - the OLD word is kept, ROE is set
			// (DSP56303UM Table 7-5). Arrival order on the wire is FIFO, so
			// the destroyed word is the ring head. Engage only after DMA4
			// opens the steady-state receive window.
			auto& essi = m_dspMixer.getPeriph().getEssi0();
			if(!m_mdLink.roeEngaged.load(std::memory_order_acquire)
				&& essi.isFastLinkRx()
				&& (m_dspMixer.getPeriph().getDMA().getDCR(4)
					& (1u << dsp56k::DmaChannel::De)))
			{
				m_mdLink.roeEngaged.store(true, std::memory_order_release);
			}
			// Never inside the active on-demand rendezvous: that path carries
			// future wire edges directly and its words must survive until
			// their window (the push-side gate had the same carve-out).
			if(_rxTick && !rendezvous && !ring.empty()
				&& m_mdLink.roeEngaged.load(std::memory_order_acquire)
				&& essi.getSR().test(dsp56k::Essi::SSISR_RDF))
			{
				ring.pop_front();
				essi.setReceiverOverrun();
				MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
					.mdReceiverOverrunDrops;);
				return true;
			}
			return false;
		}

		// MM: words captured under a strobe epoch that a new PDRC request has
		// since retired belong to the completed wire interval - the dated
		// form of the old "strobe changed during catch-up" push gate.
		const auto strobeEpoch =
			m_mmLinkStrobeEpoch.load(std::memory_order_acquire);
		while(!ring.empty() && ring.front().epoch != strobeEpoch)
		{
			ring.pop_front();
			MD_TRANSPORT_RECORD(++m_transportScorecard.link[1]
				.mmStrobeChangedDuringCatchUpDrops;);
		}
		return false;
	}

	double Hardware::linkConsumerNow(const uint32_t _consumer)
	{
		if(!m_schedDspOriginLatched[_consumer])
			return std::numeric_limits<double>::infinity();
		return schedDspFramePos(_consumer);
	}

	bool Hardware::linkHeadDue(const uint32_t _consumer)
	{
		const auto& ring = m_linkRing[_consumer];
		return !ring.empty()
			&& ring.front().dueFrames <= linkConsumerNow(_consumer);
	}

	bool Hardware::linkHeadDue(const uint32_t _consumer, const double _now)
	{
		const auto& ring = m_linkRing[_consumer];
		return !ring.empty() && ring.front().dueFrames <= _now;
	}

	void Hardware::mdLinkWindowFlushed()
	{
		if(!m_mdLink.roeEngaged.load(std::memory_order_acquire))
			return;
		m_mdLink.flushEpoch.fetch_add(1, std::memory_order_acq_rel);

		if(m_mdLink.rendezvousArmPending.load(std::memory_order_acquire))
		{
			auto& producerEssi = m_dspProducer.getPeriph().getEssi0();
			auto& mixerEssi = m_dspMixer.getPeriph().getEssi0();
			auto& dma4 = m_dspMixer.getPeriph().getDMA();
			const bool producerOnDemand = producerEssi.getCRB().test(
				dsp56k::Essi::RegCRBbits::CRB_MOD)
				&& producerEssi.getTxWordCount() == 0;
			const bool mixerReady = mixerEssi.isFastLinkRx();
			const bool dma4Idle = (dma4.getDCR(4)
				& (1u << dsp56k::DmaChannel::De)) == 0;
			if(producerOnDemand && mixerReady && dma4Idle)
			{
				m_mdLink.rendezvousArmPending.store(false, std::memory_order_release);
				m_mdLink.rendezvousActive.store(true, std::memory_order_release);
				m_mdPortC.pending.store(false, std::memory_order_release);
				m_mdPortC.releaseEpoch.store(0, std::memory_order_release);
				m_dspProducer.getPeriph().getPortC().setHostInputSource([this]()
					-> dsp56k::TWord
				{
					if(m_mdPortC.pending.load(std::memory_order_acquire))
					{
						// Producer context: the mixer's DMA4 window via the mirror.
						if(dmaEnabled(0, 4))
						{
							m_mdPortC.visible.store(
								m_mdPortC.pendingLevel.load(std::memory_order_acquire),
								std::memory_order_release);
							m_mdPortC.releaseEpoch.store(
								m_mdPortC.pendingEpoch.load(std::memory_order_acquire),
								std::memory_order_release);
							m_mdPortC.pending.store(false, std::memory_order_release);
						}
					}
					return m_mdPortC.visible.load(std::memory_order_acquire);
				});
				producerEssi.setOnDemandTxWireSemantics(true);
				mixerEssi.setOnDemandRxWireSemantics(true);
				// Both directions share the same on-demand serial protocol: only
				// fresh TX words form receive edges, and a complete burst must
				// survive the legacy queue cleanup.
				mixerEssi.setOnDemandTxWireSemantics(true);
				producerEssi.setOnDemandRxWireSemantics(true);
				producerEssi.setPendingReceiveDmaOnEnable(true);
				// Retained idle words used to keep the peer's scheduler clock
				// coupled. Preserve that clock rendezvous without inventing RX
				// data; otherwise coarse slices can slip a 32-sample block.
				mixerEssi.setOnDemandTxIdleCallback([this]
				{
					if(!m_dspThreaded[1].load(std::memory_order_acquire))
						schedCatchUpDspToDsp(1, 0);
				});
				m_mdLink.awaitFresh.store(false, std::memory_order_release);
			}
		}
		else if(m_mdLink.rendezvousActive.load(std::memory_order_acquire))
		{
			// A request must have been observed before the next receive window.
			// Discarding an unexpectedly unreleased pin prevents a stale edge from
			// being promoted into the new DMA4 window.
			m_mdPortC.pending.store(false, std::memory_order_release);
		}

		if(m_mdLink.rendezvousActive.load(std::memory_order_acquire))
			return;
		m_mdLink.awaitFresh.store(true, std::memory_order_release);
	}

	std::optional<Hardware::SequencerPosition> Hardware::readSequencerPosition()
	{
		// Found with mdPlayheadProbe: a byte that counts 0 to 11 through a pattern of 12 steps, and the
		// sequencer's tick, 0 to 5 through each step, which holds still while the sequencer is stopped
		// (the machine's other tick counters run on: its clock serves the LFOs and delays too). No byte
		// was found that tells running from stopped both when started from the front panel and by MIDI
		// START with clock (the machine following the host): the sequencer plays while its tick moves.
		uint32_t stepAddress = 0;
		uint32_t tickAddress = 0;
		if(m_firmwareFingerprint == g_mdOs163Fingerprint)
		{
			stepAddress = 0x00261aa7;
			tickAddress = 0x01001f33;
		}
		else if(m_firmwareFingerprint == g_mmOs132bFingerprint)
		{
			stepAddress = 0x002bc287;
			tickAddress = 0x002bc29f;
		}
		else
			return std::nullopt;
		const auto tick = m_uc.read8(tickAddress);
		const auto now = getEmulatedFrames();
		if(!m_sequencerTickKnown || tick != m_sequencerTick)
		{
			// The first value seen is no tick
			m_sequencerTickFrame = m_sequencerTickKnown ? now : 0;
			m_sequencerTick = tick;
			m_sequencerTickKnown = true;
		}
		// A tick lasts 83 ms at the slowest tempo, 30 BPM: a quarter of a second without one is stopped
		const bool playing = m_sequencerTickFrame && now - m_sequencerTickFrame < g_samplerate / 4;
		return SequencerPosition{m_uc.read8(stepAddress), playing};
	}

	std::optional<LiveKit> Hardware::readLiveKit()
	{
		// Found with mdPlayheadProbe --livekit: bytes that follow ASSIGN MACHINE and a parameter's CC at
		// once. The Machinedrum keeps the Kit from $100000 as its dump lays it out (name at $0a, parameters
		// at $1a, 24 a track, levels at $19a, machines at $1aa, a 32-bit word a track whose low byte is the
		// id); the Monomachine from $100028 as its dump's decoded payload (name, levels at $0b, parameters
		// at $11, 72 a track with pages synthesis to LFO 3 first, then a machine id byte a track).
		LiveKit kit;
		kit.frame = getEmulatedFrames();
		if(m_firmwareFingerprint == g_mdOs163Fingerprint)
		{
			// Then, raw where the dump packs them in 7 bits: the LFOs from $1ea, 36 bytes a track (the five
			// fields $62 sets, then the LFO's state), and the master effects from $42a
			kit.tracks = automation::machinedrum::TrackCount;
			kit.machinedrum = true;
			for(uint8_t track = 0; track < kit.tracks; ++track)
			{
				for(uint8_t parameter = 0; parameter < 24; ++parameter)
					kit.values[track][parameter] = m_uc.read8(0x0010001a + track * 24u + parameter);
				kit.values[track][automation::machinedrum::Level * 8u] = m_uc.read8(0x0010019a + track);
				kit.machines[track] = m_uc.read8(0x001001ad + track * 4u);
				const uint32_t lfo = 0x001001ea + track * 36u;
				kit.lfos[track] = {m_uc.read8(lfo), m_uc.read8(lfo + 1), m_uc.read8(lfo + 2), m_uc.read8(lfo + 3),
					m_uc.read8(lfo + 4)};
			}
			for(uint32_t i = 0; i < kit.masterEffects.size(); ++i)
				kit.masterEffects[i] = m_uc.read8(0x0010042a + i);
			return kit;
		}
		if(m_firmwareFingerprint == g_mmOs132bFingerprint)
		{
			kit.tracks = automation::monomachine::TrackCount;
			for(uint8_t track = 0; track < kit.tracks; ++track)
			{
				for(uint8_t parameter = 0; parameter < (automation::monomachine::Lfo3 + 1) * 8; ++parameter)
					kit.values[track][parameter] = m_uc.read8(0x00100039 + track * 72u + parameter);
				kit.values[track][automation::monomachine::Level * 8u] = m_uc.read8(0x00100033 + track);
				kit.machines[track] = m_uc.read8(0x001001e9 + track);
			}
			return kit;
		}
		return std::nullopt;
	}

	std::optional<LivePatternLayout> Hardware::livePatternLayout() const
	{
		// Found with mdPlayheadProbe --pattern-ram, --pattern-ext and --library-ram: the 128 patterns as their
		// dumps' payload unpacks, in three blocks of 128, A01's first. The copy of steps 33 to 64 at $262560 is the
		// last dump received, and does not play.
		if(m_model == MachineModel::Machinedrum && m_firmwareFingerprint == g_mdOs163Fingerprint)
			return LivePatternLayout{0x001272c0, 0x0016bdc0, 0x00180000};
		return std::nullopt;
	}

	bool Hardware::writeRamIfUnchanged(const std::vector<RamWrite>& _writes)
	{
		for(const auto& write : _writes)
		{
			if(write.expected && m_uc.read8(write.address) != *write.expected)
				return false;
		}
		if(_writes.empty())
			return true;
		registerExternalInteraction();
		for(const auto& write : _writes)
			m_uc.write8(write.address, write.value);
		return true;
	}

	bool Hardware::trySendPanelEvent(const uint8_t _cmd, const uint8_t _arg)
	{
		registerExternalInteraction();
		return m_panelIn->tryPush(_cmd, _arg);
	}

	size_t Hardware::getPendingPanelInputBytes() const
	{
		return m_panelIn->size();
	}

	size_t Hardware::getPanelInputOverflowCount() const
	{
		return m_panelIn->overflowCount();
	}

	PanelInputQueueStatus Hardware::getPanelInputStatus() const
	{
		return m_panelIn->status();
	}

	void Hardware::processUC()
	{
		// Deliver queued panel input to firmware over UART2 RX. The existing
		// release/acquire pending count is a counted-work wake, not a second dirty
		// bit: a racing producer can make us defer once, but the count cannot clear
		// until this single consumer drains the published packet.
		// Do not let input mutate the bootstrap machine and then disappear when the
		// coherent project images are published. Queues remain intact until restore.
		const bool projectRestorePending =
			m_pendingFlashRestoreActive.load(std::memory_order_acquire);
		if(!projectRestorePending)
			pumpScheduledMidi();
		if(!projectRestorePending && m_panelIn->hasPending())
		{
			PanelInputQueue::DrainBuffer panelInput;
			const auto availablePackets = m_uc.availablePanelRxBytes() / 2;
			const auto panelInputCount = m_panelIn->drain(panelInput, availablePackets);
			// Packets pushed to the queue directly (getPanelInput) did not go through trySendPanelEvent
			if(panelInputCount)
				registerExternalInteraction();
			for(size_t i = 0; i < panelInputCount; ++i)
			{
				const auto& packet = panelInput[i];
				// This thread is the only Sim UART producer, and drain was capped to the
				// space sampled above, so both bytes are guaranteed to fit together.
				m_uc.queuePanelRx(packet.row);
				m_uc.queuePanelRx(packet.mask);
				synthLib::RealtimeInstrumentation::recordCurrentPanelDelivery(
					static_cast<uint32_t>(getModel()), packet.row, packet.mask);
			}
		}

		// Avoid entering MIDI arbitration when every source is idle; a producer
		// racing this observation is visible at the next instruction boundary.
		if(!projectRestorePending && (m_midiSysexTransfer.ownsMidiWire()
			|| m_midiInByteCursor != 0
			|| !m_midiIn.empty()
			|| m_realtimeMidiIn.size() != 0))
			pumpMidiIngress();

		// Drive DSP2's HI08 HREQ into the ColdFire external IRQ4 BEFORE stepping the CPU, so the
		// interrupt this pump raises is visible to the instruction m_uc.exec() runs (SIM interrupts
		// are injected inside exec()). See pumpDsp2HostRequest.
		if(!isMonomachine()
			|| m_schedulerHostPumpDirty.load(std::memory_order_acquire)
			|| m_dspMixer.hasDeferredHostRx() || m_dspProducer.hasDeferredHostRx())
			pumpDsp2HostRequest();

		const auto deltaCycles = m_uc.exec();
		if(!projectRestorePending && m_midiSysexTransfer.ownsMidiWire())
			m_midiSysexTransfer.service(deltaCycles,
				m_midiInByteCursor == 0
					&& m_realtimeMidiIn.sizeBefore(
						m_midiSysexTransfer.realtimeWriteBoundary()) == 0,
				m_uc);

		m_schedUcCyclesDone += deltaCycles;
	}

	uint32_t Hardware::processUCBatch(const uint64_t _maxCycles)
	{
		// processUC checks its inputs before every instruction and advances the
		// SIM after it. When no input is pending and no SIM, scheduled MIDI or
		// host word event falls within the budget, those checks find nothing
		// and the SIM advance is linear: run the instructions back to back and
		// advance the SIM once. A peripheral access or an interrupt acknowledge
		// ends the batch after its instruction (Microcontroller::beginBatch), so
		// the host port and the SIM see exactly what the one-by-one path shows.
		// Inputs other threads queue meanwhile (panel, MIDI, a pair worker's host
		// pump wake) are taken up after the batch.
		constexpr uint64_t minCycles = 16;
		if(_maxCycles < minCycles)
			return 0;
		if(m_pendingFlashRestoreActive.load(std::memory_order_acquire) || m_panelIn->hasPending()
			|| m_midiSysexTransfer.ownsMidiWire() || m_midiInByteCursor != 0
			|| !m_midiIn.empty() || m_realtimeMidiIn.size() != 0)
			return 0;
		if(m_schedulerHostPumpDirty.load(std::memory_order_acquire)
			|| m_dspMixer.hasDeferredHostRx() || m_dspProducer.hasDeferredHostRx())
			return 0;
		// As for the idle skip: a Machinedrum DSP transmit word waits for the pump
		if(!isMonomachine() && (m_dspMixer.hdi08().hasTX() || m_dspProducer.hdi08().hasTX()))
			return 0;
		const auto& sim = m_uc.getSim();
		if(sim.needsInterruptCheck() || m_uc.m_externalIrq4Pending != sim.externalIrq4Asserted())
			return 0;

		uint64_t budget = _maxCycles;
		for(const auto deadline : {sim.cyclesUntilNextTimerInterrupt(), sim.cyclesUntilNextUartTransmit()})
		{
			if(deadline != Sim::g_noTimerInterruptDeadline)
				budget = std::min<uint64_t>(budget, deadline);
		}
		if(!m_scheduledMidi.empty())
		{
			const auto due = m_scheduledMidi.front().cycle;
			budget = std::min<uint64_t>(budget, due > m_schedUcCyclesDone ? due - m_schedUcCyclesDone : 0);
		}
		if(budget < minCycles)
			return 0;

		// The batch ends once it reaches the budget: its last instruction may
		// cross an event, which the SIM advance at the end then raises, as the
		// one-by-one path does after that instruction. A pump wake from a DSP
		// worker ends it too, as processUC would pump before the next
		// instruction: a later take of the DSP's word frees its HOTX later and
		// shifts the DSP's HTDE pacing (GND SIN clicks in pair mode).
		uint64_t done = 0;
		m_uc.beginBatch();
		do
		{
			const auto cycles = m_uc.stepBatch();
			done += cycles;
			m_schedUcCyclesDone += cycles;
		}
		while(done < budget && !m_uc.batchBroken() && !m_uc.isAtIdleSelfBranch()
			&& !m_schedulerHostPumpDirty.load(std::memory_order_relaxed));
		m_uc.endBatch();
		return static_cast<uint32_t>(done);
	}

	void Hardware::pumpDsp2HostRequest()
	{
		// The settled path executes millions of ColdFire instructions between meaningful
		// host-port edges. Keep that overwhelmingly common clean check read-only; reserve the
		// cache-line-writing RMW for a producer/consumer/ICR wake. A wake racing the exchange
		// remains set for the next instruction, so no event can be lost.
		// Advancing CPU time can make a reserved word visible even without
		// another peripheral edge. Keep pumping until it reaches its deadline.
		const bool deferred = m_dspMixer.hasDeferredHostRx()
			|| m_dspProducer.hasDeferredHostRx();
		if(!m_schedulerHostPumpDirty.load(std::memory_order_acquire) && !deferred)
			return;
		if(!m_schedulerHostPumpDirty.exchange(false, std::memory_order_acq_rel) && !deferred)
			return;

		// DSP2's HI08 receive request drives ColdFire IRQ4. Monomachine uses
		// the hardware RXDF latch. Waiting for three queued words spans two of
		// its block notifications instead of requesting service for the first word.
		const auto policy = transportPolicy(m_model);

		// Drain both DSP transmit paths continuously; only the HREQ-to-IRQ4 wire
		// is DSP2-specific. Drain the mixer path as well so its transmit
		// register cannot remain full.
		uint32_t mixerMoved = 0;
		if(m_dspMixer.booted())
			mixerMoved = m_dspMixer.pumpHostRx(policy.hostReceiveQueueCapacityWords);

		if(!m_dspProducer.booted())
			return;	// pre-boot: DSP2 is not producing; IRQ4 stays deasserted (reset default)

		const uint32_t producerMoved = m_dspProducer.pumpHostRx(
			policy.hostReceiveQueueCapacityWords);

		auto& hdi = m_uc.getHdi08Dsp2();
		const bool rreq = (hdi.icr() & mc68k::Hdi08::Rreq) != 0;	// ColdFire enabled receive requests
		const bool hreq = rreq && hdi.hostRxWordsAvailable()
			>= policy.hostReceiveIrqMinWords;
		(void)mixerMoved;
		(void)producerMoved;
		m_uc.getSim().setExternalIrq4(hreq);
	}

	void Hardware::notifyHostPumpStateChanged()
	{
		m_schedulerHostPumpDirty.store(true, std::memory_order_release);
	}

	void Hardware::onEssiCallbackMixer()
	{
		m_esaiFrameIndex.fetch_add(1, std::memory_order_acq_rel);

		// The callback runs inside the mixer on the scheduler thread. Drain the
		// codec ring immediately so its blocking producer can never park that thread.
		// A mixer on the pair worker leaves the ring to the audio thread,
		// which drains it at the end of each advance: the ring is the SPSC
		// hand-over between the two. It dates the frame with the codec clock's
		// tick that sent it (schedDrainCodecOutput): this callback runs when
		// the peripherals are next serviced, up to 0.7 frame later (measured).
		if(!m_dspThreaded[0].load(std::memory_order_relaxed))
		{
			schedDrainCodecOutput();
			return;
		}
		m_codecFrameCycles.push_back(m_dspMixer.getPeriph().getEssiClock().getLastClock());
	}

	void Hardware::ensureBufferSize(const uint32_t _frames)
	{
		for(auto& audioOutput : m_audioOutputs)
		{
			if(audioOutput.size() < _frames)
				audioOutput.resize(_frames, 0);
		}
	}

	void Hardware::setHostAudioInputLatency(const uint32_t _latency)
	{
		const auto latency = static_cast<uint32_t>(std::min<uint64_t>(
			uint64_t{_latency} + g_hostAudioInputSafetyFrames, RealtimeHostAudioInputQueue::capacity()));
		if(m_hostAudioInputLatencyInitialized && latency == m_hostAudioInputLatency)
			return;

		const size_t receiverCount = m_hostAudioInput.size();
		for(size_t receiver = 0; receiver < receiverCount; ++receiver)
			m_hostAudioInput[receiver].reset(static_cast<int64_t>(m_schedFramesTotal));
		m_hostAudioInputLatency = latency;
		m_hostAudioInputLatencyInitialized = true;
		// Publish for the receiver contexts; the generation bump makes each
		// receiver re-originate its clock on its next frame.
		m_hostAudioInputLatencyPublished.store(latency, std::memory_order_release);
		m_hostAudioInputGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	void Hardware::queueHostAudioInput(const uint32_t _frames)
	{
		// Disconnected host buses supply known silence. Preserve any delayed tail,
		// then let the ADC callback synthesize zero without queuing zero frames.
		if(!m_hostAudioInputSource[0] && !m_hostAudioInputSource[1])
		{
			m_hostAudioInputSourceCursor += _frames;
			return;
		}
		const size_t receiverCount = m_hostAudioInput.size();
		for(size_t receiver = 0; receiver < receiverCount; ++receiver)
		{
			const auto dropped = m_hostAudioInput[receiver].append(m_hostAudioInputSource,
				m_hostAudioInputSourceFrames, m_hostAudioInputSourceCursor,
				_frames, static_cast<int64_t>(m_schedFramesTotal));
			if(dropped)
				m_hostAudioInputOverflow[receiver].fetch_add(dropped, std::memory_order_relaxed);
		}
		m_hostAudioInputSourceCursor += _frames;
		// Only now is there input to miss: a receiver on a worker thread may
		// read between the call's start and this first append.
		m_hostAudioInputHasSource.store(true, std::memory_order_release);
	}

	void Hardware::processAudio(const uint32_t _frames, const uint32_t _latency)
	{
		m_midiOutputNativeOrigin.store(static_cast<uint64_t>(m_schedFramesTotal), std::memory_order_relaxed);
		ensureBufferSize(_frames);
		setHostAudioInputLatency(_latency);

		// During a real host callback retain the frames that the scheduler drains
		// immediately, then copy them into the plug-in's six output channels.
		for(auto& output : m_audioOutputs)
			std::fill_n(output.data(), _frames, dsp56k::TWord(0));

		m_schedHostAudioActive = true;
		size_t unfilled = 0;
		const auto trimmed = renderHostAudio(m_schedHostAudio, m_audioOutputs, _frames,
			[this](const uint32_t _chunk)
			{
				queueHostAudioInput(_chunk);
				advance(_chunk);
			}, &unfilled);
		m_schedHostAudioActive = false;
		if(unfilled)
			m_schedHostAudioUnderrun.fetch_add(unfilled, std::memory_order_relaxed);

		// Preserve a small surplus to maintain codec continuity, but never allow
		// stale output to accumulate beyond one current host block.
		if(trimmed)
			m_schedHostAudioOverflow.fetch_add(trimmed, std::memory_order_relaxed);
	}

	void Hardware::processAudio(const synthLib::TAudioOutputs& _outputs, const uint32_t _frames, const uint32_t _latency)
	{
		processAudio(_frames, _latency);

		for(uint32_t ch = 0; ch < m_audioOutputs.size(); ++ch)
		{
			if(!_outputs[ch])
				continue;
			for(uint32_t i = 0; i < _frames; ++i)
				_outputs[ch][i] = dsp56k::dsp2sample<float>(m_audioOutputs[ch][i]);
		}
	}

	void Hardware::processAudio(const synthLib::TAudioInputs& _inputs,
		const synthLib::TAudioOutputs& _outputs, const uint32_t _frames,
		const uint32_t _latency)
	{
		m_hostAudioInputSource = _inputs;
		m_hostAudioInputSourceFrames = _frames;
		// Set by queueHostAudioInput once input is queued.
		m_hostAudioInputHasSource.store(false, std::memory_order_release);
		m_hostAudioInputSourceCursor = 0;
		processAudio(_outputs, _frames, _latency);
		m_hostAudioInputSource.fill(nullptr);
		m_hostAudioInputSourceFrames = 0;
		m_hostAudioInputHasSource.store(false, std::memory_order_release);
		m_hostAudioInputSourceCursor = 0;
	}

	// -------------------------------------------------------------------------------------------
	// The deterministic interleave scheduler advances the whole machine by
	// _machineFrames codec frames of shared machine time, on the caller's thread, with no background
	// threads. It maintains a machine clock in codec frames and, in an event-driven loop, repeatedly
	// steps whichever component (UC / DSP1 / DSP2) is furthest BEHIND the clock forward by a bounded
	// background quantum. Fine-grained UC<->DSP synchronization happens at each HI08 access. Rates are
	// exact: one frame = g_dsp1CyclesPerEsaiFrame (2304) DSP cycles = g_ucClockHz/g_samplerate UC
	// cycles. The scheduler uses the model-specific transport policy below.
	// -------------------------------------------------------------------------------------------
	namespace
	{
		double schedQuantumFrames(const MachineModel _model)
		{
			// MD_BACKGROUND_QUANTUM_US overrides the policy for experiments.
			static const double s_override = envNumber("MD_BACKGROUND_QUANTUM_US").value_or(0.0);
			const double us = s_override > 0.0 ? s_override : transportPolicy(_model).backgroundQuantumMicroseconds;
			return us * static_cast<double>(g_samplerate) / 1.0e6;				// -> codec frames
		}

		uint64_t schedClampCycles(const MachineModel _model)
		{
			return transportPolicy(_model).catchUpMaxDspCycles;
		}

		double schedUcCyclesPerFrame()
		{
			return static_cast<double>(g_ucClockHz) / static_cast<double>(g_samplerate);
		}

	}


	double Hardware::schedDspFramePos(const uint32_t _dspIndex)
	{
		auto& d = (_dspIndex == 0) ? m_dspMixer : m_dspProducer;
		const uint64_t cyc = d.dsp().getCycles() - m_schedDspOriginCycles[_dspIndex];
		return m_schedDspOriginFrame[_dspIndex] + static_cast<double>(cyc) / static_cast<double>(g_dsp1CyclesPerEsaiFrame);
	}

	void Hardware::schedDrainCodecOutput()
	{
		// Pop everything the mixer (DSP1) ESSI1 TX produced so its blocking push
		// can never park the single scheduler thread. A mixer on the pair worker
		// may have run past the block target: only its frames dated up to
		// g_pairCodecCutFrames before the target belong to this block, so every
		// block gets the same count however far ahead the worker got.
		auto& out = m_dspMixer.getPeriph().getEssi1().getAudioOutputs();
		const bool dated = m_dspThreaded[0].load(std::memory_order_acquire);
		const uint64_t cut = dated ? schedFrameToDspCycles(0, m_schedFramesTotal - g_pairCodecCutFrames) : 0;
		while(!out.empty())
		{
			if(dated)
			{
				// The worker dates a frame right after emitting it: one not
				// dated yet was emitted past the target, so past the cut too.
				if(m_codecFrameCycles.empty() || m_codecFrameCycles.front() > cut)
					break;
				m_codecFrameCycles.pop_front();
			}
			auto frame = out.pop_front();


			if(m_schedHostAudioActive)
			{
				const bool dropped = m_schedHostAudio.emplace(
					[&frame](RealtimeHostAudioQueue::Frame& _hostFrame)
				{
					mapCodecOutputFrame(_hostFrame, frame);
				});
				if(dropped)
					m_schedHostAudioOverflow.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	bool Hardware::schedStep()
	{
		const double ucPerFrame   = schedUcCyclesPerFrame();
		const double quantumFrames= schedQuantumFrames(m_model);
		const uint64_t clampCycles= schedClampCycles(m_model);
		const double target       = m_schedFramesTotal;

		// Publish machine positions once per slice (parallel-transport spec
		// §2). Serial mode has no consumers yet; the stores document the
		// publication points the workers will wait on.
		m_schedPublished.ucCycles.store(m_schedUcCyclesDone,
			std::memory_order_release);
		// A DSP owned by a worker publishes its own position.
		if(!m_dspThreaded[0].load(std::memory_order_acquire))
			m_schedPublished.dspCycles[0].store(m_dspMixer.dsp().getCycles(),
				std::memory_order_release);
		if(!m_dspThreaded[1].load(std::memory_order_acquire))
			m_schedPublished.dspCycles[1].store(m_dspProducer.dsp().getCycles(),
				std::memory_order_release);
		m_signal.notify();
		schedTryHandoffProducer();
		schedTryHandoffPair();

		const double ucPos = static_cast<double>(m_schedUcCyclesDone) / ucPerFrame;

		// Latch the rate-lock origin of any DSP that just became runnable (boot finished during a UC
		// step): it starts "now" (the current UC machine-time), its cycle counter ~0. From here its
		// machine-frame position tracks 2304 executed cycles per frame.
		for(uint32_t i = 0; i < 2; ++i)
		{
			auto& d = (i == 0) ? m_dspMixer : m_dspProducer;
			if(!m_schedDspOriginLatched[i] && d.booted())
			{
				m_schedDspOriginLatched[i]  = true;
				m_schedDspOriginFrame[i]    = ucPos;
				m_schedDspOriginUcCycles[i]= m_schedUcCyclesDone;
				m_schedDspOriginCycles[i]   = d.dsp().getCycles();
			}
		}
		// A DSP that is not yet runnable is parked at the target so it is never chosen as the laggard.
		// So is a DSP owned by a worker: it paces itself against the published
		// positions instead.
		double dsp1Pos = (m_schedDspOriginLatched[0]
			&& !m_dspThreaded[0].load(std::memory_order_acquire))
			? schedDspFramePos(0) : target;
		double dsp2Pos = (m_schedDspOriginLatched[1]
			&& !m_dspThreaded[1].load(std::memory_order_acquire))
			? schedDspFramePos(1) : target;

		// MM host traffic is a flow-controlled lossless stream. Park a backlogged
		// DSP slice until the UC drains below
		// the threshold; a release clamp bounds the stall so a non-draining UC phase cannot
		// starve the codec. MD path untouched.
		const bool s_mmBackpressure = isMonomachine();
		if(s_mmBackpressure)
		{
			const auto policy = transportPolicy(m_model);
			for(uint32_t i = 0; i < 2; ++i)
			{
				// A worker applies the backpressure of the DSPs it owns.
				if(m_dspThreaded[i].load(std::memory_order_acquire))
					continue;
				auto& d = (i == 0) ? m_dspMixer : m_dspProducer;
				double& pos = (i == 0) ? dsp1Pos : dsp2Pos;
				if(!m_schedDspOriginLatched[i] || !d.booted()
					|| d.hostTxBacklog() <= policy.hostTransmitBackpressureThresholdWords)
				{
					m_mmBpSinceUcCycles[i] = 0;
					continue;
				}
				if(!m_mmBpSinceUcCycles[i])
					m_mmBpSinceUcCycles[i] = m_schedUcCyclesDone + 1;	// +1: 0 means "not stalled"
				if(m_schedUcCyclesDone - (m_mmBpSinceUcCycles[i] - 1)
					< policy.hostTransmitBackpressureReleaseUcCycles)
				{
					pos = target;
					MD_TRANSPORT_RECORD(++m_transportScorecard.mmBackpressureParkDecisions[i];);
				}
			}
		}
		double minPos = ucPos; int who = 0;			// 0 = UC, 1 = DSP1(mixer), 2 = DSP2(producer)
		if(dsp1Pos < minPos) { minPos = dsp1Pos; who = 1; }
		if(dsp2Pos < minPos) { minPos = dsp2Pos; who = 2; }

		if(minPos >= target)
			return false;							// everything has reached the shared clock

		double subTarget = std::min(minPos + quantumFrames, target);

		// Threaded producer: the producer trails the UC (its only external
		// gate), and the mixer's link wait needs the producer at mixer - D,
		// so the mixer may not lead the UC by more than D. Both run on this
		// thread: a mixer at its cap simply yields the slice to the UC (the
		// cap is never below the block target once the UC has reached it).
		if(m_dspThreaded[1].load(std::memory_order_acquire) && who == 1)
		{
			const double mixerCap = ucPos + m_linkPipelineDepthFrames;
			if(dsp1Pos >= mixerCap && ucPos < target)
			{
				who = 0;
				minPos = ucPos;
				subTarget = std::min(ucPos + quantumFrames, target);
			}
			else
				// Strictly at the cap: a mixer past UC + D reads link slots the
				// UC-gated producer cannot have produced, and the UC cannot
				// advance while this thread waits inside the mixer for them.
				subTarget = std::min(subTarget, mixerCap);
		}

		// Pair worker: the DSPs trail the UC, so a DSP word reaches the UC as
		// late as the DSPs lag. The serial scheduler bounded that lag by one
		// quantum (laggard-first): keep the UC within that lead of the slower
		// DSP, waiting for the worker when it is there. A DSP held by MM
		// backpressure does not count - it waits for this UC, not the other
		// way round. MD_PAIR_UC_LEAD_US overrides the lead.
		if(m_dspPairWorker.load(std::memory_order_acquire))
		{
			const double ucLead = m_pairUcLeadFrames;
			const auto dspMinFrames = [&]
			{
				double pos = std::numeric_limits<double>::infinity();
				for(uint32_t i = 0; i < 2; ++i)
				{
					if(m_pairBackpressured[i].load(std::memory_order_acquire))
						continue;
					const auto cycles = m_schedPublished.dspCycles[i].load(std::memory_order_acquire);
					pos = std::min(pos, m_schedDspOriginFrame[i]
						+ static_cast<double>(cycles - std::min(cycles, m_schedDspOriginCycles[i]))
							/ static_cast<double>(g_dsp1CyclesPerEsaiFrame));
				}
				return pos;
			};
			// The DSP minimum only grows while a DSP stays unheld: re-read the
			// published positions only when the cached one would cap this slice.
			if(m_pairDspMinCache + ucLead < ucPos + quantumFrames)
				m_pairDspMinCache = dspMinFrames();
			if(ucPos >= m_pairDspMinCache + ucLead)
			{
				m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
				m_signal.notify();
				const auto waitStart = m_transportTrace ? std::chrono::steady_clock::now()
					: std::chrono::steady_clock::time_point{};
				waitTransport(TransportWaitSite::DspTime, std::chrono::milliseconds(50), [&]
				{
					return dspMinFrames() + ucLead > ucPos;
				});
				if(m_transportTrace)
				{
					m_pairTrace.leadWaits.fetch_add(1, std::memory_order_relaxed);
					m_pairTrace.leadWaitNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - waitStart).count()), std::memory_order_relaxed);
				}
				m_pairDspMinCache = dspMinFrames();
				return true;
			}
			subTarget = std::min(subTarget, std::max(m_pairDspMinCache + ucLead, ucPos + 1e-3));
		}

		if(m_transportTrace && (++m_schedStepCount % 20000) == 0)
			std::fprintf(stderr, "[audio] steps=%llu uc=%.1f mixer=%.1f producer=%.1f target=%.1f "
				"clamps dsp=%llu link=%llu room=%llu parked=%llu mixerGate=%llu\n",
				static_cast<unsigned long long>(m_schedStepCount), ucPos, dsp1Pos,
				producerPublishedFrames(), target,
				static_cast<unsigned long long>(m_transportWaitClamps[0].load()),
				static_cast<unsigned long long>(m_transportWaitClamps[1].load()),
				static_cast<unsigned long long>(m_transportWaitClamps[2].load()),
				static_cast<unsigned long long>(m_transportWaitClamps[3].load()),
				static_cast<unsigned long long>(m_transportWaitClamps[4].load()));

		if(who == 0)
		{
#if MD_TRANSPORT_DIAGNOSTICS
			auto& score = m_transportScorecard.backgroundUc;
			++score.calls;
			const auto diagnosticStart = m_schedUcCyclesDone;
			const auto diagnosticTarget = static_cast<uint64_t>(
				std::ceil(subTarget * ucPerFrame));
			const auto diagnosticRequested = diagnosticTarget > diagnosticStart
				? diagnosticTarget - diagnosticStart : 0;
			score.requestedCycles += diagnosticRequested;
			score.maximumRequestedCycles = std::max(
				score.maximumRequestedCycles, diagnosticRequested);
#endif
			// Advance the UC toward subTarget; each processUC() runs one m_uc.exec() (and its HI08
			// callbacks, which catch the target DSP up inline). Guaranteed at least one step; clamped.
			m_audioPhase.store(1);
			const auto ucSliceStart = m_transportTrace ? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			const auto ucSliceStartCycles = m_schedUcCyclesDone;
			const uint64_t clampStop = m_schedUcCyclesDone + clampCycles;

			uint32_t probeCount = 0;
			// Threaded producer: publish the UC position while the slice runs.
			// The producer is gated at the published UC position with zero lead,
			// so a slice-end publication alone makes it start each stretch of
			// machine time only once the UC has finished it - the mixer then
			// waits for the producer to cover the whole slice. Published <= real,
			// so no host word can ever be stamped behind the producer.
			const bool publishInSlice = m_dspThreaded[1].load(std::memory_order_acquire);
			uint32_t publishCount = 0;
			// MD_UC_BATCH=0 steps every instruction through processUC.
			static const bool s_ucBatch = []{ const char* e = std::getenv("MD_UC_BATCH"); return !e || e[0] != '0'; }();
			do
			{
			// A batch stays within the slice, and within a short stretch while a
			// threaded DSP paces itself on the published UC position.
			bool batched = false;
			if(s_ucBatch)
			{
				const double sliceLeft = subTarget * ucPerFrame - static_cast<double>(m_schedUcCyclesDone);
				if(sliceLeft > 0.0 && m_schedUcCyclesDone < clampStop)
					batched = processUCBatch(std::min(std::min(static_cast<uint64_t>(std::ceil(sliceLeft)),
						clampStop - m_schedUcCyclesDone), publishInSlice ? uint64_t{128} : uint64_t{2048})) != 0;
			}
			if(!batched)
				processUC();
			if(publishInSlice && (batched || (++publishCount & 31) == 0))
			{
				m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
				m_signal.notify();
			}
			// Probe periodically within the existing UC slice. A
			// pending host word/wake, restore or MIDI transfer disables skipping.
			// The Monomachine path skips its ColdFire idle loop (BRA.B -2) in
			// chunks. The Machinedrum idles the same way, but its unconditional
			// per-step host pump must not be skipped while a DSP holds an
			// unpumped transmit word: delaying that word would delay the
			// HREQ->IRQ4 edge the idle firmware may be waiting for. With both
			// transmit registers empty the pump is a no-op (no UC reads happen
			// mid-skip, so the latched queue state cannot be observed), and the
			// skip stays transparent.
			// Evaluated only for the Machinedrum: DSP registers are not the
			// UC's to read once a worker owns the DSPs.
			const auto dspTxClear = [this]
			{
				return !m_dspMixer.hdi08().hasTX() && !m_dspProducer.hdi08().hasTX();
			};
			// A deferred (dated) host word no longer forbids the skip: the jump
			// below is bounded by its ready cycle, so the pump delivers it at
			// exactly the host cycle a non-skipping UC would have reached.
			// A batch stops at the idle loop: probe right away.
			if(((batched && m_uc.isAtIdleSelfBranch()) || (probeCount++ & 15u) == 0)
				&& (isMonomachine() || dspTxClear())
				&& m_schedUcCyclesDone < clampStop
					&& !m_pendingFlashRestoreActive.load(std::memory_order_acquire)
					&& !m_schedulerHostPumpDirty.load(std::memory_order_acquire)
					&& !m_midiSysexTransfer.ownsMidiWire() && m_midiInByteCursor == 0)
				{
					const double remaining = (subTarget
						- static_cast<double>(m_schedUcCyclesDone) / ucPerFrame) * ucPerFrame;
					if(remaining >= 16.0)
					{
						auto maxCycles = static_cast<uint32_t>(std::min<double>(
							remaining, static_cast<double>(clampStop - m_schedUcCyclesDone)));
						if(!m_scheduledMidi.empty())
						{
							const auto deadline = m_scheduledMidi.front().cycle;
							maxCycles = deadline <= m_schedUcCyclesDone ? 0
								: static_cast<uint32_t>(std::min<uint64_t>(maxCycles,
									deadline - m_schedUcCyclesDone));
						}
						// A staged host word becomes visible at its ready cycle;
						// never jump past it, or its HREQ->IRQ4 edge would slip by
						// the whole skip (spec §5.7).
						const auto nextHostRx = std::min(
							m_dspMixer.nextDeferredHostRxCycle(),
							m_dspProducer.nextDeferredHostRxCycle());
						if(nextHostRx != std::numeric_limits<uint64_t>::max())
							maxCycles = nextHostRx <= m_schedUcCyclesDone ? 0
								: static_cast<uint32_t>(std::min<uint64_t>(maxCycles,
									nextHostRx - m_schedUcCyclesDone));
						const auto limit = m_uc.idleSelfBranchInstructions(maxCycles);
						uint32_t instructions = 0;
						// Keep external input polling at each omitted instruction
						// boundary; a producer still wakes the ordinary path.
						for(; instructions < limit; ++instructions)
							if(m_panelIn->hasPending() || !m_midiIn.empty()
								|| m_realtimeMidiIn.size() != 0
								|| m_midiSysexTransfer.ownsMidiWire())
								break;
						if(instructions)
						{
							MD_TRANSPORT_RECORD(m_transportScorecard.idleSelfBranchInstructions
								+= instructions;);
							const auto cycles = instructions * 2;
							// Preserve the host clock seen by the final SIM update.
							m_schedUcCyclesDone += cycles - 2;
							m_uc.advanceIdleSelfBranch(instructions);
							m_schedUcCyclesDone += 2;
						}
					}
				}
			}
			while(static_cast<double>(m_schedUcCyclesDone) / ucPerFrame < subTarget
				&& m_schedUcCyclesDone < clampStop);
			if(m_transportTrace)
			{
				m_timeTrace.ucSliceNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - ucSliceStart).count()), std::memory_order_relaxed);
				m_timeTrace.ucSliceCycles.fetch_add(m_schedUcCyclesDone - ucSliceStartCycles, std::memory_order_relaxed);
				m_timeTrace.ucSlices.fetch_add(1, std::memory_order_relaxed);
			}
#if MD_TRANSPORT_DIAGNOSTICS
			const auto diagnosticExecuted = m_schedUcCyclesDone - diagnosticStart;
			score.executedCycles += diagnosticExecuted;
			score.maximumExecutedCycles = std::max(
				score.maximumExecutedCycles, diagnosticExecuted);
			if(static_cast<double>(m_schedUcCyclesDone) / ucPerFrame >= subTarget)
				++score.reachedTarget;
			else if(m_schedUcCyclesDone >= clampStop)
				++score.hitClamp;
			else
				++score.unexpectedShort;
#endif
		}
		else
			schedRunDspSlice(who - 1, subTarget);

		return true;
	}

	void Hardware::schedRunDspSlice(const uint32_t _dspIndex, const double _subTarget)
	{
		const auto phaseBefore = m_audioPhase.exchange(2);
		struct PhaseRestore
		{
			std::atomic<int32_t>& phase; int32_t value;
			~PhaseRestore() { phase.store(value); }
		} restore{m_audioPhase, phaseBefore};
		const uint64_t clampCycles = schedClampCycles(m_model);
		auto& d = (_dspIndex == 0) ? m_dspMixer : m_dspProducer;
		const uint32_t idx = _dspIndex;
		const uint64_t startCyc  = d.dsp().getCycles();
		struct SliceTimer
		{
			std::atomic<uint64_t>* ns;
			std::atomic<uint64_t>* cycles;
			std::atomic<uint64_t>* count;
			dsp56k::DSP& dsp;
			uint64_t startCycles;
			std::chrono::steady_clock::time_point start;
			~SliceTimer()
			{
				if(!ns)
					return;
				ns->fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - start).count()), std::memory_order_relaxed);
				cycles->fetch_add(dsp.getCycles() - startCycles, std::memory_order_relaxed);
				if(count)
					count->fetch_add(1, std::memory_order_relaxed);
			}
		} sliceTimer{m_transportTrace ? (idx == 0 ? &m_timeTrace.mixerSliceNs : &m_timeTrace.producerSliceNs) : nullptr,
			idx == 0 ? &m_timeTrace.mixerSliceCycles : &m_timeTrace.producerSliceCycles,
			idx == 0 ? &m_timeTrace.mixerSlices : nullptr, d.dsp(), startCyc,
			m_transportTrace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};
		uint64_t targetCyc = m_schedDspOriginCycles[idx]
			+ static_cast<uint64_t>((_subTarget - m_schedDspOriginFrame[idx]) * static_cast<double>(g_dsp1CyclesPerEsaiFrame));
		if(targetCyc <= startCyc)
			targetCyc = startCyc + 1;			// guarantee >=1 step of progress (float rounding)
		const uint64_t stopCyc = std::min(targetCyc, startCyc + clampCycles);
#if MD_TRANSPORT_DIAGNOSTICS
		auto& score = m_transportScorecard.backgroundDsp[idx];
		++score.calls;
		const auto diagnosticRequested = targetCyc - startCyc;
		score.requestedCycles += diagnosticRequested;
		score.maximumRequestedCycles = std::max(
			score.maximumRequestedCycles, diagnosticRequested);
#endif
		// A HOTX word left in the latch while the staging queue was full
		// gets its chance now that the UC may have drained the queue.
		d.stageHostTx();
		if(m_schedBoundedJit)
			d.dsp().execUntilCycles(stopCyc);
		else
		{
			d.dsp().exec();
			while(d.dsp().getCycles() < stopCyc)
				d.dsp().exec();
		}
#if MD_TRANSPORT_DIAGNOSTICS
		const auto diagnosticExecuted = d.dsp().getCycles() - startCyc;
		score.executedCycles += diagnosticExecuted;
		score.maximumExecutedCycles = std::max(
			score.maximumExecutedCycles, diagnosticExecuted);
		if(d.dsp().getCycles() >= targetCyc)
			++score.reachedTarget;
		else if(stopCyc < targetCyc && d.dsp().getCycles() >= stopCyc)
			++score.hitClamp;
		else
			++score.unexpectedShort;
#endif
		if(idx == 0)
			schedDrainCodecOutput();			// keep the mixer ESSI1 output ring shallow
		m_schedPublished.dspCycles[idx].store(d.dsp().getCycles(),
			std::memory_order_release);
		m_signal.notify();
	}

	double Hardware::producerPublishedFrames() const
	{
		if(!m_schedDspOriginLatched[1])
			return std::numeric_limits<double>::infinity();
		const auto cycles = m_schedPublished.dspCycles[1].load(std::memory_order_acquire);
		return m_schedDspOriginFrame[1]
			+ static_cast<double>(cycles - std::min(cycles, m_schedDspOriginCycles[1]))
				/ static_cast<double>(g_dsp1CyclesPerEsaiFrame);
	}

	uint64_t Hardware::schedFrameToDspCycles(const uint32_t _dspIndex, const double _frames) const
	{
		const auto idx = _dspIndex & 1;
		const double delta = _frames - m_schedDspOriginFrame[idx];
		if(delta <= 0.0)
			return m_schedDspOriginCycles[idx];
		return m_schedDspOriginCycles[idx]
			+ static_cast<uint64_t>(delta * static_cast<double>(g_dsp1CyclesPerEsaiFrame));
	}

	void Hardware::schedTryHandoffProducer()
	{
		// Handoff barrier (parallel-transport spec §3): the producer moves to
		// its worker only once the boot-era protocol that mutates its ESSI
		// from the mixer context has completed - the MD OS 1.63 rendezvous
		// arming (mdLinkWindowFlushed). The Monomachine stays on the serial
		// adapter in this migration step.
		if(m_transportMode != TransportMode::Parallel || isMonomachine()
			|| m_dspThreaded[1].load(std::memory_order_acquire)
			|| !m_dspProducer.booted() || !m_dspMixer.booted()
			|| !m_schedDspOriginLatched[0] || !m_schedDspOriginLatched[1]
			|| m_mdLink.rendezvousArmPending.load(std::memory_order_acquire))
			return;
		m_schedPublished.dspCycles[1].store(m_dspProducer.dsp().getCycles(),
			std::memory_order_release);
		m_producerParked.store(false, std::memory_order_release);
		m_workerExit.store(false, std::memory_order_release);
		m_dspThreaded[1].store(true, std::memory_order_release);
		m_producerWorker = std::thread([this] { producerWorkerLoop(); });
	}

	void Hardware::schedTryHandoffPair()
	{
		// The lone producer's barrier: both DSPs booted and rate-locked, and
		// the MD OS 1.63 rendezvous armed. The two DSPs stay on one thread, so
		// the link, the Port C strobe and the MM epochs keep their serial
		// ordering; only the UC's accesses become dated.
		if(m_transportMode != TransportMode::Pair
			|| m_dspThreaded[1].load(std::memory_order_acquire)
			|| !m_dspProducer.booted() || !m_dspMixer.booted()
			|| !m_schedDspOriginLatched[0] || !m_schedDspOriginLatched[1]
			|| m_mdLink.rendezvousArmPending.load(std::memory_order_acquire))
			return;
		// This thread still owns both DSPs: hand what the serial path holds
		// over to the threaded structures before the worker starts.
		if(m_transportTrace)
			std::fprintf(stderr, "[pair] handoff at uc=%llu\n", static_cast<unsigned long long>(m_schedUcCyclesDone));
		schedDrainCodecOutput();
		m_dspMixer.enterThreadedHostTransport();
		m_dspProducer.enterThreadedHostTransport();
		m_dspMixer.publishHostStatus();
		m_dspProducer.publishHostStatus();
		// Gate constants. The laggard-first scheduler let a DSP run up to a
		// quantum past the UC (a UC word then landed that much late in DSP
		// time) and the UC a quantum past a DSP (a DSP word reached it that
		// much late). The UC keeps that quantum. The DSPs get the policy's
		// shorter lead: an idle worker waits at its gate, so unlike the serial
		// scheduler it lands nearly every UC word at the full lead, plus up to
		// a chunk (transportPolicy). MD_PAIR_LEAD_US and MD_PAIR_UC_LEAD_US
		// override them.
		const auto envUs = [](const char* _name) { return envNumber(_name).value_or(-1.0); };
		const auto usToFrames = [](const double _us) { return _us * static_cast<double>(g_samplerate) / 1.0e6; };
		const auto policy = transportPolicy(m_model);
		m_pairQuantumFrames = schedQuantumFrames(m_model);
		const double dspLeadUs = envUs("MD_PAIR_LEAD_US");
		const double dspLeadFrames = usToFrames(dspLeadUs >= 0.0 ? dspLeadUs : policy.pairDspLeadMicroseconds);
		m_pairDspLeadUc = static_cast<uint64_t>(dspLeadFrames * schedUcCyclesPerFrame());
		const double ucLeadUs = envUs("MD_PAIR_UC_LEAD_US");
		m_pairUcLeadFrames = ucLeadUs >= 0.0 ? usToFrames(ucLeadUs) : m_pairQuantumFrames;
		if(const auto minChunk = envCount<uint64_t>("MD_PAIR_MIN_CHUNK"))
			m_pairMinChunkCycles = std::max<uint64_t>(1, *minChunk);
		// DSP2 round-trip hold (pairDspLeadUc), window in microseconds of UC
		// time. MD_PAIR_HOLD_DSP2_US overrides the policy; negative disables.
		const double holdDsp2Us = envNumber("MD_PAIR_HOLD_DSP2_US").value_or(policy.pairHoldDsp2Microseconds);
		m_pairHoldDsp2 = isMonomachine() && holdDsp2Us >= 0.0;
		m_pairHoldDsp2Uc = m_pairHoldDsp2
			? static_cast<uint64_t>(usToFrames(holdDsp2Us) * schedUcCyclesPerFrame()) : 0;
		// The gates must not close on each other. The UC waits until the slower
		// DSP is within the UC lead of it; a DSP's UC gate lets it pass the UC
		// by the DSP lead at most (hostToDspDeadline rounds down), and by
		// nothing during a DSP2 hold. With both leads at zero the UC waits for
		// a DSP to pass it, which the gate forbids, and the machine stops at
		// the handoff. Where the DSP lead can be zero, the UC therefore leads
		// by at least one worker chunk plus the cycle the deadline rounds away:
		// the slower DSP then always has a whole chunk to run while the UC waits.
		if(!m_pairDspLeadUc || m_pairHoldDsp2)
		{
			const double minUcLeadFrames = static_cast<double>(m_pairMinChunkCycles + 1)
				/ static_cast<double>(g_dsp1CyclesPerEsaiFrame);
			if(m_pairUcLeadFrames < minUcLeadFrames)
			{
				std::fprintf(stderr, "[pair] UC lead raised from %.2f to %.2f us: with no DSP lead the gates would close\n",
					m_pairUcLeadFrames * 1.0e6 / g_samplerate, minUcLeadFrames * 1.0e6 / g_samplerate);
				m_pairUcLeadFrames = minUcLeadFrames;
			}
		}
		// Placement. auto keeps this (UC) thread on the physical core it runs on
		// and the worker on the other cores sharing its last-level cache: on a
		// Ryzen 3700X that is ~80% of real time for the Monomachine against
		// 90-97% with free placement (SMT siblings or two CCX). It is the
		// default when this thread is the device's own render thread, never a
		// host's audio thread. MDMM_PAIR_AFFINITY overrides: auto, off, worker
		// (the worker alone, which measured no gain), or u,w to pin this thread
		// to logical CPU u and the worker to w.
		const char* affinity = envOverride("MDMM_PAIR_AFFINITY");
		if(!affinity)
			affinity = m_pairPlacementAllowed.load(std::memory_order_relaxed) ? "auto" : "off";
		if(std::strcmp(affinity, "off") != 0)
		{
			const bool workerOnly = std::strcmp(affinity, "worker") == 0;
			if(workerOnly || std::strcmp(affinity, "auto") == 0)
			{
				uint64_t core = 0, cache = 0;
				if(dsp56k::ThreadTools::getCpuTopology(dsp56k::ThreadTools::getCurrentCpu(), core, cache)
					&& (cache & ~core))
				{
					if(!workerOnly)
						dsp56k::ThreadTools::setCurrentThreadAffinity(core);
					m_pairWorkerAffinity = cache & ~core;
				}
			}
			else
			{
				// A side without digits pins nothing: strtoul reads it as CPU 0.
				char* end = nullptr;
				const auto uc = std::strtoul(affinity, &end, 10);
				const bool ucGiven = end != affinity;
				if(end && *end == ',')
				{
					char* workerEnd = nullptr;
					const auto worker = std::strtoul(end + 1, &workerEnd, 10);
					if(workerEnd != end + 1 && worker < 64)
						m_pairWorkerAffinity = uint64_t{1} << worker;
				}
				if(ucGiven && uc < 64)
					dsp56k::ThreadTools::setCurrentThreadAffinity(uint64_t{1} << uc);
			}
		}
		m_pairBpThreshold = policy.hostTransmitBackpressureThresholdWords;
		m_pairBpRelease = policy.hostTransmitBackpressureReleaseUcCycles;
		for(uint32_t i = 0; i < 2; ++i)
		{
			auto& d = (i == 0) ? m_dspMixer : m_dspProducer;
			m_schedPublished.dspCycles[i].store(d.dsp().getCycles(), std::memory_order_release);
			m_pairBackpressured[i].store(false, std::memory_order_release);
			m_pairBpSince[i] = 0;
		}
		m_producerParked.store(false, std::memory_order_release);
		m_workerExit.store(false, std::memory_order_release);
		m_dspPairWorker.store(true, std::memory_order_release);
		m_dspThreaded[0].store(true, std::memory_order_release);
		m_dspThreaded[1].store(true, std::memory_order_release);
		m_producerWorker = std::thread([this] { pairWorkerLoop(); });
	}

	uint64_t Hardware::pairGateCycles(const uint32_t _dspIndex, const bool _update)
	{
		const uint32_t i = _dspIndex & 1;
		auto& d = (i == 0) ? m_dspMixer : m_dspProducer;
		// The lone producer's gates (producerGateHint), for either DSP: the
		// block target plus one quantum, and never past the UC, whose host
		// words and commands are dated with the UC cycle that issued them.
		const double limit = static_cast<double>(m_schedTargetFrames.load(std::memory_order_acquire))
			+ m_pairQuantumFrames;
		const uint64_t ucCycles = m_schedPublished.ucCycles.load(std::memory_order_acquire);
		// Lead over the UC (spec §3 gate 3, L_lead): see the gate constants in
		// schedTryHandoffPair.
		const uint64_t gate = std::min(schedFrameToDspCycles(i, limit),
			hostToDspDeadline(i, ucCycles + pairDspLeadUc(ucCycles)));
		// Where the serial bridge ran a DSP past the UC, its head item lets it
		// pass the gates by the same envelope: an item due but blocked on HRX
		// or on the previous command (writeWordToDsp, hdiSendIrqToDSP), and a
		// UC stalled on a full host stream (producerTargetCycles). The peer
		// DSP gets the same machine time, since the one being waited for may
		// need it on the link.
		double allowanceFrames = -1.0;
		for(uint32_t x = 0; x < 2; ++x)
		{
			auto& dx = (x == 0) ? m_dspMixer : m_dspProducer;
			uint64_t allowance = dx.hostToDspHeadBlockedAllowance();
			if(m_hostWriteBlocked[x].load(std::memory_order_acquire))
				allowance = std::max(allowance, dx.hostToDspHeadAllowance());
			if(allowance > m_schedDspOriginCycles[x])
				allowanceFrames = std::max(allowanceFrames, m_schedDspOriginFrame[x]
					+ static_cast<double>(allowance - m_schedDspOriginCycles[x])
						/ static_cast<double>(g_dsp1CyclesPerEsaiFrame));
		}
		if(allowanceFrames >= 0.0)
		{
			// Not past the host audio input the audio thread has queued (the
			// block target plus the 64-frame safety margin, spec §3 gate 1),
			// except for a UC stalled on a full stream, which cannot queue more
			// before the DSP lands its item.
			const bool uncapped = m_hostWriteBlocked[0].load(std::memory_order_acquire)
				|| m_hostWriteBlocked[1].load(std::memory_order_acquire);
			if(!uncapped)
				allowanceFrames = std::min(allowanceFrames,
					static_cast<double>(m_schedTargetFrames.load(std::memory_order_acquire))
						+ g_hostAudioInputSafetyFrames / 2);
			const uint64_t allowed = std::max(gate, schedFrameToDspCycles(i, allowanceFrames));
			if(allowed > gate)
				return allowed;
		}
		if(!isMonomachine())
			return gate;
		// MM host traffic is a flow-controlled lossless stream (schedStep): a
		// DSP whose host backlog exceeds the threshold holds until the UC
		// drains it, for at most the release window of UC time. Its UC waits
		// give way meanwhile (m_pairBackpressured), so the UC keeps running and
		// the window always ends.
		const auto setBackpressured = [&](const bool _held)
		{
			if(!_update || m_pairBackpressured[i].load(std::memory_order_relaxed) == _held)
				return;
			m_pairBackpressured[i].store(_held, std::memory_order_release);
			m_signal.notify();
		};
		if(d.hostTxBacklog() <= m_pairBpThreshold)
		{
			if(_update)
				m_pairBpSince[i] = 0;
			setBackpressured(false);
			return gate;
		}
		const uint64_t since = m_pairBpSince[i] ? m_pairBpSince[i] : ucCycles + 1;	// +1: 0 means "not stalled"
		if(_update)
			m_pairBpSince[i] = since;
		if(ucCycles - (since - 1) >= m_pairBpRelease)
		{
			setBackpressured(false);
			return gate;
		}
		setBackpressured(true);
		return std::min(gate, d.dsp().getCycles());
	}

	uint64_t Hardware::pairDspLeadUc(const uint64_t _ucCycles) const
	{
		if(!m_pairHoldDsp2)
			return m_pairDspLeadUc;
		// DSP2 exchanges four words with the UC per link burst, each answered
		// by a host command. A DSP running ahead receives every answer that
		// much late, which stretches the whole exchange until DSP2 misses a
		// burst to the mixer. Holding DSP2 alone lets the mixer run ahead and
		// starve the link, so both DSPs are held (implementation log,
		// 2026-09-29). Both run on this worker: DSP2's state reads are local.
		if(m_dspProducer.hostTxInFlight())
			return 0;
		const uint64_t take = m_dspProducer.lastHostTxTakeUcCycle();
		if(take && _ucCycles < take + m_pairHoldDsp2Uc)
			return 0;
		return m_pairDspLeadUc;
	}

	void Hardware::runPairChunk(const uint32_t _dspIndex, const uint64_t _targetCyc)
	{
		const uint32_t i = _dspIndex & 1;
		auto& d = (i == 0) ? m_dspMixer : m_dspProducer;
		// One codec frame per chunk, ending at a pending host item's deadline
		// (runProducerChunk). The link catch-ups inside may run the other DSP
		// up to this one's time: never past its own gates, which are the same.
		d.serviceHostTransport();
		const uint64_t now = d.dsp().getCycles();
		uint64_t stopCyc = std::min(_targetCyc, now + g_dsp1CyclesPerEsaiFrame);
		const uint64_t hostEvent = d.nextHostTransportCycle();
		if(hostEvent > now)
			stopCyc = std::min(stopCyc, hostEvent);
#ifdef DSP56K_TSC_PROBES
		const uint64_t probeCycles[2] = {m_dspMixer.dsp().getCycles(), m_dspProducer.dsp().getCycles()};
		{
			DSP_PROBE_SCOPE(0, Exec, i);
			d.dsp().execUntilCycles(stopCyc);
		}
		if(auto* const probeCtx = dsp56k::probe::t_ctx)
		{
			probeCtx->cycles[i][0] += ((i == 0) ? m_dspMixer : m_dspProducer).dsp().getCycles() - probeCycles[i];
			probeCtx->cycles[i ^ 1][1] += ((i == 0) ? m_dspProducer : m_dspMixer).dsp().getCycles() - probeCycles[i ^ 1];
		}
#else
		d.dsp().execUntilCycles(stopCyc);
#endif
		// Both: the link catch-ups inside may have run the other DSP too.
		m_dspMixer.publishHostStatus();
		m_dspProducer.publishHostStatus();
		m_schedPublished.dspCycles[0].store(m_dspMixer.dsp().getCycles(), std::memory_order_release);
		m_schedPublished.dspCycles[1].store(m_dspProducer.dsp().getCycles(), std::memory_order_release);
		m_signal.notify();
	}

#ifdef DSP56K_TSC_PROBES
	namespace
	{
		// Pair worker TSC probe session (dsp56kBase/tscprobe.h): installed for the
		// worker's lifetime, reported to stderr when the worker exits.
		struct PairProbeSession
		{
			dsp56k::probe::Ctx ctx;
			dsp56k::DSP* dsps[2];
			dsp56k::probe::Counters start[2];
			uint64_t tscStart = 0;
			std::chrono::steady_clock::time_point wallStart;

			PairProbeSession(dsp56k::DSP& _mixer, dsp56k::DSP& _producer) : dsps{&_mixer, &_producer}
			{
				ctx.level = envCount<uint8_t>("DSP56K_PROBE_LEVEL").value_or(1);
				dsp56k::probe::t_ctx = &ctx;
				dsp56k::probe::calibrate(ctx);
				start[0] = _mixer.probeCounters();
				start[1] = _producer.probeCounters();
				wallStart = std::chrono::steady_clock::now();
				tscStart = dsp56k::probe::now();
				ctx.last = tscStart;
			}

			~PairProbeSession()
			{
				using namespace dsp56k::probe;
				const uint64_t tscEnd = now();
				ctx.bucket(ctx.curDsp, ctx.curCat).self += tscEnd - ctx.last;
				t_ctx = nullptr;
				const double wallNs = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - wallStart).count());
				const double ticks = static_cast<double>(tscEnd - tscStart);
				const double nsPerTick = wallNs / ticks;
				// Calibrate again on a warm core and keep the cheaper cost: the
				// corrected figures stay upper bounds either way.
				Ctx late{ctx.level};
				calibrate(late);
				const double startIn = ctx.kIn, startOut = ctx.kOut;
				ctx.kIn = std::min(ctx.kIn, late.kIn);
				ctx.kOut = std::min(ctx.kOut, late.kOut);
				uint32_t clamped = 0;
				const auto corrected = [&](const Bucket& _b)
				{
					const double v = static_cast<double>(_b.self) - static_cast<double>(_b.calls) * ctx.kIn
						- static_cast<double>(_b.childEnters) * ctx.kOut;
					if(v < 0.0)
						++clamped;
					return std::max(0.0, v);
				};
				std::fprintf(stderr, "[probe] level=%u wall=%.2fs ticks/ns=%.3f kIn=%.1f kOut=%.1f ticks (start %.1f/%.1f, end %.1f/%.1f)\n",
					ctx.level, wallNs * 1e-9, 1.0 / nsPerTick, ctx.kIn, ctx.kOut, startIn, startOut, late.kIn, late.kOut);
				double sumCorrected = 0.0;
				for(uint32_t d = 0; d < 2; ++d)
					for(uint32_t n = 0; n < 2; ++n)
						for(uint32_t c = 0; c < Calib; ++c)
							sumCorrected += corrected(ctx.acc[d][n][c]);
				std::fprintf(stderr, "[probe] corrected ticks cover %.1f%% of wall (rest: probe cost)\n", 100.0 * sumCorrected / ticks);
				std::fprintf(stderr, "[probe] worker: park wall%%=%.2f (%llu parks) | outside chunks wall%%=%.2f\n",
					100.0 * corrected(ctx.acc[0][0][Park]) / ticks, static_cast<unsigned long long>(ctx.acc[0][0][Park].calls),
					100.0 * corrected(ctx.acc[0][0][Outside]) / ticks);
				static const char* const dspNames[2] = {"mixer", "producer"};
				for(uint32_t d = 0; d < 2; ++d)
				{
					const auto& e = dsps[d]->probeCounters();
					const auto& s = start[d];
					const auto diff = [](const uint64_t _a, const uint64_t _b) { return static_cast<unsigned long long>(_a - _b); };
					for(uint32_t n = 0; n < 2; ++n)
					{
						const uint32_t k = n ? 0 : 1;	// skip counters are indexed [nested, own]
						const double cyc = static_cast<double>(ctx.cycles[d][n]);
						const double skipped = static_cast<double>(diff(e.nopSkipped[k], s.nopSkipped[k]) + diff(e.pollSkipped[k], s.pollSkipped[k]));
						const double executed = std::max(1.0, cyc - skipped);
						double dspSum = 0.0;
						for(uint32_t c = Exec; c < Calib; ++c)
							dspSum += corrected(ctx.acc[d][n][c]);
						std::fprintf(stderr, "[probe] %s %s: cycles=%.0f (%.1f M/s, %.1f%% skipped) wall share=%.1f%% ns/cycle=%.3f ns/executed cycle=%.3f\n",
							dspNames[d], n ? "nested" : "own", cyc, cyc / wallNs * 1e3, cyc > 0 ? 100.0 * skipped / cyc : 0.0,
							100.0 * dspSum / ticks, cyc > 0 ? dspSum * nsPerTick / cyc : 0.0, cyc > 0 ? dspSum * nsPerTick / executed : 0.0);
						for(uint32_t c = Exec; c < Calib; ++c)
						{
							const auto& b = ctx.acc[d][n][c];
							if(!b.calls && !b.self)
								continue;
							const double v = corrected(b);
							std::fprintf(stderr, "[probe]   %-10s calls=%12llu (%8.0f/s) wall%%=%6.2f ns/call=%8.1f ns/cycle=%6.3f\n",
								catName(c), static_cast<unsigned long long>(b.calls), static_cast<double>(b.calls) / wallNs * 1e9,
								100.0 * v / ticks, b.calls ? v * nsPerTick / static_cast<double>(b.calls) : 0.0,
								cyc > 0 ? v * nsPerTick / cyc : 0.0);
						}
					}
					std::fprintf(stderr, "[probe] %s callbacks: periph=%llu intr=%llu prevent=%llu nop=%llu | periphDue=%llu intrEmpty=%llu intrMasked=%llu modeChecks=%llu\n",
						dspNames[d], diff(e.checks[0], s.checks[0]), diff(e.checks[1], s.checks[1]), diff(e.checks[2], s.checks[2]),
						diff(e.checks[3], s.checks[3]), diff(e.periphDue, s.periphDue), diff(e.intrEmpty, s.intrEmpty),
						diff(e.intrMasked, s.intrMasked), diff(e.modeChecks, s.modeChecks));
					std::fprintf(stderr, "[probe] %s essi slots: tx0=%llu frames=%llu rx0=%llu idle=%llu | tx1=%llu frames=%llu rx1=%llu idle=%llu\n",
						dspNames[d], diff(e.essiTxSlots[0], s.essiTxSlots[0]), diff(e.essiTxFrames[0], s.essiTxFrames[0]),
						diff(e.essiRxSlots[0], s.essiRxSlots[0]), diff(e.essiRxIdle[0], s.essiRxIdle[0]),
						diff(e.essiTxSlots[1], s.essiTxSlots[1]), diff(e.essiTxFrames[1], s.essiTxFrames[1]),
						diff(e.essiRxSlots[1], s.essiRxSlots[1]), diff(e.essiRxIdle[1], s.essiRxIdle[1]));
					std::fprintf(stderr, "[probe] %s untimed scope entries (level too low):", dspNames[d]);
					for(uint32_t c = Exec; c < Calib; ++c)
						if(ctx.untimedCalls[d][c])
							std::fprintf(stderr, " %s=%llu", catName(c), static_cast<unsigned long long>(ctx.untimedCalls[d][c]));
					std::fprintf(stderr, "\n");
					for(uint32_t n = 0; n < 2; ++n)
					{
						const uint32_t k = n ? 0 : 1;	// counters are indexed [nested, own]
						std::fprintf(stderr, "[probe] %s %s skips: nop calls=%llu skippedCycles=%llu | poll calls=%llu acted=%llu skippedCycles=%llu\n",
							dspNames[d], n ? "nested" : "own", diff(e.nopCalls[k], s.nopCalls[k]), diff(e.nopSkipped[k], s.nopSkipped[k]),
							diff(e.pollCalls[k], s.pollCalls[k]), diff(e.pollActed[k], s.pollActed[k]), diff(e.pollSkipped[k], s.pollSkipped[k]));
						uint64_t reads = 0;
						for(uint32_t r = 0; r < 128; ++r)
							reads += ctx.mmioReads[d][n][r];
						std::fprintf(stderr, "[probe] %s %s mmio reads=%llu (%.0f/s):", dspNames[d], n ? "nested" : "own",
							static_cast<unsigned long long>(reads), static_cast<double>(reads) / wallNs * 1e9);
						for(uint32_t r = 0; r < 128; ++r)
						{
							if(ctx.mmioReads[d][n][r] && ctx.mmioReads[d][n][r] * 100 >= reads)
								std::fprintf(stderr, " $ffff%02x=%llu", 0x80 + r, static_cast<unsigned long long>(ctx.mmioReads[d][n][r]));
						}
						std::fprintf(stderr, "\n");
					}
				}
				std::fprintf(stderr, "[probe] buckets clamped by the correction: %u\n", clamped);
			}
		};
	}
#endif

	void Hardware::pairWorkerLoop()
	{
		// The audio thread waits on this worker for every block's codec
		// frames: share the host's audio scheduling band (producerWorkerLoop).
		struct ProAudioTask
		{
			void* task = nullptr;
			~ProAudioTask() { dsp56k::ThreadTools::leaveProAudioTask(task); }
		} proAudio;
		const char* const priority = std::getenv("MDMM_WORKER_PRIORITY");
		if(priority && std::strcmp(priority, "high") == 0)
			dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
		else if(!priority || std::strcmp(priority, "normal") != 0)
			proAudio.task = dsp56k::ThreadTools::joinProAudioTask();
		dsp56k::ThreadTools::setCurrentThreadName("MD DSPs");
		if(m_pairWorkerAffinity)
			dsp56k::ThreadTools::setCurrentThreadAffinity(m_pairWorkerAffinity);
#ifdef DSP56K_TSC_PROBES
		PairProbeSession probeSession(m_dspMixer.dsp(), m_dspProducer.dsp());
#endif
		const bool trace = m_transportTrace;
		std::array<uint64_t, 2> gate{};
		const auto runnable = [&](const uint32_t _i)
		{
			auto& d = (_i == 0) ? m_dspMixer : m_dspProducer;
			// A head item may become applicable with the DSP held exactly at
			// its gate (a drain window ending there): land what is due first,
			// or nothing would, and the UC waiting on stream room with it.
			d.serviceHostTransport();
			d.publishHostStatus();
			gate[_i] = pairGateCycles(_i);
			// Gather the UC's frequent small publications into chunks of half a
			// frame: a chunk's fixed cost is otherwise as large as its work.
			const uint64_t minChunk = m_pairUrgent.load(std::memory_order_acquire) ? 1 : m_pairMinChunkCycles;
			return d.dsp().getCycles() + minChunk <= gate[_i];
		};
		const auto nowNs = []
		{
			return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		};
		for(;;)
		{
			if(m_workerExit.load(std::memory_order_acquire))
				return;
			const uint64_t turnStart = trace ? nowNs() : 0;
			const bool run0 = runnable(0);
			const bool run1 = runnable(1);
			if(!run0 && !run1)
			{
				// Both gated: park until a publication (UC position, block
				// target, drained backlog) opens a gate. Bounded, so a lost
				// wake costs at most the timeout.
				m_producerParked.store(true, std::memory_order_release);
				m_signal.notify();
				const auto parkStart = trace ? std::chrono::steady_clock::now()
					: std::chrono::steady_clock::time_point{};
				PairIdle reason = PairIdle::UcGate;
				uint64_t parkStartNs = 0;
				if(trace)
				{
					// The laggard is what holds the machine back.
					const uint32_t laggard = schedDspFramePos(1) < schedDspFramePos(0) ? 1u : 0u;
					reason = pairIdleReason(laggard, gate[laggard]);
					parkStartNs = nowNs();
					m_pairTrace.turnNs.fetch_add(parkStartNs - turnStart, std::memory_order_relaxed);
				}
				// The predicate runs under the signal's mutex: gates are only
				// read here. An item that becomes due exactly at a closed gate
				// lands at the next turn, the timeout at the latest.
				const auto wouldRun = [&](const uint32_t _i)
				{
					const uint64_t minChunk = m_pairUrgent.load(std::memory_order_acquire) ? 1 : m_pairMinChunkCycles;
					return ((_i == 0) ? m_dspMixer : m_dspProducer).dsp().getCycles() + minChunk
						<= pairGateCycles(_i, false);
				};
				{
					DSP_PROBE_SCOPE(0, Park, 0);
					m_signal.waitFor(std::chrono::microseconds(500), [&]
					{
						return m_workerExit.load(std::memory_order_acquire) || wouldRun(0) || wouldRun(1);
					});
				}
				if(trace)
				{
					m_timeTrace.workerParks.fetch_add(1, std::memory_order_relaxed);
					m_timeTrace.workerParkNs.fetch_add(static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - parkStart).count()), std::memory_order_relaxed);
					const auto r = static_cast<size_t>(reason);
					m_pairTrace.idles[r].fetch_add(1, std::memory_order_relaxed);
					m_pairTrace.idleNs[r].fetch_add(nowNs() - parkStartNs, std::memory_order_relaxed);
				}
				m_producerParked.store(false, std::memory_order_release);
				continue;
			}
			// Laggard first, like the serial scheduler's DSP choice.
			uint32_t idx = run0 ? 0 : 1;
			if(run0 && run1 && schedDspFramePos(1) < schedDspFramePos(0))
				idx = 1;
			const auto chunkStart = trace ? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			const uint64_t chunkStartNs = trace ? nowNs() : 0;
			if(trace)
				m_pairTrace.turnNs.fetch_add(chunkStartNs - turnStart, std::memory_order_relaxed);
			const uint64_t startCycles = m_dspMixer.dsp().getCycles() + m_dspProducer.dsp().getCycles();
			runPairChunk(idx, gate[idx]);
			if(trace)
			{
				m_timeTrace.workerExecNs.fetch_add(static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - chunkStart).count()), std::memory_order_relaxed);
				const uint64_t cycles = m_dspMixer.dsp().getCycles() + m_dspProducer.dsp().getCycles() - startCycles;
				m_timeTrace.workerExecCycles.fetch_add(cycles, std::memory_order_relaxed);
				m_pairTrace.chunks.fetch_add(1, std::memory_order_relaxed);
				m_pairTrace.chunkCycles.fetch_add(cycles, std::memory_order_relaxed);
				m_pairTrace.chunkNs.fetch_add(nowNs() - chunkStartNs, std::memory_order_relaxed);
			}
		}
	}

	Hardware::PairIdle Hardware::pairIdleReason(const uint32_t _dspIndex, const uint64_t _gate)
	{
		const uint32_t i = _dspIndex & 1;
		if(m_pairBackpressured[i].load(std::memory_order_relaxed))
			return PairIdle::Backpressure;
		const uint64_t cycles = ((i == 0) ? m_dspMixer : m_dspProducer).dsp().getCycles();
		if(cycles < _gate)
			return PairIdle::Gathering;
		const uint64_t targetGate = schedFrameToDspCycles(i,
			static_cast<double>(m_schedTargetFrames.load(std::memory_order_relaxed)) + m_pairQuantumFrames);
		const uint64_t ucCycles = m_schedPublished.ucCycles.load(std::memory_order_relaxed);
		const uint64_t ucGate = hostToDspDeadline(i, ucCycles + pairDspLeadUc(ucCycles));
		return targetGate <= ucGate ? PairIdle::BlockTarget : PairIdle::UcGate;
	}

	void Hardware::waitMixerAtTarget()
	{
		// The block's codec frames come from the mixer on the pair worker. It
		// can always reach the block target: its gates are the target plus a
		// quantum and the UC, which is there already. MM backpressure may hold
		// it short; the serial scheduler then ended the block short as well.
		const uint64_t targetCyc = std::min(schedFrameToDspCycles(0, m_schedFramesTotal),
			hostToDspDeadline(0, m_schedUcCyclesDone));
		m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
		m_signal.notify();
		const auto ready = [&]
		{
			return m_schedPublished.dspCycles[0].load(std::memory_order_acquire) >= targetCyc
				|| m_pairBackpressured[0].load(std::memory_order_acquire);
		};
		if(m_transportTrace && !ready())
		{
			const auto waitStart = std::chrono::steady_clock::now();
			waitTransport(TransportWaitSite::MixerGate, std::chrono::milliseconds(50), ready);
			m_pairTrace.mixerWaits.fetch_add(1, std::memory_order_relaxed);
			m_pairTrace.mixerWaitNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - waitStart).count()), std::memory_order_relaxed);
			return;
		}
		waitTransport(TransportWaitSite::MixerGate, std::chrono::milliseconds(50), ready);
	}

	void Hardware::stopProducerWorker()
	{
		if(!m_producerWorker.joinable())
			return;
		m_workerExit.store(true, std::memory_order_release);
		m_signal.notify();
		m_producerWorker.join();
	}

	void Hardware::enableParallelTransport()
	{
		if(const auto mode = parseTransportMode(std::getenv("MDMM_TRANSPORT")))
			m_transportMode = *mode;
		if(const auto help = envCount<uint32_t>("MDMM_PRODUCER_HELP_US"))
			m_producerHelpDelayUs = *help;
	}

	uint64_t Hardware::producerGateHint() const
	{
		// Gates of the worker (spec §3): block target, mixer + L_lead,
		// UC + L_lead, all in machine frames; L_lead = the serial quantum.
		const double quantum = schedQuantumFrames(m_model);
		const double ucPerFrame = schedUcCyclesPerFrame();
		// The UC ends its last slice a few cycles past the block target and
		// its host accesses then need the producer at that exact time, so the
		// block gate carries one quantum of slack (well inside the 64-frame
		// host-input safety margin the receiver reads are covered by).
		double limit = static_cast<double>(m_schedTargetFrames.load(std::memory_order_acquire))
			+ quantum;
		// No lead over the UC at all: a host word or command is dated with
		// the UC cycle that issued it and lands when the producer reaches
		// that time. Had the producer already run past it, the word would
		// land late in DSP time - the serial bridge caught the DSP up to the
		// UC before every write, never the other way round, and the firmware
		// relies on that ordering (a word arriving inside a masked sync wait
		// is never acknowledged). The producer therefore follows the UC's
		// published position and may only trail it.
		(void)ucPerFrame;
		// No mixer gate: the mixer is held within D of the UC on the audio
		// thread, so bounding the producer by the UC bounds it against the
		// mixer too, and the producer never waits on the mixer (no cycle).
		// The UC gate uses the exact rational conversion the HI08 bridge uses
		// for its own deadline (waitForDspTime), so the producer can always
		// reach precisely what the UC waits for - a floating-point frame
		// bound could fall a few cycles short and turn every host access into
		// an expired wait.
		const uint64_t ucLimit = hostToDspDeadline(1,
			m_schedPublished.ucCycles.load(std::memory_order_acquire));
		return std::min(schedFrameToDspCycles(1, limit), ucLimit);
	}

	uint64_t Hardware::producerTargetCycles() const
	{
		uint64_t cycles = producerGateHint();
		// While the UC is stalled on a full host stream (its time frozen), the
		// head item entitles the producer to run past its gates by exactly the
		// drain run the serial bridge gave that write: the inline clamp from
		// where the write started (its deadline, or the landing of the item
		// before it). A firmware that does not read therefore gets one clamp of
		// DSP time per blocked item - enough for a link wait to reach its own
		// timeout. At any other time the producer stays behind the UC: a
		// producer running ahead starves its link and Port C sync from the
		// mixer, spends its cycles waiting on them, drains the host stream ever
		// slower, and shows the UC host-port state from its own future.
		if(m_hostWriteBlocked[1].load(std::memory_order_acquire))
			cycles = std::max(cycles, m_dspProducer.hostToDspHeadAllowance());
		return cycles;
	}

	void Hardware::producerWorkerLoop()
	{
		// The audio thread waits on this worker many times per block, so the
		// worker must share the scheduling band of the host's audio threads:
		// otherwise the host's other engine threads (MMCSS "Pro Audio" in
		// Ableton Live) preempt it on a loaded machine and every block stalls.
		// MDMM_WORKER_PRIORITY=normal|high overrides it for A/B measurements
		// (high = THREAD_PRIORITY_TIME_CRITICAL without MMCSS).
		struct ProAudioTask
		{
			void* task = nullptr;
			~ProAudioTask() { dsp56k::ThreadTools::leaveProAudioTask(task); }
		} proAudio;
		const char* const priority = std::getenv("MDMM_WORKER_PRIORITY");
		if(priority && std::strcmp(priority, "high") == 0)
			dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
		else if(!priority || std::strcmp(priority, "normal") != 0)
			proAudio.task = dsp56k::ThreadTools::joinProAudioTask();
		dsp56k::ThreadTools::setCurrentThreadName("MD DSP2");
		auto& d = m_dspProducer;
		const bool trace = envOverride("MDMM_TRANSPORT_TRACE") != nullptr;
		uint64_t chunks = 0, parks = 0;
		// The audio thread may be executing the producer (tryHelpProducer):
		// outside m_producerExec only published values are readable, so the
		// gate is judged from the hint. A blocked host write lets the producer
		// run past it by the head item's allowance, which only the lock holder
		// can read: while a write is blocked the worker takes the lock to find
		// out, and once it finds the allowance used up it records where
		// (position and blocking episode) so it parks instead of retrying
		// until either moves.
		uint64_t spentAt = std::numeric_limits<uint64_t>::max();
		uint32_t spentEpisode = 0;
		const auto gateOpen = [&]
		{
			const uint64_t published = m_schedPublished.dspCycles[1].load(std::memory_order_acquire);
			if(producerGateHint() > published)
				return true;
			return m_hostWriteBlocked[1].load(std::memory_order_acquire)
				&& (published != spentAt
					|| m_hostWriteBlockEpisode[1].load(std::memory_order_acquire) != spentEpisode);
		};
		for(;;)
		{
			if(m_workerExit.load(std::memory_order_acquire))
				return;
			const uint64_t targetCyc = producerGateHint();
			const uint64_t now = m_schedPublished.dspCycles[1].load(std::memory_order_acquire);
			if(trace && (chunks % 4410) == 0)
				std::fprintf(stderr, "[worker] chunks=%llu helped=%llu parks=%llu now=%llu target=%llu "
					"blockTarget=%llu uc=%llu mixer=%llu clamps dsp=%llu link=%llu room=%llu parked=%llu\n",
					static_cast<unsigned long long>(chunks),
					static_cast<unsigned long long>(m_producerHelpedChunks.load(std::memory_order_relaxed)),
					static_cast<unsigned long long>(parks),
					static_cast<unsigned long long>(now), static_cast<unsigned long long>(targetCyc),
					static_cast<unsigned long long>(m_schedTargetFrames.load()),
					static_cast<unsigned long long>(m_schedPublished.ucCycles.load()),
					static_cast<unsigned long long>(m_schedPublished.dspCycles[0].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[0].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[1].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[2].load()),
					static_cast<unsigned long long>(m_transportWaitClamps[3].load()));
			if(!gateOpen())
			{
				++parks;
				// Gated: park until a publication moves a gate. The wait is
				// bounded so a lost wake can only cost a few microseconds.
				m_producerParked.store(true, std::memory_order_release);
				m_signal.notify();
				const auto parkStart = std::chrono::steady_clock::now();
				uint32_t parkRounds = 0;
				while(!m_signal.waitFor(std::chrono::microseconds(500), [&]
				{
					return m_workerExit.load(std::memory_order_acquire) || gateOpen();
				}))
				{
					if(trace)
						m_timeTrace.workerParkTimeouts.fetch_add(1, std::memory_order_relaxed);
					if(trace && (++parkRounds % 200) == 0)
						std::fprintf(stderr, "[worker] parked %lld ms: now=%llu target=%llu "
							"blockTarget=%llu uc=%llu mixer=%llu\n",
							static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::steady_clock::now() - parkStart).count()),
							static_cast<unsigned long long>(m_schedPublished.dspCycles[1].load()),
							static_cast<unsigned long long>(producerGateHint()),
							static_cast<unsigned long long>(m_schedTargetFrames.load()),
							static_cast<unsigned long long>(m_schedPublished.ucCycles.load()),
							static_cast<unsigned long long>(m_schedPublished.dspCycles[0].load()));
				}
				if(trace)
				{
					m_timeTrace.workerParks.fetch_add(1, std::memory_order_relaxed);
					m_timeTrace.workerParkNs.fetch_add(static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - parkStart).count()), std::memory_order_relaxed);
				}
				m_producerParked.store(false, std::memory_order_release);
				continue;
			}
			std::lock_guard<std::mutex> exec(m_producerExec);
			if(trace && (chunks % 4410) == 0)
			{
				auto& s = d.hostToDspTrace();
				std::fprintf(stderr, "[worker]   stream depth=%zu lastLand=%llu headAllowance=%llu ucGate=%llu "
					"pushed data=%llu cmd=%llu landed read=%llu chunk=%llu overdue=%llu\n",
					d.hostToDspDepth(), static_cast<unsigned long long>(d.hostToDspLastLand()),
					static_cast<unsigned long long>(d.hostToDspHeadAllowance()),
					static_cast<unsigned long long>(hostToDspDeadline(1, m_schedPublished.ucCycles.load())),
					static_cast<unsigned long long>(s.pushedData.load()),
					static_cast<unsigned long long>(s.pushedCommands.load()),
					static_cast<unsigned long long>(s.landedOnRead.load()),
					static_cast<unsigned long long>(s.landedInChunk.load()),
					static_cast<unsigned long long>(s.landedOverdue.load()));
			}
			const uint64_t startCyc = d.dsp().getCycles();
			const auto chunkStart = trace ? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			// The authoritative gate, with the blocked flag read once: only a
			// gate that included the allowance may record it as used up.
			const bool writeBlocked = m_hostWriteBlocked[1].load(std::memory_order_acquire);
			const uint32_t episode = m_hostWriteBlockEpisode[1].load(std::memory_order_acquire);
			uint64_t gate = producerGateHint();
			if(writeBlocked)
				gate = std::max(gate, d.hostToDspHeadAllowance());
			if(!runProducerChunk(gate))
			{
				// Already at the gate: the audio thread covered the gap while
				// we waited for the lock, or the allowance is used up.
				if(writeBlocked)
				{
					spentAt = startCyc;
					spentEpisode = episode;
				}
				continue;
			}
			++chunks;
			m_workerChunks.store(chunks, std::memory_order_relaxed);
			if(trace)
			{
				const auto chunkNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - chunkStart).count();
				m_timeTrace.workerExecNs.fetch_add(static_cast<uint64_t>(chunkNs), std::memory_order_relaxed);
				m_timeTrace.workerExecCycles.fetch_add(d.dsp().getCycles() - startCyc, std::memory_order_relaxed);
				const auto chunkMs = chunkNs / 1000000;
				if(chunkMs > 50)
					std::fprintf(stderr, "[worker] slow chunk %lld ms: cycles %llu -> %llu pc=%06x\n",
						static_cast<long long>(chunkMs), static_cast<unsigned long long>(startCyc),
						static_cast<unsigned long long>(d.dsp().getCycles()), d.dsp().getPC().toWord());
			}
			if(trace && d.dsp().getCycles() == startCyc)
				std::fprintf(stderr, "[worker] no progress at %llu\n",
					static_cast<unsigned long long>(startCyc));
		}
	}

	bool Hardware::runProducerChunk(const uint64_t _targetCyc)
	{
		auto& d = m_dspProducer;
		const uint64_t now = d.dsp().getCycles();
		if(now >= _targetCyc)
			return false;
		// One codec frame per chunk: the position publication and the
		// host-port service run at frame granularity. A pending host item
		// ends the chunk at its deadline, so it lands exactly when the
		// serial bridge would have written it rather than up to a frame
		// late.
		d.applyHostToDspStream();
		d.stageHostTx();
		uint64_t stopCyc = std::min(_targetCyc, now + g_dsp1CyclesPerEsaiFrame);
		const uint64_t headDeadline = d.hostToDspHeadDeadline();
		if(headDeadline > now)
			stopCyc = std::min(stopCyc, headDeadline);
		d.dsp().execUntilCycles(stopCyc);
		m_schedPublished.dspCycles[1].store(d.dsp().getCycles(), std::memory_order_release);
		m_signal.notify();
		return true;
	}

	bool Hardware::tryHelpProducer()
	{
		std::unique_lock<std::mutex> exec(m_producerExec, std::try_to_lock);
		if(!exec.owns_lock() || !runProducerChunk(producerTargetCycles()))
			return false;
		m_producerHelpedChunks.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool Hardware::waitSignalOrHelp(const std::chrono::microseconds _timeout,
		const std::function<bool()>& _ready)
	{
		// The audio thread waits here for the producer (host access, link
		// word). When every core is busy the OS may not schedule the worker
		// for milliseconds, and the audio thread would sit idle on work it
		// can do itself. If the producer's published position has not moved
		// for m_producerHelpDelayUs, run its chunks here until the wait is
		// satisfied or the worker takes the producer back. The worker
		// keeps the producer whenever it runs: one failed try-lock ends the
		// help, and the next wait gives it the delay again.
		// The pair worker owns both DSPs: nothing to help with in isolation.
		if(m_producerHelpDelayUs == 0 || !m_dspThreaded[1].load(std::memory_order_acquire)
			|| m_dspPairWorker.load(std::memory_order_acquire))
			return m_signal.waitFor(_timeout, _ready);
		const auto deadline = std::chrono::steady_clock::now() + _timeout;
		const std::chrono::microseconds delay(m_producerHelpDelayUs);
		for(;;)
		{
			if(m_audioHelpsProducer)
			{
				if(tryHelpProducer())
				{
					if(_ready())
						return true;
					if(std::chrono::steady_clock::now() >= deadline)
						return false;
					continue;
				}
				m_audioHelpsProducer = false;
			}
			const auto now = std::chrono::steady_clock::now();
			if(now >= deadline)
				return _ready();
			const uint64_t before = m_schedPublished.dspCycles[1].load(std::memory_order_acquire);
			const auto slice = std::min<std::chrono::steady_clock::duration>(delay, deadline - now);
			if(m_signal.waitFor(std::chrono::duration_cast<std::chrono::microseconds>(slice), _ready))
				return true;
			if(m_schedPublished.dspCycles[1].load(std::memory_order_acquire) == before)
				m_audioHelpsProducer = true;
		}
	}

	bool Hardware::waitTransportImpl(const TransportWaitSite _site,
		const std::chrono::microseconds _timeout, const std::function<bool()>& _ready)
	{
		if(_ready())
			return true;
		// The pair worker gathers short chunks into longer ones unless this
		// thread waits on it.
		struct Urgency
		{
			std::atomic<uint32_t>* count;
			~Urgency() { if(count) count->fetch_sub(1, std::memory_order_acq_rel); }
		} urgency{m_dspPairWorker.load(std::memory_order_acquire) ? &m_pairUrgent : nullptr};
		if(urgency.count)
		{
			urgency.count->fetch_add(1, std::memory_order_acq_rel);
			m_signal.notify();
		}
		const auto siteIndex = static_cast<size_t>(_site);
		if(m_transportTrace)
			m_timeTrace.waits[siteIndex].fetch_add(1, std::memory_order_relaxed);
		const auto phaseBefore = m_audioPhase.exchange(10 + static_cast<int32_t>(_site));
		struct PhaseRestore
		{
			std::atomic<int32_t>& phase; int32_t value;
			~PhaseRestore() { phase.store(value); }
		} restore{m_audioPhase, phaseBefore};
		const auto start = std::chrono::steady_clock::now();
		for(;;)
		{
			// The mixer may advance only within the laggard-first envelope
			// (UC + quantum): beyond it the producer, gated by the UC, could
			// never reach what the mixer's own link wait needs. Publish the UC
			// position first so the producer's gate and this bound agree.
			m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
			const double quantum = schedQuantumFrames(m_model);
			const double ucFrames = static_cast<double>(m_schedUcCyclesDone) / schedUcCyclesPerFrame();
			double bound = std::min(m_schedFramesTotal, ucFrames + quantum);
			if(m_dspThreaded[1].load(std::memory_order_acquire))
				bound = std::min(bound, ucFrames + m_linkPipelineDepthFrames);
			if(m_schedDspOriginLatched[0] && !m_dspThreaded[0].load(std::memory_order_acquire)
				&& schedDspFramePos(0) < bound)
			{
				schedRunDspSlice(0, std::min(schedDspFramePos(0) + quantum, bound));
				if(_ready())
					return true;
				continue;
			}
			const auto blockStart = m_transportTrace ? std::chrono::steady_clock::now()
				: std::chrono::steady_clock::time_point{};
			const bool woke = waitSignalOrHelp(std::chrono::microseconds(200), _ready);
			if(m_transportTrace)
			{
				m_timeTrace.blockedNs[siteIndex].fetch_add(static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now() - blockStart).count()), std::memory_order_relaxed);
				if(!woke)
					m_timeTrace.timedOutRounds[siteIndex].fetch_add(1, std::memory_order_relaxed);
			}
			if(woke)
				return true;
			const auto waited = std::chrono::steady_clock::now() - start;
			if(m_transportTrace && waited > std::chrono::seconds(1))
			{
				std::fprintf(stderr, "[audio] waiting site=%u for %lld ms: uc=%.1f mixer=%.1f producer=%.1f bound=%.1f target=%.1f parked=%d producerTarget=%llu lease=%.1f\n",
					static_cast<unsigned>(_site),
					static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(waited).count()),
					ucFrames, m_schedDspOriginLatched[0] ? schedDspFramePos(0) : -1.0,
					producerPublishedFrames(), bound, m_schedFramesTotal,
					m_producerParked.load() ? 1 : 0,
					static_cast<unsigned long long>(producerTargetCycles()), 0.0);
				m_dspProducer.traceHostStream("[audio]   producer");
			}
			if(waited > _timeout)
			{
				m_transportWaitClamps[static_cast<size_t>(_site)].fetch_add(1,
					std::memory_order_relaxed);
				return false;
			}
		}
	}

	void Hardware::waitProducerParked()
	{
		if(!m_dspThreaded[1].load(std::memory_order_acquire))
			return;
		if(m_dspPairWorker.load(std::memory_order_acquire))
		{
			// The pair worker parks only with both DSPs at their gates, which
			// stay put while this thread holds the UC.
			if(!m_signal.waitFor(std::chrono::milliseconds(200), [&]
				{
					return m_producerParked.load(std::memory_order_acquire);
				}))
				m_transportWaitClamps[static_cast<size_t>(TransportWaitSite::ProducerParked)]
					.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto parked = [&]
		{
			return m_producerParked.load(std::memory_order_acquire)
				&& m_schedPublished.dspCycles[1].load(std::memory_order_acquire)
					>= producerTargetCycles();
		};
		if(!m_signal.waitFor(std::chrono::milliseconds(200), parked))
			m_transportWaitClamps[static_cast<size_t>(TransportWaitSite::ProducerParked)]
				.fetch_add(1, std::memory_order_relaxed);
	}

	uint64_t Hardware::hostToDspDeadline(const uint32_t _dspIndex, const uint64_t _ucCycle) const
	{
		const auto i = _dspIndex & 1;
		if(!m_schedDspOriginLatched[i] || _ucCycle <= m_schedDspOriginUcCycles[i])
			return 0;
		return dspCatchupDeadline<g_ucClockHz, g_dsp1CyclesPerEsaiFrame * g_samplerate>(
			m_schedDspOriginCycles[i], _ucCycle - m_schedDspOriginUcCycles[i]);
	}

	void Hardware::waitForDspTime(const uint32_t _dspIndex)
	{
		const uint32_t i = _dspIndex & 1;
		if(!m_dspThreaded[i].load(std::memory_order_acquire))
		{
			schedCatchUpDsp(_dspIndex);
			return;
		}
		// Threaded adapter (spec §5.5): the DSP's state at the UC's current
		// machine time becomes observable once its published position has
		// reached that time. Publish the UC position first so the worker's
		// UC gate can open, then let the mixer keep advancing while we wait:
		// the audio thread owns UC and mixer, and the producer's mixer gate
		// would otherwise stall it. The minimum is never gated (§3).
		const uint64_t targetCyc = hostToDspDeadline(i, m_schedUcCyclesDone);
		m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
		m_signal.notify();
		// A DSP held by MM backpressure on the pair worker is where the serial
		// catch-up stopped: at the threshold, short of the UC.
		const auto ready = [&]
		{
			return m_schedPublished.dspCycles[i].load(std::memory_order_acquire) >= targetCyc
				|| m_pairBackpressured[i].load(std::memory_order_acquire);
		};
		const bool mustWait = m_transportTrace && !ready();
		const auto waitStart = mustWait ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		const bool reached = waitTransport(TransportWaitSite::DspTime, std::chrono::milliseconds(50), ready);
		if(mustWait)
		{
			m_pairTrace.dspTimeWaits.fetch_add(1, std::memory_order_relaxed);
			m_pairTrace.dspTimeWaitNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - waitStart).count()), std::memory_order_relaxed);
		}
		if(!reached && m_transportTrace)
		{
			static std::atomic<uint32_t> s_reports{0};
			if((s_reports.fetch_add(1) % 50) == 0)
				std::fprintf(stderr, "[audio] DspTime expired: target=%llu published=%llu producerTarget=%llu parked=%d ucNow=%llu ucPublished=%llu\n",
					static_cast<unsigned long long>(targetCyc),
					static_cast<unsigned long long>(m_schedPublished.dspCycles[i].load()),
					static_cast<unsigned long long>(producerTargetCycles()),
					m_producerParked.load() ? 1 : 0,
					static_cast<unsigned long long>(m_schedUcCyclesDone),
					static_cast<unsigned long long>(m_schedPublished.ucCycles.load()));
		}
	}

	bool Hardware::linkRxAvailable(const uint32_t _consumer)
	{
		auto& tick = m_linkRxTick[_consumer].value;
		tick.cycles = ~0ull;
		if(linkDisposeAtConsumer(_consumer, true))
			return false;
		const double now = linkConsumerNow(_consumer);
		// With one thread running both DSPs, nothing reaches this ring or the
		// consumer's receiver before the pop of this slot: leave it the
		// disposal just done and this position (blockingPop).
		if(dspsShareThread())
			tick = {((_consumer == 0) ? m_dspMixer : m_dspProducer).dsp().getCycles(), now};
		if(linkHeadDue(_consumer, now))
			return true;
		// Mixer input with a threaded producer: an empty (or immature) ring
		// is genuinely empty only if the producer has already passed the
		// time this slot needs (pop rule (c)); otherwise wait for it (d).
		if(_consumer != 0 || dspsShareThread() || !m_schedDspOriginLatched[1])
			return false;
		const double needed = now - m_linkPipelineDepthFrames;
		// Never wait for more than the producer's own UC gate can grant (the
		// exact rational conversion it uses): the UC runs on this thread and
		// cannot advance while the mixer waits here, so a need past it could
		// only expire. Floating-point rounding or a JIT block overshoot past
		// the mixer's cap would otherwise ask for a few cycles beyond the UC.
		const uint64_t neededCyc = std::min(schedFrameToDspCycles(1, needed),
			hostToDspDeadline(1, m_schedUcCyclesDone));
		const auto proven = [&]
		{
			return linkHeadDue(0)
				|| m_schedPublished.dspCycles[1].load(std::memory_order_acquire) >= neededCyc;
		};
		if(proven())
			return linkHeadDue(0);
		// This runs inside the mixer's own execution: publish the current
		// mixer AND UC positions first, or the producer's gates keep it parked
		// on stale slice-start values and this wait can only expire.
		m_schedPublished.dspCycles[0].store(m_dspMixer.dsp().getCycles(),
			std::memory_order_release);
		m_schedPublished.ucCycles.store(m_schedUcCyclesDone, std::memory_order_release);
		m_signal.notify();
		const auto phaseBefore = m_audioPhase.exchange(11);
		const auto linkWaitStart = m_transportTrace ? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		const bool provenNow = waitSignalOrHelp(std::chrono::milliseconds(5), proven);
		if(m_transportTrace)
		{
			constexpr auto site = static_cast<size_t>(TransportWaitSite::LinkProducer);
			m_timeTrace.waits[site].fetch_add(1, std::memory_order_relaxed);
			m_timeTrace.blockedNs[site].fetch_add(static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - linkWaitStart).count()), std::memory_order_relaxed);
			if(!provenNow)
				m_timeTrace.timedOutRounds[site].fetch_add(1, std::memory_order_relaxed);
		}
		m_audioPhase.store(phaseBefore);
		if(!provenNow)
		{
			const auto clamps = m_transportWaitClamps[static_cast<size_t>(TransportWaitSite::LinkProducer)]
				.fetch_add(1, std::memory_order_relaxed);
			if(m_transportTrace && (clamps % 100) == 0)
				std::fprintf(stderr, "[audio] link wait expired (#%llu): mixer=%.1f needed producer>=%.1f published=%.1f ring=%zu\n",
					static_cast<unsigned long long>(clamps + 1), linkConsumerNow(0), needed,
					producerPublishedFrames(), m_linkRing[0].size());
		}
		return linkHeadDue(0);
	}

	uint64_t Hardware::hostRxReadyCycle(const uint32_t _dspIndex,
		const uint64_t _dspCycle) const
	{
		// Use integer boot coordinates so even a long-running machine retains
		// the fractional remainder that determines the first safe host cycle.
		const auto index = _dspIndex & 1;
		if(!m_schedDspOriginLatched[index]
			|| _dspCycle < m_schedDspOriginCycles[index])
			return m_schedUcCyclesDone;
		return hostReceiveDeadline<g_ucClockHz, g_dsp1CyclesPerEsaiFrame * g_samplerate>(
			m_schedDspOriginUcCycles[index], _dspCycle - m_schedDspOriginCycles[index]);
	}

	void Hardware::schedCatchUpDsp(const uint32_t _dspIndex)
	{
		// Run the target DSP inline up to the UC's current machine time
		// (the caller's point in the boot handshake) before a host access. This is what advances the
		// DSP in fine lockstep with the UC's poll loops, so the UC's ISR/reply polls converge instead
		// of spinning while the DSP is frozen for the UC's whole background quantum. Bounded by the
		// catch-up clamp; monotone (never runs the DSP backwards or past the UC).
		const uint32_t i = _dspIndex & 1;
#if MD_TRANSPORT_DIAGNOSTICS
		auto& score = m_transportScorecard.coldFireToDsp[i];
		++score.calls;
#endif
		if(!m_schedDspOriginLatched[i])
		{
			MD_TRANSPORT_RECORD(++score.originUnavailable;);
			return;									// not yet rate-locked (still booting) - nothing to catch up
		}
		auto& d = (i == 0) ? m_dspMixer : m_dspProducer;

		if(m_schedUcCyclesDone <= m_schedDspOriginUcCycles[i])
		{
			MD_TRANSPORT_RECORD(++score.timeUnavailable;);
			return;
		}
		const uint64_t targetCyc = dspCatchupDeadline<g_ucClockHz,
			g_dsp1CyclesPerEsaiFrame * g_samplerate>(m_schedDspOriginCycles[i],
				m_schedUcCyclesDone - m_schedDspOriginUcCycles[i]);
		const uint64_t startCyc = d.dsp().getCycles();
		if(startCyc >= targetCyc)
		{
			MD_TRANSPORT_RECORD(++score.alreadyAtTarget;);
			return;
		}
		const auto policy = transportPolicy(m_model);
		const uint64_t clampStop = startCyc + policy.catchUpMaxDspCycles;
		MD_TRANSPORT_RECORD(const auto requested = targetCyc - startCyc;
			score.requestedCycles += requested;
			score.maximumRequestedCycles = std::max(score.maximumRequestedCycles, requested););
		// MM flow control: a host-TX-backlogged DSP does not advance in catch-up either - the
		// catch-up loops are how a DSP outruns the UC by thousands of words in the first place
		// (see the schedStep backpressure comment). MD path untouched.
		const bool s_mmBp = isMonomachine();
		if(!s_mmBp)
		{
			// Same stop rule as the per-block loop below (the first block that
			// reaches the target), under one trampoline entry instead of one per
			// block. No measurable change on the host bench, kept as the cheaper
			// equivalent.
			d.dsp().execUntilCycles(std::min(targetCyc, clampStop));
		}
		else
		{
			while(d.dsp().getCycles() < targetCyc && d.dsp().getCycles() < clampStop
				&& d.hostTxBacklog() <= policy.hostTransmitBackpressureThresholdWords)
				d.dsp().exec();
		}
		MD_TRANSPORT_RECORD(const auto executed = d.dsp().getCycles() - startCyc;
			score.executedCycles += executed;
			score.maximumExecutedCycles = std::max(score.maximumExecutedCycles, executed);
			if(d.dsp().getCycles() >= targetCyc)
				++score.reachedTarget;
			else if(d.dsp().getCycles() >= clampStop)
				++score.hitClamp;
			else if(s_mmBp && d.hostTxBacklog()
				> policy.hostTransmitBackpressureThresholdWords)
				++score.stoppedByBackpressure;
			else
				++score.unexpectedShort;);
	}

	void Hardware::schedCatchUpDspToDsp(const uint32_t _consumer, const uint32_t _producer,
		const std::optional<double> _producerPos)
	{
		// Before a producer DSP enqueues a link frame into the ESSI route,
		// consumer DSP's input ring, advance the CONSUMER to the producer's current machine time - so a
		// frame is never consumed "before" (in DSP-time) it was produced, nor an arbitrary quantum
		// late. The reentrancy guard stops the consumer's own back-channel pushes from recursing into a
		// second catch-up (they just enqueue non-blocking; that DSP is caught up at its next link frame
		// or by the scheduler). Bounded by the catch-up clamp; only ever runs a DSP forward.
		const uint32_t c = _consumer & 1;
		const uint32_t p = _producer & 1;
#if MD_TRANSPORT_DIAGNOSTICS
		auto& score = m_transportScorecard.dspToDsp[c];
		++score.calls;
#endif
		if(m_schedInLinkDelivery)
		{
			MD_TRANSPORT_RECORD(++score.reentrant;);
			return;
		}

		if(!m_schedDspOriginLatched[c] || !m_schedDspOriginLatched[p])
		{
			MD_TRANSPORT_RECORD(++score.originUnavailable;);
			return;
		}
		auto& d = (c == 0) ? m_dspMixer : m_dspProducer;
		const double producerPos = _producerPos ? *_producerPos : schedDspFramePos(p);
		const double deltaFrames = producerPos - m_schedDspOriginFrame[c];
		if(deltaFrames <= 0.0)
		{
			MD_TRANSPORT_RECORD(++score.timeUnavailable;);
			return;
		}
		const uint64_t targetCyc = m_schedDspOriginCycles[c]
			+ static_cast<uint64_t>(deltaFrames * static_cast<double>(g_dsp1CyclesPerEsaiFrame));
		if(d.dsp().getCycles() >= targetCyc)
		{
			MD_TRANSPORT_RECORD(++score.alreadyAtTarget;);
			// Most link writes arrive after the consumer's ordinary scheduler slice already
			// reached this producer timestamp. The old zero-iteration path merely entered and
			// left the reentrancy guard; returning here is equivalent and avoids that hot cost.
			return;
		}
		const auto policy = transportPolicy(m_model);
		const uint64_t startCyc = d.dsp().getCycles();
		const uint64_t clampStop = startCyc + policy.catchUpMaxDspCycles;
		MD_TRANSPORT_RECORD(const auto requested = targetCyc - startCyc;
			score.requestedCycles += requested;
			score.maximumRequestedCycles = std::max(score.maximumRequestedCycles, requested););
		m_schedInLinkDelivery = true;
#ifdef DSP56K_TSC_PROBES
		const auto probeToken = dsp56k::probe::enter(0, dsp56k::probe::CatchUp, c);
#endif
		const bool bpGate = isMonomachine();
		// A consumer on the pair worker gets its UC items through the dated
		// stream: land them on the way at their deadlines, as its own chunks do.
		const bool dated = !dspInlineRunAllowed(c);
		uint64_t nextHostItem = std::numeric_limits<uint64_t>::max();
		if(dated)
		{
			d.serviceHostTransport();
			nextHostItem = d.nextHostTransportCycle();
		}
		if(!bpGate)
		{
			const uint64_t stop = std::min(targetCyc, clampStop);
			while(d.dsp().getCycles() < stop)
			{
				const uint64_t now = d.dsp().getCycles();
				d.dsp().execUntilCycles(nextHostItem > now ? std::min(stop, nextHostItem) : stop);	// same stop rule, one JIT entry
				if(!dated)
					break;
				d.serviceHostTransport();
				nextHostItem = d.nextHostTransportCycle();
			}
		}
		else
		{
			// Between two blocks this loop acts only at targetCyc, clampStop and
			// nextHostItem, and on the host backlog. The consumer grows the
			// backlog only by writing HOTX; in pair mode the UC thread moves it
			// too, but at no particular DSP cycle, so checking it at every block
			// never pinned that down either. So run up to the nearest of the
			// three under one trampoline entry, which a HOTX write leaves once
			// its block completes (Dsp::setExecExitOnHostTx): the backlog is
			// checked where a block-by-block loop checked it. NOP and polling
			// loops skip up to that target, as in the consumer's own chunks;
			// stepped with exec(), every polling iteration of a catch-up ran as
			// a block of its own.
			d.setExecExitOnHostTx(true);
			while(d.dsp().getCycles() < targetCyc && d.dsp().getCycles() < clampStop
				&& d.hostTxBacklog() <= policy.hostTransmitBackpressureThresholdWords)
			{
				if(d.dsp().getCycles() >= nextHostItem)
				{
					d.serviceHostTransport();
					nextHostItem = d.nextHostTransportCycle();
					// An item that cannot land yet is retried at the next
					// chunk or HRX read edge, not at every instruction.
					if(nextHostItem <= d.dsp().getCycles())
						nextHostItem = std::numeric_limits<uint64_t>::max();
				}
				d.dsp().execUntilCycles(std::min(std::min(targetCyc, clampStop), nextHostItem));
			}
			d.setExecExitOnHostTx(false);
		}
#ifdef DSP56K_TSC_PROBES
		dsp56k::probe::leave(probeToken);
#endif
		m_schedInLinkDelivery = false;
		MD_TRANSPORT_RECORD(const auto executed = d.dsp().getCycles() - startCyc;
			score.executedCycles += executed;
			score.maximumExecutedCycles = std::max(score.maximumExecutedCycles, executed);
			if(d.dsp().getCycles() >= targetCyc)
				++score.reachedTarget;
			else if(d.dsp().getCycles() >= clampStop)
				++score.hitClamp;
			else if(bpGate && d.hostTxBacklog()
				> policy.hostTransmitBackpressureThresholdWords)
				++score.stoppedByBackpressure;
			else
				++score.unexpectedShort;);
	}

	void Hardware::advance(const uint32_t _machineFrames)
	{
		// The RAM-packing update touches producer memory: only with the
		// worker parked at the previous target (rare, pending-gated).
		if(m_ramRecordingModePending)
			waitProducerParked();
		serviceRamRecordingMode();
		m_schedFramesTotal += static_cast<double>(_machineFrames);
		m_schedTargetFrames.store(static_cast<uint64_t>(m_schedFramesTotal),
			std::memory_order_release);
		m_signal.notify();

		while(schedStep())
		{
		}

		// The mixer on the pair worker delivers this window's codec frames
		// through the ESSI1 ring; take them once it has reached the target.
		if(m_dspPairWorker.load(std::memory_order_acquire))
			waitMixerAtTarget();
		schedDrainCodecOutput();					// final drain (also covers a UC-only advance window)
		advanceFactoryFlashCapture();
		// Never make the emulation/audio thread wait for a UI snapshot read. If the
		// reader owns the short copy lock, the next machine interval republishes.
		m_frontPanelPublisher->tryPublish(m_frontPanel);
	}

	void Hardware::requestRamRecordingMode(const RamRecordingMode _mode)
	{
		m_ramRecordingMode = _mode;
		m_ramRecordingModePending = supportsRamRecordingMode();
	}

	void Hardware::serviceRamRecordingMode()
	{
		if(!m_ramRecordingModePending || !isFirmwareMidiReady())
			return;
		switch(setRamPackingMode(m_dspProducer.dsp(), m_ramRecordingMode))
		{
		case RamPackingUpdate::Busy:
			return;
		case RamPackingUpdate::Applied:
		case RamPackingUpdate::AlreadyApplied:
			m_ramRecordingModePending = false;
			return;
		case RamPackingUpdate::UnexpectedCode:
			m_ramRecordingModePending = false;
			std::fprintf(stderr,
				"[MD] RAM recording compatibility mode unavailable: unexpected loaded program\n");
			return;
		}
	}

	namespace
	{
		uint64_t delayedMidiDeadline(const uint64_t _sample, const uint32_t _latency)
		{
			constexpr auto maximum = std::numeric_limits<uint64_t>::max();
			return midiReceiveDeadline<g_ucClockHz, g_samplerate>(
				_latency > maximum - _sample ? maximum : _sample + _latency);
		}
	}

	void Hardware::retimeMidi(const uint32_t _extraLatency)
	{
		// Plugin serializes this control operation with rendering. Recompute all
		// pending deadlines from their undelayed positions so a reduction cannot
		// let a new Note Off/Stop overtake an older Note On/Start. Past deadlines
		// stay ordered and drain at the next instruction boundary.
		m_scheduledMidi.retime([&](uint64_t sample) {
			return delayedMidiDeadline(sample, _extraLatency);
		});
	}

	bool Hardware::scheduleMidi(const synthLib::SMidiEvent& _ev, const uint32_t _extraLatency)
	{
		constexpr auto maximum = std::numeric_limits<uint64_t>::max();
		const auto frame = static_cast<uint64_t>(m_schedFramesTotal);
		const auto offset = static_cast<uint64_t>(_ev.offset);
		const auto sample = offset > maximum - frame ? maximum : frame + offset;
		if(!m_scheduledMidi.push(_ev, delayedMidiDeadline(sample, _extraLatency), sample))
		{
			++m_scheduledMidiOverflow;
			return false;
		}
		if(_ev.source != synthLib::MidiEventSource::Internal
			&& (_ev.sysex.empty() || !automation::sysex::isReadOnlyRequest(m_model, _ev.sysex)))
			registerExternalInteraction();
		return true;
	}

	void Hardware::pumpScheduledMidi()
	{
		while(m_scheduledMidi.ready(m_schedUcCyclesDone))
		{
			const auto& event = m_scheduledMidi.front().event;
			const auto type = static_cast<uint8_t>(event.a & 0xf0);
			const bool pad = !isMonomachine() && event.sysex.empty()
				&& (type == synthLib::M_NOTEON || type == synthLib::M_NOTEOFF)
				&& event.b >= 36 && event.b <= 51;
			if(pad)
			{
				if(type == synthLib::M_NOTEON && event.c != 0)
				{
					// Admit a complete press/release pulse, retaining it if UART2 is
					// full. Host notes must not be coalesced into a UI row snapshot.
					if(m_uc.availablePanelRxBytes() < 4)
						return;
					const auto padIndex = static_cast<uint8_t>(event.b - 36);
					const auto row = static_cast<uint8_t>(0x20 + (padIndex >> 3));
					const auto mask = static_cast<uint8_t>(1u << (padIndex & 7));
					m_uc.queuePanelRx(row);
					m_uc.queuePanelRx(mask);
					m_uc.queuePanelRx(row);
					m_uc.queuePanelRx(0);
					synthLib::RealtimeInstrumentation::recordCurrentPanelDelivery(
						static_cast<uint32_t>(m_model), row, mask);
					synthLib::RealtimeInstrumentation::recordCurrentPanelDelivery(
						static_cast<uint32_t>(m_model), row, 0);
				}
			}
			else
			{
				// Both this producer and Device/control callers hold the owning
				// Plugin lock. Do not enter the blocking ring operation when full.
				if(m_midiIn.full())
					return;
				m_midiIn.push_back(event);
			}
			m_scheduledMidi.pop();
		}
	}

	bool Hardware::sendMidi(const synthLib::SMidiEvent& _ev)
	{
		// Internal clock traffic and the controller's exact read-only state queries
		// do not affect the factory baseline. All other routable traffic does.
		if(_ev.source != synthLib::MidiEventSource::Internal
			&& (_ev.sysex.empty() || !automation::sysex::isReadOnlyRequest(
				m_model, _ev.sysex)))
			registerExternalInteraction();
		m_midiIn.push_back(_ev);
		return true;
	}

	void Hardware::pumpMidiIngress()
	{
		const auto pumpRealtime = [this](const bool _hasBoundary,
			const size_t _writeBoundary)
		{
			uint8_t byte = 0;
			while(m_realtimeMidiIn.tryPeek(byte))
			{
				if(_hasBoundary
					&& m_realtimeMidiIn.sizeBefore(_writeBoundary) == 0)
					return true;
				if(!m_uc.tryQueueMidiRx(byte))
					return false;
				uint8_t committed = 0;
				if(!m_realtimeMidiIn.tryPop(committed))
					return false;
			}
			return true;
		};

		const auto pumpGeneralFront = [this]()
		{
			if(m_midiIn.empty())
				return false;
			if(m_midiInByteCursor == 0 && m_midiSysexTransfer.ownsMidiWire())
				return false;
			const auto& event = m_midiIn.front();
			const auto type = static_cast<uint8_t>(event.a & 0xf0);
			const size_t byteCount = !event.sysex.empty() ? event.sysex.size()
				: event.a == synthLib::M_SONGPOSITION ? 3u
				: (type == synthLib::M_PROGRAMCHANGE || type == synthLib::M_AFTERTOUCH
					|| event.a == synthLib::M_QUARTERFRAME || event.a == synthLib::M_SONGSELECT)
					? 2u : type < 0xf0 ? 3u : 1u;
			while(m_midiInByteCursor < byteCount)
			{
				const auto cursor = m_midiInByteCursor;
				const uint8_t byte = !event.sysex.empty() ? event.sysex[cursor]
					: cursor == 0 ? event.a : cursor == 1 ? event.b : event.c;
				if(!m_uc.tryQueueMidiRx(byte))
					return false;
				++m_midiInByteCursor;
			}
			(void)m_midiIn.pop_front();
			m_midiInByteCursor = 0;
			return true;
		};

		// A file transfer owns the wire, but never splits an event already admitted.
		// Realtime bytes queued before start also cross before its payload.
		if(m_midiSysexTransfer.ownsMidiWire())
		{
			if(m_midiInByteCursor != 0 && !pumpGeneralFront())
				return;
			(void)pumpRealtime(true,
				m_midiSysexTransfer.realtimeWriteBoundary());
			return;
		}

		// Once a normal MIDI event has begun, finish it before switching back to the
		// semantic byte queue. At event boundaries semantic commands retain their old
		// priority over general host input.
		if(m_midiInByteCursor == 0 && !pumpRealtime(false, 0))
			return;
		while(!m_midiIn.empty())
		{
			if(!pumpGeneralFront())
				return;
			if(!pumpRealtime(false, 0))
				return;
		}
	}

	bool Hardware::startMidiSysexTransfer(PreparedMidiSysexTransfer& _transfer)
	{
		if(isProjectStateRestorePending() || _transfer.model() != m_model)
			return false;
		if(!m_midiSysexTransfer.start(
			_transfer, m_realtimeMidiIn.writePosition()))
			return false;
		registerExternalInteraction();
		return true;
	}

}
