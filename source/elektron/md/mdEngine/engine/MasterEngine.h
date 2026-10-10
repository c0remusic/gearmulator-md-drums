// md-drums: the master effects of the Machinedrum's mixer DSP (DSP1), run on their own in the dsp56300 emulator: the
// section of OS 1.63's mixer program from P:$342 to P:$970 (Rhythm Echo on the delay send, Gate Box on the reverb send,
// their returns added to the dry main mix, then the EQ and Dynamix), fed with the three buses the Mixer makes. The rest
// of the program (track chains, mix, codec, host port) never runs. See .scratch/md-drums-editor/research/03-master-dsp1.md.
//
// Main comes out MainDelay samples after its input: Dynamix's look-ahead, as on the machine.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "Mixer.h"
#include "../tools/mdfw/Firmware.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }

namespace md::engine
{
	class MasterEngine
	{
	public:
		static constexpr int kBlock = Mixer::kBlock;
		static constexpr int MainDelay = 16;

		// The four parameter blocks the OS sends the DSP (Y addresses and word counts), one effect per tick
		static constexpr uint32_t kEchoWords = 0x150, kDynamixWords = 0x17a, kEqWords = 0x170, kGateBoxWords = 0x185;

		// Loads the mixer program (section 2) and runs the parts of its boot the master needs: the sine table, the
		// clears and the three effects' own initialisation. Throws on failure.
		explicit MasterEngine(const fw::Firmware& _fw);
		~MasterEngine();

		MasterEngine(const MasterEngine&) = delete;
		MasterEngine& operator=(const MasterEngine&) = delete;

		// Parameter words into Y, as the OS's block writes put them there (24 bits each); between two blocks.
		void setWords(uint32_t _y, const uint32_t* _words, int _count);
		// Words into X, between two blocks (external memory, from $020000, is X, Y and P at once): with setWords, what a
		// test needs to start from another mixer DSP's state
		void setX(uint32_t _x, const uint32_t* _words, int _count);

		// One 32-sample block: the dry main mix, the reverb send and the delay send in, Main out. False on a fault.
		bool process(const Mixer::Stereo& _main, const Mixer::Stereo& _rev, const Mixer::Stereo& _del, Mixer::Stereo& _out);

		// The master's code compiled now rather than in the first block. The mixer DSP's JIT compiles its code the first
		// time it runs: about 26 ms in the first 32-sample block (mdDrumsFirstHitTest), nine times what an audio thread at
		// 128 samples has. Runs _blocks blocks, loud noise on the three buses then silence, so that the effects' branches
		// for a signal and for its tail run too; then the DSP gets its memory and registers back, as VoiceEngine's warm-up
		// does: P holds the same code after, so what the JIT compiled stays valid, and the master sounds as if nothing had
		// run. Between two blocks. False on a fault.
		bool warmUp(int _blocks = 64);

		uint32_t readX(uint32_t _addr) const;
		uint32_t readY(uint32_t _addr) const;
		uint64_t instructionsLastBlock() const { return m_lastInstructions; }
		const std::string& faultReason() const { return m_fault; }
		dsp56k::DSP& dsp() { return *m_dsp; }

	private:
		void installHarness();
		bool runUntilDone(uint64_t _maxInstructions);

		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		uint64_t m_lastInstructions = 0;
		std::string m_fault;
	};
}
