// md-drums: harness on VoiceEngine's pattern (a host-port handshake per block), for the mixer DSP's master section.
#include "MasterEngine.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::engine
{
	using namespace dsp56k;

	namespace
	{
		// External SRAM from $020000 is shared by P, X and Y, as for the voice DSP. The master reads up to $14ffff.
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x200000, kBridge = 0x020000;

		// The board's memory map and OMR, as VoiceEngine sets them for the voice DSP
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00498d;

		// Mixer program (OS 1.63, section 2)
		constexpr TWord kEntry = 0x24;			// after the vector table: SR, then jsr $100000 (boot), then the block loop
		constexpr TWord kAfterBoot = 0x2e;		// back from the boot, before the codec and DMA set-up of the block loop
		constexpr TWord kBootTrackState = 0x100031;	// the boot's track state (Y:$200-$5ff), not needed
		constexpr TWord kBootMasterClear = 0x100058;	// clears Y:$150-$1bd, then the Echo, Dynamix and Gate Box inits
		constexpr TWord kBootPeripherals = 0x100065;	// host port, ESSI, DMA and interrupts: not needed
		constexpr TWord kMasterStart = 0x342;	// r7 = $150: the Echo
		constexpr TWord kMasterEnd = 0x971;		// Main is in Y:$000-$03f; the program then adds it into the codec frame
		constexpr TWord kStub = 0x0c00;			// free internal P (the program's ends at $a07)

		// The three buses (interleaved L R) and Main
		constexpr TWord kDryBus = 0x180, kRevBus = 0x1c0, kDelBus = 0x600, kMain = 0x000;

		constexpr uint64_t kMaxInstrInit = 20'000'000;	// the delay pool's clear alone is ~230k iterations
		constexpr uint64_t kMaxInstrBlock = 200'000;	// a block costs ~9,740
	}

	MasterEngine::MasterEngine(const fw::Firmware& _fw)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		{
			// As the voice DSP and gearmulator-md-mm's mixer DSP: the entry code runs in the vector area's neighbourhood
			auto cfg = m_dsp->getJit().getConfig();
			cfg.dynamicFastInterrupts = true;
			cfg.aguSupportBitreverse = true;
			cfg.linkJitBlocks = false;
			m_dsp->getJit().setConfig(cfg);
		}
		m_periphX->getHI08().setRXRateLimit(0);
		m_periphX->getHI08().setTransmitDataAlwaysEmpty(true);

		m_dsp->resetHW();
		for(const auto& r : _fw.dspB.records)
		{
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		}
		installHarness();
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;
		m_dsp->setPC(kEntry);

		// The boot's parts the master needs, then the stub's first "done", where the DSP waits for its first block
		if(!runUntilDone(kMaxInstrInit))
			throw std::runtime_error("mixer DSP init did not complete: " + m_fault);
	}

	MasterEngine::~MasterEngine() = default;

	void MasterEngine::installHarness()
	{
		Assembler as;
		auto emit = [&](TWord& _pc, const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("master harness: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(_pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(_pc++, r.word[1]);
		};
		auto hex = [](TWord _v) { std::ostringstream o; o << "$" << std::hex << _v; return o.str(); };
		auto jump = [&](TWord _at, TWord _to)
		{
			m_dsp->memWriteP(_at, 0x0af080);	// jmp >_to
			m_dsp->memWriteP(_at + 1, _to);
		};

		// The boot: skip the track state and, after the master's own init, the peripherals (rts back to the entry code),
		// whose block loop is replaced by the stub
		jump(kBootTrackState, kBootMasterClear);
		m_dsp->memWriteP(kBootPeripherals, 0x00000c);	// rts
		jump(kAfterBoot, kStub);

		// The end of the master: Main is in Y:$000-$03f
		jump(kMasterEnd, kStub);

		// Tell the host the block is done, wait for its "go" word (it writes the buses and parameters meanwhile), run
		// the next block
		TWord pc = kStub;
		emit(pc, "movep #>1,x:<<$ffffc7");
		const TWord wait = pc;
		emit(pc, "jclr #0,x:<<$ffffc3," + hex(wait));
		emit(pc, "movep x:<<$ffffc6,a");
		emit(pc, "jmp " + hex(kMasterStart));
	}

	bool MasterEngine::runUntilDone(const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		const auto start = m_dsp->getInstructionCounter();
		while(hi.txData().empty())
		{
			m_dsp->exec();
			if(m_dsp->getInstructionCounter() - start > _maxInstructions)
			{
				std::ostringstream o;
				o << "instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord();
				m_fault = o.str();
				return false;
			}
		}
		while(hi.hasTX())
			hi.readTX();
		m_lastInstructions = m_dsp->getInstructionCounter() - start;
		return true;
	}

	void MasterEngine::setWords(const uint32_t _y, const uint32_t* _words, const int _count)
	{
		for(int k = 0; k < _count; ++k)
			m_dsp->memWrite(MemArea_Y, _y + static_cast<TWord>(k), _words[k] & 0xffffff);
	}

	void MasterEngine::setX(const uint32_t _x, const uint32_t* _words, const int _count)
	{
		for(int k = 0; k < _count; ++k)
			m_dsp->memWrite(MemArea_X, _x + static_cast<TWord>(k), _words[k] & 0xffffff);
	}

	bool MasterEngine::process(const Mixer::Stereo& _main, const Mixer::Stereo& _rev, const Mixer::Stereo& _del,
		Mixer::Stereo& _out)
	{
		const auto bus = [&](const TWord _x, const Mixer::Stereo& _in)
		{
			for(int i = 0; i < kBlock; ++i)
				for(int c = 0; c < 2; ++c)
					m_dsp->memWrite(MemArea_X, _x + static_cast<TWord>(2 * i + c), static_cast<TWord>(_in[i][c]) & 0xffffff);
		};
		bus(kDryBus, _main);
		bus(kRevBus, _rev);
		bus(kDelBus, _del);

		const TWord go = 1;
		m_periphX->getHI08().writeRX(&go, 1);
		if(!runUntilDone(kMaxInstrBlock))
			return false;

		for(int i = 0; i < kBlock; ++i)
			for(int c = 0; c < 2; ++c)
			{
				const auto w = m_mem->get(MemArea_Y, kMain + static_cast<TWord>(2 * i + c)) & 0xffffff;
				_out[i][c] = static_cast<int32_t>(w << 8) >> 8;
			}
		return true;
	}

	bool MasterEngine::warmUp(const int _blocks)
	{
		// P as large as the external memory X and Y share with it; X and Y up to where they join it
		const auto sizeP = Memory::calcPMemSize(kSizeP, kSizeXY, kBridge);
		const auto sizeXY = Memory::calcXYMemSize(kSizeXY, kBridge);
		const auto* p = m_mem->getMemAreaPtr(MemArea_P);
		const auto* x = m_mem->getMemAreaPtr(MemArea_X);
		const auto* y = m_mem->getMemAreaPtr(MemArea_Y);
		const std::vector<TWord> savedP(p, p + sizeP), savedX(x, x + sizeXY), savedY(y, y + sizeXY);
		const auto savedRegs = m_dsp->regs();

		uint32_t seed = 0x2545f491;
		Mixer::Stereo main{}, rev{}, del{}, out{};
		bool ok = true;
		for(int b = 0; b < _blocks && ok; ++b)
		{
			const bool loud = b < _blocks / 2;
			for(auto* bus : {&main, &rev, &del})
			{
				for(auto& frame : *bus)
				{
					for(auto& sample : frame)
					{
						seed ^= seed << 13;
						seed ^= seed >> 17;
						seed ^= seed << 5;
						sample = loud ? static_cast<int32_t>(seed << 8) >> 8 : 0;
					}
				}
			}
			ok = process(main, rev, del, out);
		}

		std::copy(savedP.begin(), savedP.end(), m_mem->getMemAreaPtr(MemArea_P));
		std::copy(savedX.begin(), savedX.end(), m_mem->getMemAreaPtr(MemArea_X));
		std::copy(savedY.begin(), savedY.end(), m_mem->getMemAreaPtr(MemArea_Y));
		m_dsp->regs() = savedRegs;
		if(ok)
			m_fault.clear();
		return ok;
	}

	uint32_t MasterEngine::readX(const uint32_t _addr) const
	{
		return m_mem->get(MemArea_X, _addr) & 0xffffff;
	}

	uint32_t MasterEngine::readY(const uint32_t _addr) const
	{
		return m_mem->get(MemArea_Y, _addr) & 0xffffff;
	}
}
