#include "mddsp.h"

#include "mdenv.h"
#include "mdhardware.h"
#include "mdtransportpolicy.h"

#include "mc68k/hdi08.h"
#include "synthLib/realtimeInstrumentation.h"

#include <chrono>
#include <cstdio>
#include <limits>

namespace md
{
	using dsp56k::TWord;

	namespace
	{
		// DSP56303 memory sizing. XY is bridged into
		// P above g_bridgedAddr, so external SRAM (>= 0x020000) is shared between P and
		// XY. The Machinedrum second-stage loader uploads its main program into external
		// SRAM and jumps there, so sizeP() must span the external range.
		constexpr TWord g_pMemSize    = 0x800000;
		constexpr TWord g_xyMemSize   = 0x800000;
		constexpr TWord g_bridgedAddr = 0x020000;

		// Fill unused low P with a block-terminating op (RTS) so a stray jump there
		// compiles cleanly under the JIT.
		constexpr TWord g_trapFillEnd = 0x020000;
		constexpr TWord g_fillInstr   = 0x00000C;	// RTS

	}

	Dsp::Dsp(Hardware& _hw, mc68k::Hdi08& _hdiUc, const uint32_t _index)
		: m_hardware(_hw)
		, m_hdiUC(_hdiUc)
		, m_index(_index)
		, m_buffer(dsp56k::Memory::calcMemSize(g_pMemSize, g_xyMemSize, g_bridgedAddr), 0)
		, m_memory(m_validator, g_pMemSize, g_xyMemSize, g_bridgedAddr, m_buffer.data())
		, m_dsp(m_memory, &m_periphX, &m_periphNop)
		, m_boot(m_dsp)
	{
#ifdef DSP56K_TSC_PROBES
		m_dsp.setProbeId(_index & 1);
#endif
		if(!_hw.isValid())
			return;

		// Clock the serial ports from DSP cycles. At 101.6064 MHz, the 1152-cycle
		// codec slot and two slots per frame produce exactly 44.1 kHz; the firmware's
		// ESSI0 divider derives the 96-cycle inter-DSP link slot.
		m_periphX.getEssiClock().setExternalClockFrequency(10'240'000);
		m_periphX.getEssiClock().setSamplerate(44100);
		m_periphX.getEssiClock().setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
		m_periphX.getEssiClock().setExactCycleDeadlineEnabled(
			transportPolicy(m_hardware.getModel()).exactEssiCycleDeadlines);

		// Fine-link mode must be active before the firmware writes CRA so ESSI0 can
		// run below the codec clock base. Synchronous receivers skip RX when their
		// wire is empty instead of fabricating a DMA word from the retained RX value.
		m_periphX.getEssiClock().setCyclesPerSample(1152u);
		m_periphX.getEssi0().setFineLinkMode(true);
		if(m_index == 0 || !m_hardware.isMonomachine())
		{
			m_periphX.getEssi0().setRxDataAvailableCallback([this]
			{
				return m_hardware.linkRxAvailable(m_index);
			});

			// An RX0 read with DMA4 disabled flushes staged link data. DMA reads
			// occur with the channel enabled, which distinguishes the two cases.
			// This protocol is specific to Machinedrum DSP1.
			if(m_index == 0 && !m_hardware.isMonomachine())
			{
				m_periphX.getEssi0().setRxConsumeCallback([this]
				{
					if(m_periphX.getDMA().getDCR(4) & (1u << dsp56k::DmaChannel::De))
						return;
					auto& ring = m_hardware.linkRing(m_index);
#if MD_TRANSPORT_DIAGNOSTICS
					const auto purgedFrames = ring.size();
#endif
					while(!ring.empty())
						ring.pop_front();
#if MD_TRANSPORT_DIAGNOSTICS
					m_hardware.recordMdLinkPurge(purgedFrames);
#endif
					m_hardware.mdLinkWindowFlushed();
				});
			}
		}

		// Serialize host commands through the HI08 busy state so overlapping
		// commands cannot overwrite one another.
		hdi08().setHostCommandArbitration(true);
		// HC and received words belong to the same machine-time domain. A DSP
		// advanced inline may have accepted a command in the CPU's future.
		// Expose that acknowledgement only at its actual acceptance timestamp,
		// not immediately on the CVR write or as late as handler return.
		if(m_hardware.isMonomachine())
			m_hdiUC.setReadCvrCallback([this](uint8_t value)
			{
				// Exact even for the pair worker: a published, bounded-stale
				// acceptance fails the Monomachine's GND SIN oracle.
				m_hardware.waitForDspTime(m_index);
				const bool pending = hdi08().hostCommandPending();
				const bool future = m_hardware.hostRxReadyCycle(m_index, hdi08().hostCommandAcceptedCycle())
					> m_hardware.hostCurrentCycle();
				return static_cast<uint8_t>((value & ~mc68k::Hdi08::Hc) | ((pending || future) ? mc68k::Hdi08::Hc : 0));
			});

		auto config = m_dsp.getJit().getConfig();
		config.aguSupportBitreverse = true;
		// Keep eager child-block linking disabled because bootstrap targets may lie
		// outside the active program range.
		config.linkJitBlocks = false;
		config.dynamicPeripheralAddressing = false;
		// Loader execution can enter vector-area code as ordinary control flow, so
		// select the processing mode dynamically, as the other emulator hosts do.
		config.dynamicFastInterrupts = true;
		// Cap JIT block size so tight program loops return to the dispatcher often enough
		// for the peripherals (the ESSI cycle clock in particular) to be serviced; the
		// EssiClock ticks at most once per peripherals exec.
		config.maxInstructionsPerBlock = 32;
		// Likewise return from hardware DO loops regularly to service peripherals.
		// For the Machinedrum, 64 keeps the ESSI cycle clock serviced well within a
		// 2304-cycle codec frame while cutting dispatcher exits inside long firmware
		// fill loops; measured ~10% lower idle host CPU vs. 4, with the audio
		// firmware soaks and the timing suites unchanged. The Monomachine keeps 4:
		// its transport policy uses exact ESSI cycle deadlines and a 30us background
		// quantum, and 64 makes mmSineFirmwareTest/mmSineMidiFirmwareTest fail.
		// MD_MAX_DO_ITERATIONS overrides for experiments (power of two required by
		// the JIT).
		config.maxDoIterations = m_hardware.isMonomachine() ? 4 : 64;
		if(const auto doIterations = envCount<uint32_t>("MD_MAX_DO_ITERATIONS"))
		{
			const auto v = *doIterations;
			if(v && (v & (v - 1)) == 0)
				config.maxDoIterations = v;
			else
				std::fprintf(stderr, "[MD] MD_MAX_DO_ITERATIONS=%u ignored: not a power of two\n", v);
		}
		// JIT blocks are first compiled synchronously by the audio thread; a
		// pattern/kit switch can queue hundreds of cold compilations into one
		// callback (measured: 183 compilations, 29ms in a 2.9ms budget). The
		// optimizer's cold cost exceeds its measured steady-state gain on Apple
		// silicon (upstream) and on Windows x64 (Ryzen 3700X: <2%, within noise),
		// so keep it off everywhere to shorten those storms.
		// MD_JIT_OPTIMIZER=1 forces it back on, =0 forces it off.
		config.enableOptimizer = false;
		if(const char* const optimizer = envOverride("MD_JIT_OPTIMIZER"))
			config.enableOptimizer = optimizer[0] != '0';
		config.getBlockConfig = [](const TWord)
			-> std::optional<dsp56k::JitConfig>
		{
			synthLib::RealtimeInstrumentation::recordCurrentCallbackJitCompilation();
			return {};
		};
		m_dsp.getJit().setConfig(config);
		m_dsp.getJit().preallocateBlockRuntimeData(
			RealtimeJitBlockRuntimeDataReserve);

		const TWord fillEnd = std::min<TWord>(g_trapFillEnd, m_memory.sizeP());
		for(TWord i = 0; i < fillEnd; ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, g_fillInstr);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		// Keep the DSP from blocking on empty serial input during boot. The
		// ESSI0 link prefill lives in the Hardware-owned TimedLinkRing now.
		m_periphX.getEssi1().writeEmptyAudioIn(64);

		hdi08().setRXRateLimit(0);
		hdi08().setTransmitDataAlwaysEmpty(false);

		// HI08 has a DSP transmit register and a host receive latch. Transfer a
		// word into the free latch, then let HTDE pace the DSP. A DSP running
		// ahead must not publish RXDF/HREQ before the host reaches its timestamp.
		if(m_hardware.isMonomachine())
			hdi08().setWriteTxCallback([this]
			{
				m_mmHostTxCycle = m_dsp.getCycles();
				if(m_execExitOnHostTx)
					m_dsp.requestExecExit();
				// A DSP on a worker never touches the UC's register file: it
				// stages the word, the UC context takes it (stageHostTx).
				if(m_hardware.dspInlineRunAllowed(m_index))
					hdiTransferDSPtoUC();
				else
					stageHostTx();
			});
		else
			// MD: the DSP context is the only reader of its HOTX latch. Each
			// write is dated at its cycle and staged for the UC (spec §5.2).
			hdi08().setWriteTxCallback([this]
			{
				m_lastHostTxCycle = m_dsp.getCycles();
				stageHostTx();
			});

		// Threaded adapter: the HRX read that empties the register is where the
		// serial bridge's drain run ended and its next write landed. Land the
		// next due data word right there, in the DSP context, so a host burst
		// drains at the firmware's own read rate rather than one word per
		// worker chunk.
		hdi08().setReadRxCallback([this]
		{
			landHostToDspWordOnRead();
		});

		// ---- Bridge the ColdFire-facing HI08 register file to the DSP (n2x model) ----

		m_hdiUC.setRxEmptyCallback([this](const bool _needMoreData)
		{
			onUCRxEmpty(_needMoreData);
		});

		// TX: during the boot upload each assembled 24-bit word drives DspBoot. Once boot
		// has finished the callback is switched to feed the running DSP's HORX.
		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			if(m_boot.hdiWriteTX(_word))
				onDspBootFinished();
		});

		m_hdiUC.setWriteIrqCallback([this](const uint8_t _irq)
		{
			hdiSendIrqToDSP(_irq);
		});

		m_hdiUC.setReadIsrCallback([this](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});

		// Derive TXDE/TRDY from the receive depth for this interface. Other products
		// retain the default always-ready behavior.
		m_hdiUC.setForceTxde(false);

		m_hdiUC.setInitHdi08Callback([this]
		{
			// Complete host-port initialization and report the transmitter ready.
			m_hdiUC.icr(m_hdiUC.icr() & 0x7f);
			m_hdiUC.isr(m_hdiUC.isr() | mc68k::Hdi08::IsrBits::Txde | mc68k::Hdi08::IsrBits::Trdy);
		});

	}

	void Dsp::onDspBootFinished()
	{
		// After boot, further host words are input to the DSP program.
		m_hdiUC.setWriteTxCallback([this](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});

		// There are no background DSP threads. Publish the completed boot state to
		// the deterministic scheduler that owns all subsequent execution.
		m_schedRunnable.store(true, std::memory_order_release);
	}

	namespace
	{
		uint64_t schedInlineClamp(const MachineModel _model)
		{
			return transportPolicy(_model).catchUpMaxDspCycles;
		}
	}

	size_t Dsp::hostTxBacklog()
	{
		// The UC-facing receive depth comes from the mirror the UC context
		// publishes, so a DSP worker can evaluate its backlog without reading
		// the ColdFire's register file. A threaded MM stages in the dated
		// queue instead of the latch (stageHostTx).
		// A threaded MM's taken copy sits both in HOTX and in the UC latch
		// until the DSP acknowledges the take: count it once.
		const size_t backlog = hdi08().txData().size() + m_hardware.ucRxDepth(m_index)
			+ (m_timedHostRx.pending() ? 1 : 0);
		// Only a staged copy can have been taken (serial MM never stages one).
		const size_t takes = m_mmTxStaged ? m_hostTxTakes.size() : 0;
		return backlog > takes ? backlog - takes : 0;
	}

	void Dsp::applyHostTxTakes()
	{
		while(!m_hostTxTakes.empty()
			&& m_hardware.hostToDspDeadline(m_index, m_hostTxTakes.front()) <= m_dsp.getCycles())
		{
			m_lastHostTxTakeUc = m_hostTxTakes.pop_front();
			// The UC read the latch this word went to: HOTX frees at that time,
			// as when the serial bridge moved the word out.
			if(hdi08().hasTX())
				hdi08().readTX();
			m_mmTxStaged = false;
		}
	}

	uint64_t Dsp::nextHostTxTakeCycle() const
	{
		if(m_hostTxTakes.empty())
			return std::numeric_limits<uint64_t>::max();
		return m_hardware.hostToDspDeadline(m_index, m_hostTxTakes.front());
	}

	uint64_t Dsp::nextHostTransportCycle() const
	{
		return std::min(hostToDspHeadDeadline(), nextHostTxTakeCycle());
	}

	void Dsp::serviceHostTransport()
	{
		applyHostToDspStream();
		stageHostTx();
	}

	void Dsp::publishHostStatus()
	{
		const auto hf23 = hdi08().readControlRegister() & 0x18;	// HF2 (bit3), HF3 (bit4)
		const auto depth = std::min<size_t>(hdi08().rxData().size(), 0xffffff);
		const auto status = static_cast<uint32_t>(hf23 | (depth << 8));
		if(m_hostStatus.load(std::memory_order_relaxed) != status)
			m_hostStatus.store(status, std::memory_order_release);
	}

	void Dsp::enterThreadedHostTransport()
	{
		// Scheduler thread, before a worker owns this DSP: a word still in the
		// MM's serial one-word latch moves to the dated staging queue, which
		// is the only DSP->UC path from now on.
		if(!m_hardware.isMonomachine() || !m_timedHostRx.pending())
			return;
		const uint64_t ready = m_timedHostRx.readyCycle();
		uint32_t word = 0;
		if(m_timedHostRx.take(std::numeric_limits<uint64_t>::max(), word))
			m_hostTxStaging.push_back(StagedHostWord{word, ready});
	}

	void Dsp::publishUcRxDepth()
	{
		m_hardware.publishUcRxDepth(m_index, m_hdiUC.rxDataSize());
	}

	void Dsp::stageHostTx()
	{
		if(m_hardware.isMonomachine())
		{
			// Threaded MM: the one-latch rule of the serial path
			// (hdiTransferDSPtoUC) with the UC's side of it in the UC context.
			// The DSP stages a copy of its HOTX word at once; the UC takes it
			// when its latch is free and its time has reached the word; HOTX
			// frees when the DSP reaches the time of that take. The serial MM
			// stages from the UC side.
			if(m_hardware.dspInlineRunAllowed(m_index))
				return;
			applyHostTxTakes();
			if(m_mmTxStaged || !hdi08().hasTX() || m_hostTxStaging.full())
				return;
			m_hostTxStaging.push_back(StagedHostWord{hdi08().txData().front(),
				m_hardware.hostRxReadyCycle(m_index, m_mmHostTxCycle), true});
			m_mmTxStaged = true;
			m_hardware.notifyHostPumpStateChanged();
			return;
		}
		if(m_hostTxStaging.full() || !hdi08().hasTX())
			return;
		m_hostTxStaging.push_back(StagedHostWord{hdi08().readTX(),
			m_hardware.hostRxReadyCycle(m_index, m_lastHostTxCycle)});
		m_hardware.notifyHostPumpStateChanged();
	}

	bool Dsp::takeDueHostRx(const uint64_t _now, uint32_t& _word, bool* _inHotx)
	{
		if(m_hostTxStaging.empty() || m_hostTxStaging.front().readyCycle > _now)
			return false;
		const auto staged = m_hostTxStaging.pop_front();
		_word = staged.word;
		if(_inHotx)
			*_inHotx = staged.inHotx;
		return true;
	}

	void Dsp::pushHostToDsp(const HostToDspItem::Kind _kind, const uint32_t _value)
	{
		// The UC never drops a host word. A full stream means the worker is
		// behind on applying items; wait for room while the mixer keeps
		// advancing (the worker's mixer gate must be able to open), and only
		// give up on a dead worker.
		// Never drop: an expired wait is logged and retried, a dropped host
		// word corrupts the firmware's control flow.
		if(m_hostToDsp.full())
		{
			m_hardware.setHostWriteBlocked(m_index, true);
			while(m_hostToDsp.full())
			{
				if(!m_hardware.waitTransport(Hardware::TransportWaitSite::HostToDspRoom,
					std::chrono::seconds(5), [&] { return !m_hostToDsp.full(); }))
					std::fprintf(stderr, "[MD] host-to-DSP stream of DSP%u full for 5 s, still waiting\n",
						m_index + 1);
			}
			m_hardware.setHostWriteBlocked(m_index, false);
		}
		m_hostToDsp.push_back(HostToDspItem{_kind, _value, m_hardware.hostCurrentCycle()});
		(_kind == HostToDspItem::Kind::Data ? m_hostToDspTrace.pushedData : m_hostToDspTrace.pushedCommands)
			.fetch_add(1, std::memory_order_relaxed);
		m_hardware.transportSignal().notify();
	}

	void Dsp::landHostToDspWordOnRead()
	{
		if(m_hardware.dspInlineRunAllowed(m_index) || m_hostToDsp.empty())
			return;
		const auto& item = m_hostToDsp.front();
		const uint64_t now = m_dsp.getCycles();
		// Commands keep their chunk-boundary dispatch (host-command-idle has
		// no read edge to hook); a word waits for its deadline and a free HRX.
		if(item.kind != HostToDspItem::Kind::Data || hdi08().hasRXData()
			|| m_hardware.hostToDspDeadline(m_index, item.ucCycle) > now)
			return;
		const TWord word = item.value;
		hdi08().writeRX(&word, 1);
		m_hostToDsp.pop_front();
		m_hostToDspLastLand.store(now, std::memory_order_release);
		m_hostToDspTrace.landedOnRead.fetch_add(1, std::memory_order_relaxed);
		m_hardware.transportSignal().notify();
	}

	uint64_t Dsp::hostToDspHeadDeadline() const
	{
		if(m_hostToDsp.empty())
			return std::numeric_limits<uint64_t>::max();
		return m_hardware.hostToDspDeadline(m_index, m_hostToDsp.front().ucCycle);
	}

	uint64_t Dsp::hostToDspDrainStart(const HostToDspItem& _item) const
	{
		return std::max(m_hardware.hostToDspDeadline(m_index, _item.ucCycle),
			m_hostToDspLastLand.load(std::memory_order_acquire));
	}

	void Dsp::applyHostToDspStream()
	{
		const uint64_t now = m_dsp.getCycles();
		const uint64_t clamp = schedInlineClamp(m_hardware.getModel());
		// Wake a UC waiting for stream room only when an item landed: the
		// worker applies the stream on every turn, most of them empty.
		bool landed = false;
		struct NotifyOnLanding
		{
			Hardware& hardware; const bool& landed;
			~NotifyOnLanding() { if(landed) hardware.transportSignal().notify(); }
		} notifyOnLanding{m_hardware, landed};
		while(!m_hostToDsp.empty())
		{
			const auto& item = m_hostToDsp.front();
			const uint64_t deadline = m_hardware.hostToDspDeadline(m_index, item.ucCycle);
			if(deadline > now)
				return;
			// The serial path paced a word on HRX being drained and a command
			// on host-command-idle, but only up to the inline clamp - after it
			// the word or command went through anyway (the HI08 receive queue
			// is deeper than one word). Keep that exact envelope: a head that
			// cannot land within the clamp of its drain start lands regardless,
			// so a firmware waiting for the NEXT item can never deadlock. The
			// drain starts where the serial write would have: at the deadline,
			// or later if the previous item landed later.
			const bool overdue = now >= hostToDspDrainStart(item) + clamp;
			if(item.kind == HostToDspItem::Kind::Data)
			{
				if(hdi08().hasRXData() && !overdue)
					return;
				// The receive ring push blocks when full (Lock=true): never
				// let the worker park there, the item simply waits.
				if(hdi08().dataRXFull())
					return;
				const TWord word = item.value;
				hdi08().writeRX(&word, 1);
			}
			else
			{
				// MM: the data words issued before a command are consumed first;
				// the serial bridge ran the DSP until HORX drained, up to four
				// clamps (hdiSendIrqToDSP).
				if(m_hardware.isMonomachine() && !hdi08().rxData().empty()
					&& now < hostToDspDrainStart(item) + clamp * 4)
					return;
				if(hdi08().hostCommandBusy() && !overdue)
					return;
				dispatchHostCommandInterrupt(static_cast<uint8_t>(item.value));
			}
			m_hostToDsp.pop_front();
			m_hostToDspLastLand.store(now, std::memory_order_release);
			(overdue ? m_hostToDspTrace.landedOverdue : m_hostToDspTrace.landedInChunk)
				.fetch_add(1, std::memory_order_relaxed);
			landed = true;
		}
	}

	uint64_t Dsp::hostToDspHeadAllowance() const
	{
		if(m_hostToDsp.empty())
			return 0;
		return hostToDspDrainStart(m_hostToDsp.front()) + schedInlineClamp(m_hardware.getModel());
	}

	uint64_t Dsp::hostToDspHeadBlockedAllowance()
	{
		if(m_hostToDsp.empty())
			return 0;
		const auto& item = m_hostToDsp.front();
		if(m_hardware.hostToDspDeadline(m_index, item.ucCycle) > m_dsp.getCycles())
			return 0;
		const uint64_t clamp = schedInlineClamp(m_hardware.getModel());
		const uint64_t start = hostToDspDrainStart(item);
		if(item.kind == HostToDspItem::Kind::Data)
			return hdi08().hasRXData() || hdi08().dataRXFull() ? start + clamp : 0;
		const bool mmDrain = m_hardware.isMonomachine() && !hdi08().rxData().empty();
		if(!mmDrain && !hdi08().hostCommandBusy())
			return 0;
		return start + clamp * (m_hardware.isMonomachine() ? 5 : 1);
	}

	void Dsp::traceHostStream(const char* _tag) const
	{
		const auto& hdi = const_cast<Dsp*>(this)->hdi08();
		const auto now = m_dsp.getCycles();
		if(m_hostToDsp.empty())
		{
			std::fprintf(stderr, "%s stream empty now=%llu hrx=%d hcBusy=%d pc=%06x\n", _tag,
				static_cast<unsigned long long>(now), hdi.hasRXData() ? 1 : 0,
				hdi.hostCommandBusy() ? 1 : 0, m_dsp.getPC().toWord());
			return;
		}
		const auto& head = m_hostToDsp.front();
		std::fprintf(stderr, "%s stream=%zu head=%s value=%06x uc=%llu deadline=%llu now=%llu "
			"hrx=%d hcBusy=%d pc=%06x\n", _tag, m_hostToDsp.size(),
			head.kind == HostToDspItem::Kind::Data ? "data" : "cmd", head.value,
			static_cast<unsigned long long>(head.ucCycle),
			static_cast<unsigned long long>(m_hardware.hostToDspDeadline(m_index, head.ucCycle)),
			static_cast<unsigned long long>(now), hdi.hasRXData() ? 1 : 0,
			hdi.hostCommandBusy() ? 1 : 0, m_dsp.getPC().toWord());
	}

	bool Dsp::stagedHostRxTakeable() const
	{
		return !m_hostTxStaging.front().inHotx || m_hdiUC.canReceiveData();
	}

	uint64_t Dsp::nextDeferredHostRxCycle() const
	{
		if(!m_hostTxStaging.empty())
			return m_hostTxStaging.front().readyCycle;
		return m_timedHostRx.pending() ? m_timedHostRx.readyCycle()
			: std::numeric_limits<uint64_t>::max();
	}

	uint32_t Dsp::pumpHostRx(const size_t _maxUcWords)
	{
		if(m_hardware.isMonomachine())
			return hdiTransferDSPtoUC() ? 1 : 0;

		// Drain DSP HOTX continuously into a bounded host-side queue rather than
		// demand-pulling one word at a time; that queue depth is what
		// raises HI08 HREQ (>= set_host_rx_irq_min_words words). Our UC-facing HI08 backing queue
		// (m_rxData) is that host-side queue: push DSP HOTX words straight into it, bypassing the
		// one-word RXDF latch gate in hdiTransferDSPtoUC (canReceiveData) which otherwise caps
		// availability at a single word and makes HREQ's >= 3 threshold unreachable. Bounded by
		// _maxUcWords so we don't grow it unbounded when the host isn't reading. This also fixes
		// the "HOTX is full, Discarding" overflow: DSP2's HOTX now drains promptly instead of only
		// when the firmware happens to demand a word.
		// The words come from the dated staging queue the DSP context fills;
		// only those whose ready cycle the host clock has reached are visible.
		uint32_t moved = 0;
		uint32_t w = 0;
		while(m_hdiUC.rxDataSize() < _maxUcWords
			&& takeDueHostRx(m_hardware.hostCurrentCycle(), w))
		{
			m_hdiUC.writeRx(w);
			++moved;
		}
		// If the host consumed the latched word on the previous instruction but more remain queued
		// (and no fresh DSP word re-latched them above), latch the next now so it is delivered in
		// FIFO order rather than read back as a spurious 0.
		m_hdiUC.relatchRx();
		publishUcRxDepth();
		return moved;
	}

	void Dsp::setHostPumpWakeCallback(const std::function<void()>& _callback)
	{
		hdi08().setHostPumpWakeCallback(_callback);
		m_hdiUC.setIcrWriteCallback([_callback](const uint8_t)
		{
			_callback();
		});
		// Fired on the UC context whenever the receive queue latches or
		// drains, i.e. the only other site that changes the UC-facing depth.
		m_hdiUC.setRxStateChangedCallback([this, _callback]
		{
			publishUcRxDepth();
			_callback();
		});
	}

	void Dsp::onUCRxEmpty(const bool _needMoreData)
	{
		m_hardware.notifyHostPumpStateChanged();

		if(_needMoreData)
		{
			// A blocking host read needs its peer to make progress on this single
			// scheduler thread, so run the target DSP inline until it produces the
			// reply or the in-flight host command has been fully serviced, bounded.
			// A reserved/readable MM reply already satisfies production: wait for
			// CPU time to make it visible instead of running the producer farther.
			const uint64_t startCycle = m_dsp.getCycles();
			const uint64_t clampStop = startCycle
				+ schedInlineClamp(m_hardware.getModel());
			// A produced reply is either still in the HOTX latch or already
			// staged (dated) for the UC. Serial adapter only: the threaded
			// transport waits on the worker's position instead (spec §5.6).
			if(m_hardware.dspInlineRunAllowed(m_index))
				while(!hdi08().hasTX() && !hasDeferredHostRx()
					&& (!m_hardware.isMonomachine() || m_hdiUC.canReceiveData())
					&& (hdi08().hostCommandBusy() || dsp().hasPendingInterrupts())
					&& m_dsp.getCycles() < clampStop)
					m_dsp.exec();
#if MD_TRANSPORT_DIAGNOSTICS
			const bool workComplete = hdi08().hasTX() || hasDeferredHostRx()
				|| (m_hardware.isMonomachine() && !m_hdiUC.canReceiveData())
				|| (!hdi08().hostCommandBusy() && !dsp().hasPendingInterrupts());
			m_hardware.recordInlineHdi08Run(m_index, startCycle, clampStop,
				workComplete);
#endif
			hdiTransferDSPtoUC();
			return;
		}

		hdiTransferDSPtoUC();
	}

	void Dsp::hdiTransferUCtoDSP(const uint32_t _word)
	{
		// Threaded adapter: the word is dated with the UC's cycle and the
		// worker lands it in HRX once its own time reaches that stamp.
		if(!m_hardware.dspInlineRunAllowed(m_index))
		{
			pushHostToDsp(HostToDspItem::Kind::Data, _word);
			return;
		}
		// Catch the DSP up to the UC's current machine time before the word
		// lands, so it consumes everything up to "now" first.
		m_hardware.waitForDspTime(m_index);

		// Route ordinary data words through the paced host receive path. Host-command
		// arbitration keeps each argument with its in-flight command.
		writeWordToDsp(_word);
	}

	void Dsp::writeWordToDsp(const uint32_t _word)
	{
		// The DSP56303 HI08 host data path has a host latch and a one-word HRX. Before placing
		// a word in HRX, advance the target DSP until the previous word drains, bounded by the
		// scheduler clamp. This preserves receive ordering without a wall-clock
		// wait or an unbounded host-side FIFO.
		const uint64_t startCycle = m_dsp.getCycles();
		const uint64_t clampStop = startCycle
			+ schedInlineClamp(m_hardware.getModel());
		if(m_hardware.dspInlineRunAllowed(m_index))
			while(hdi08().hasRXData() && m_dsp.getCycles() < clampStop)
				m_dsp.exec();
#if MD_TRANSPORT_DIAGNOSTICS
		m_hardware.recordInlineHdi08Run(m_index, startCycle, clampStop,
			!hdi08().hasRXData());
#endif
		hdi08().writeRX(&_word, 1);
		return;
	}

	void Dsp::waitForHostCommandIdle()
	{
		// A CVR write may not overtake a host command already in flight. Hold the current
		// transaction in emulated time until RTI clears host-command-busy.
		if(!hdi08().hostCommandBusy())
			return;

		const uint64_t startCycle = m_dsp.getCycles();
		const uint64_t clampStop = startCycle
			+ schedInlineClamp(m_hardware.getModel());
		if(m_hardware.dspInlineRunAllowed(m_index))
			while(hdi08().hostCommandBusy() && m_dsp.getCycles() < clampStop)
				m_dsp.exec();
#if MD_TRANSPORT_DIAGNOSTICS
		m_hardware.recordInlineHdi08Run(m_index, startCycle, clampStop,
			!hdi08().hostCommandBusy());
#endif
		return;
	}

	void Dsp::dispatchHostCommandInterrupt(const uint8_t _vba)
	{
		// Under the arbitration config, route the host-command vector through the DSP-side HDI08
		// so it raises HCP and arms the handler hold as it injects (native DSP56303 host-command
		// dispatch). Otherwise fall back to the legacy out-of-band inject (default uitest path).
		if(hdi08().hostCommandArbitration())
			hdi08().writeHostCommand(_vba);
		else
			dsp().injectExternalInterrupt(_vba);
	}

	void Dsp::hdiSendIrqToDSP(const uint8_t _irq)
	{
		// Threaded adapter: the command joins the dated stream behind any
		// data word issued before it, and the worker dispatches it once no
		// other host command is in flight.
		if(booted() && !m_hardware.dspInlineRunAllowed(m_index))
		{
			pushHostToDsp(HostToDspItem::Kind::Command, _irq);
			return;
		}
		// Catch the DSP up to the UC's current machine time before the CVR is
		// dispatched, so HCP is raised at a defined point in DSP time.
		if(booted())
			m_hardware.waitForDspTime(m_index);
		// Preserve Monomachine host-command ordering. Data words precede the next
		// command, so drain the receive path before dispatching that command. Run the DSP
		// inline until HORX has drained before dispatching the CVR. This is needed
		// only for the MM transport.
		const bool s_mmInOrderCvr = m_hardware.isMonomachine();
		if(s_mmInOrderCvr && booted())
		{
			const uint64_t startCycle = m_dsp.getCycles();
			const uint64_t clampStop = startCycle
				+ schedInlineClamp(m_hardware.getModel()) * 4;
			if(m_hardware.dspInlineRunAllowed(m_index))
				while(!hdi08().rxData().empty() && m_dsp.getCycles() < clampStop)
					m_dsp.exec();
#if MD_TRANSPORT_DIAGNOSTICS
			m_hardware.recordInlineHdi08Run(m_index, startCycle, clampStop,
				hdi08().rxData().empty());
#endif
		}

		if(!booted())
		{
			// Pre-boot: nothing to interrupt.
			return;
		}

		// Serialize host commands before dispatch. The HI08 command bit remains busy
		// until the current handler returns, keeping the following argument words with
		// the correct command.
		waitForHostCommandIdle();

		dispatchHostCommandInterrupt(_irq);

		hdiTransferDSPtoUC();
	}

	uint8_t Dsp::hdiUcReadIsr(uint8_t _isr)
	{
		// Catch the DSP up to the UC's current machine time before reporting
		// status, so a UC status-poll loop sees the DSP's progress (e.g. a reply it is waiting for)
		// in fine lockstep instead of a frozen snapshot.
		// The pair worker's DSPs are read from their published state instead
		// (spec §5.5 bounded-stale fallback): waiting for them at every poll
		// kept the UC and the worker in lockstep. The UC stays within one
		// quantum of the slower DSP (schedStep), the staleness bound, and
		// words still in the dated stream count as occupying HORX.
		const bool published = m_hardware.isDspPairThreaded();
		if(!published)
			m_hardware.waitForDspTime(m_index);
		hdiTransferDSPtoUC();
		// Publication above may have changed RXDF after Hdi08 sampled _isr.
		// Return the current latch state, including on the first data-byte read.
		_isr = static_cast<uint8_t>((_isr & ~mc68k::Hdi08::Rxdf)
			| (m_hdiUC.canReceiveData() ? 0 : mc68k::Hdi08::Rxdf));

		const uint32_t status = published ? m_hostStatus.load(std::memory_order_acquire) : 0;

		// Mirror the DSP's host flags HF2/HF3 into the UC-visible ISR.
		const auto hf23 = published ? (status & 0x18) : (hdi08().readControlRegister() & 0x18);	// HF2 (bit3), HF3 (bit4)
		_isr &= ~0x18;
		_isr |= static_cast<uint8_t>(hf23);

		// Model the two-stage HI08 transmit path described by DSP56303UM 6.3.6/6.6.8:
		// TXDE reports room in the host latch, while TRDY additionally requires the
		// DSP receive latch to be empty.
		const auto horxDepth = published ? (status >> 8) + m_hostToDsp.size() : hdi08().rxData().size();
		_isr &= static_cast<uint8_t>(~(mc68k::Hdi08::IsrBits::Txde | mc68k::Hdi08::IsrBits::Trdy));
		if(horxDepth == 0)
			_isr |= mc68k::Hdi08::IsrBits::Txde | mc68k::Hdi08::IsrBits::Trdy;
		else if(horxDepth == 1)
			_isr |= mc68k::Hdi08::IsrBits::Txde;
		// HREQ is routed separately; composing it here would require the unmodelled IVR path.

		return _isr;
	}

	bool Dsp::hdiTransferDSPtoUC()
	{
		if(m_hardware.isMonomachine())
		{
			// The deferred word reserves the same receive latch as m_hdiUC:
			// never stage another word while that latch is readable by the CPU.
			if(!m_hdiUC.canReceiveData())
				return false;

			// A DSP on a worker stages its words itself (stageHostTx): the UC
			// context only takes the due one.
			if(!m_hardware.dspInlineRunAllowed(m_index))
			{
				uint32_t staged = 0;
				bool inHotx = false;
				const uint64_t now = m_hardware.hostCurrentCycle();
				if(!takeDueHostRx(now, staged, &inHotx))
					return false;
				m_hdiUC.writeRx(staged);
				publishUcRxDepth();
				if(inHotx)
				{
					m_hostTxTakes.push_back(now);
					m_hardware.transportSignal().notify();
				}
				m_hardware.notifyHostPumpStateChanged();
				return true;
			}

			if(!m_timedHostRx.pending() && hdi08().hasTX())
			{
				const auto word = hdi08().readTX();
				m_timedHostRx.stage(word,
					m_hardware.hostRxReadyCycle(m_index, m_mmHostTxCycle));
				m_hardware.notifyHostPumpStateChanged();
			}

			uint32_t word;
			if(!m_timedHostRx.take(m_hardware.hostCurrentCycle(), word))
				return false;
			m_hdiUC.writeRx(word);
			publishUcRxDepth();
			m_hardware.notifyHostPumpStateChanged();
			return true;
		}

		uint32_t echo = 0;
		if(m_hdiUC.canReceiveData()
			&& takeDueHostRx(m_hardware.hostCurrentCycle(), echo))
		{
			m_hdiUC.writeRx(echo);
			publishUcRxDepth();
			m_hardware.notifyHostPumpStateChanged();
			return true;
		}
		return false;
	}
}
