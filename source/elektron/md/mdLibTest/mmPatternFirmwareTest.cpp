// A Monomachine pattern as the JOUER page reads and edits it (md::automation::sysex::parseMmPatternDump,
// MmPatternEditor), against the real machine.
//
// The factory patterns A01 to A04 decode: 64 steps, Kits 1 to 4; every step whose trig starts the amp
// envelope has its note, a step without a trig has none, a lock only on a step with a trig, a row per
// set bit of the lock masks. Then A01 is rewritten through MmPatternEditor (every trig cleared, 16
// steps, four trigs on track 1, two locks), sent in GLOBAL > SYSEX RECV, read back as written, and
// played with GND-SIN on track 1. What the machine plays tells what the dump means:
// - a note is a MIDI note number: C4 (60) sounds an octave above C3 (48), counted in zero crossings
//   of the sine;
// - lock mask bit b is the track parameter at page * 8 + index, its place in the Kit (mmLockBit): bit
//   13, AMP VOL, at 0 silences its trig; bit 50 at 0 (an LFO 3 setting) leaves it as loud as the first.
// Played again, the sequencer's step and running state as JOUER reads them in the firmware's RAM count
// the 16 steps and stop with STOP.
//
// Firmware from GEARMULATOR_MM_FIRMWARE_BIN; 77 without it.

#include "mmFirmwareMachine.h"

#include "mdLib/mdromloader.h"

#include "baseLib/filesystem.h"

#include <array>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace md::test;
	namespace sysex = md::automation::sysex;
	using Pattern = sysex::MmPatternDump;
	constexpr auto Model = md::MachineModel::Monomachine;

	// 150 BPM: a MIDI clock tick lasts 735 frames, a step 0.1 s
	constexpr uint32_t FramesPerTick = md::g_samplerate * 60 / (150 * 24);
	constexpr uint32_t Chunk = 49;
	constexpr int TicksPerStep = 6;
	constexpr uint16_t GndSin = 1;
	constexpr uint8_t VolumeBit = sysex::mmLockBit(md::automation::monomachine::Amplification, 5);
	constexpr uint8_t Lfo3Bit = sysex::mmLockBit(md::automation::monomachine::Lfo3, 2);
	static_assert(VolumeBit == 13 && Lfo3Bit == 50, "lock bits");

	struct StepSound
	{
		double rms = 0;
		int crossings = 0;		// of the left channel, through 0
	};

	// The pattern selected, from START, a step at a time
	std::vector<StepSound> play(Monomachine& _mm, const int _steps)
	{
		_mm.send({0xfc});
		advanceFrames(_mm.hardware(), FramesPerTick * 12);
		std::array<std::vector<float>, 2> audio;
		synthLib::TAudioOutputs outputs{};
		for(size_t channel = 0; channel < audio.size(); ++channel)
		{
			audio[channel].resize(Chunk);
			outputs[channel] = audio[channel].data();
		}
		std::vector<StepSound> steps(static_cast<size_t>(_steps));
		std::vector<double> sums(steps.size(), 0.0);
		std::vector<int> counts(steps.size(), 0);
		float previous = 0;
		_mm.send({0xfa});
		for(int tick = 0; tick < _steps * TicksPerStep; ++tick)
		{
			_mm.send({0xf8});
			const auto step = static_cast<size_t>(tick / TicksPerStep);
			for(uint32_t frame = 0; frame < FramesPerTick; frame += Chunk)
			{
				_mm.hardware().processAudio(outputs, Chunk, 0);
				for(const auto sample : audio[0])
				{
					sums[step] += static_cast<double>(sample) * sample;
					++counts[step];
					if((sample >= 0) != (previous >= 0))
						++steps[step].crossings;
					previous = sample;
				}
			}
		}
		_mm.send({0xfc});
		advanceFrames(_mm.hardware(), FramesPerTick * 12);
		for(size_t step = 0; step < steps.size(); ++step)
			steps[step].rms = counts[step] ? std::sqrt(sums[step] / counts[step]) : 0.0;
		return steps;
	}

	// The playing step and whether the sequencer runs, as JOUER lights them (Hardware::readSequencerPosition,
	// read from the firmware's RAM): the selected pattern of 16 steps played from START counts 0 to 15 and
	// starts over, and STOP stops it
	void checkSequencerPosition(Monomachine& _mm, std::vector<std::string>& _failures)
	{
		// Whether it plays comes from the sequencer's tick moving between two reads: read every tick,
		// and as Device does, every block, while stopped
		const auto stoppedFor = [&](const uint32_t _frames)
		{
			std::optional<md::Hardware::SequencerPosition> last;
			for(uint32_t frame = 0; frame < _frames; frame += 256)
			{
				advanceFrames(_mm.hardware(), 256);
				last = _mm.hardware().readSequencerPosition();
			}
			return last;
		};
		const auto before = stoppedFor(md::g_samplerate / 2);
		if(!before || before->playing)
			_failures.push_back("sequencer position unknown, or playing before START");
		_mm.send({0xfa});
		std::vector<uint8_t> steps;
		bool playing = true;
		for(int tick = 0; tick < 20 * TicksPerStep; ++tick)
		{
			_mm.send({0xf8});
			advanceFrames(_mm.hardware(), FramesPerTick);
			const auto position = _mm.hardware().readSequencerPosition();
			// From its second tick on, the first seen moving
			if(tick >= 2)
				playing &= position && position->playing;
			if(position && (steps.empty() || steps.back() != position->step))
				steps.push_back(position->step);
		}
		_mm.send({0xfc});
		const auto after = stoppedFor(md::g_samplerate / 2);
		bool counts = steps.size() >= 16;
		for(size_t i = 1; i < steps.size(); ++i)
			counts &= steps[i] == (steps[i - 1] + 1) % 16;
		std::string seen;
		for(const auto step : steps)
			seen += " " + std::to_string(step);
		std::printf("MM sequencer position, 20 steps of 16:%s; after STOP %s\n", seen.c_str(),
			after && !after->playing ? "stopped" : "still playing");
		if(!playing || !counts)
			_failures.push_back("the sequencer position does not count the steps of the pattern");
		if(!after || after->playing)
			_failures.push_back("the sequencer position plays on after STOP");
	}

	void checkFactoryPattern(const Pattern& _pattern, const uint8_t _slot, std::vector<std::string>& _failures)
	{
		const auto name = "A0" + std::to_string(_slot + 1);
		size_t trigs = 0;
		size_t rows = 0;
		for(uint8_t track = 0; track < Pattern::TrackCount; ++track)
		{
			trigs += std::bitset<64>(_pattern.trigs[track]).count();
			rows += std::bitset<64>(_pattern.lockMasks[track]).count();
			for(uint8_t step = 0; step < Pattern::StepCount; ++step)
			{
				const bool amp = (_pattern.ampTrigs[track] >> step) & 1u;
				if(amp && !_pattern.note(track, step))
					_failures.push_back(name + ": a trig without its note");
				if(!_pattern.hasTrig(track, step) && _pattern.notes[track][step] != Pattern::None)
					_failures.push_back(name + ": a note without a trig");
			}
			for(uint8_t bit = 0; bit < Pattern::LockBitCount; ++bit)
			{
				if(_pattern.lockedSteps(track, bit) & ~(_pattern.trigs[track] | _pattern.ampTrigs[track]))
					_failures.push_back(name + ": a lock on a step without a trig");
			}
		}
		std::printf("MM %s: %u steps, Kit %u, %zu trigs, %zu lock rows\n", name.c_str(), _pattern.length, _pattern.kit + 1,
			trigs, _pattern.lockRows.size());
		if(_pattern.slot != _slot || _pattern.length != 64 || _pattern.kit != _slot || trigs == 0)
			_failures.push_back(name + ": not the factory pattern's length, Kit or trigs");
		if(_pattern.lockRows.size() != std::min<size_t>(rows, Pattern::LockRowCount))
			_failures.push_back(name + ": not a lock row per set bit");
	}

	bool run()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("mmPatternFirmwareTest: SKIP (GEARMULATOR_MM_FIRMWARE_BIN not set)\n");
			return false;
		}
		std::vector<uint8_t> rom;
		requireThat(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		requireThat(md::RomLoader::isRomForModel(rom, Model), std::string(path) + " is not the Monomachine firmware");
		Monomachine mm(rom, path);
		mm.follow();
		std::printf("MM follows MIDI clock and transport\n");

		std::vector<std::string> failures;
		for(uint8_t slot = 0; slot < 4; ++slot)
		{
			const auto pattern = sysex::parseMmPatternDump(mm.readPattern(slot));
			requireThat(pattern.has_value(), "factory pattern not decoded");
			checkFactoryPattern(*pattern, slot, failures);
		}

		// A01, every trig cleared, 16 steps; track 1: C3, C4, C3 with AMP VOL locked at 0, C3 with an LFO 3
		// setting locked at 0
		auto editor = sysex::MmPatternEditor::fromDump(mm.readPattern(0));
		requireThat(editor.has_value(), "A01 not editable");
		for(uint8_t track = 0; track < Pattern::TrackCount; ++track)
		{
			for(uint8_t step = 0; step < Pattern::StepCount; ++step)
				requireThat(editor->setTrig(track, step, std::nullopt), "trig not cleared");
		}
		requireThat(editor->setLength(16), "length refused");
		requireThat(editor->setTrig(0, 0, 48) && editor->setTrig(0, 4, 60) && editor->setTrig(0, 8, 48)
			&& editor->setTrig(0, 12, 48), "trigs refused");
		requireThat(editor->setLock(0, VolumeBit, 8, 0) && editor->setLock(0, Lfo3Bit, 12, 0), "locks refused");
		const auto sent = sysex::parseMmPatternDump(editor->toDump());
		mm.writePattern(editor->toDump());
		const auto written = sysex::parseMmPatternDump(mm.readPattern(0));
		requireThat(sent && written, "written A01 not read back");
		std::printf("MM A01 read back: %u steps, track 1 trigs %04llx, notes %u %u %u %u, lock bits %016llx\n", written->length,
			static_cast<unsigned long long>(written->trigs[0]), written->notes[0][0], written->notes[0][4], written->notes[0][8],
			written->notes[0][12], static_cast<unsigned long long>(written->lockMasks[0]));
		if(written->length != 16 || written->trigs != sent->trigs || written->ampTrigs != sent->ampTrigs
			|| written->notes != sent->notes || written->lockMasks != sent->lockMasks || written->lockRows != sent->lockRows)
			failures.push_back("A01 did not come back as written");

		// Played with a sine on track 1
		mm.selectPattern(0);
		const auto assign = sysex::assignMachine(Model, 0, GndSin);
		requireThat(assign.has_value(), "no GND-SIN");
		mm.send(*assign);
		advanceFrames(mm.hardware(), md::g_samplerate / 2);
		const auto steps = play(mm, 16);
		for(size_t step = 0; step < steps.size(); ++step)
			std::printf("  step %2zu: rms %.4f, %d crossings\n", step + 1, steps[step].rms, steps[step].crossings);
		const auto& c3 = steps[0];
		const auto& c4 = steps[4];
		if(c3.rms < 0.05 || std::abs(c4.crossings - 2 * c3.crossings) > 3)
			failures.push_back("C4 did not sound an octave above C3");
		for(const size_t step : {9, 10, 11})
		{
			if(steps[step].rms > c3.rms / 100)
				failures.push_back("the trig with AMP VOL locked at 0 (bit 13) sounds at step " + std::to_string(step + 1));
		}
		if(steps[12].rms < c3.rms / 2)
			failures.push_back("the trig with lock bit 50 at 0 is not as loud as the first");
		checkSequencerPosition(mm, failures);

		for(const auto& failure : failures)
			std::printf("  FAIL %s\n", failure.c_str());
		requireThat(failures.empty(), std::to_string(failures.size()) + " checks failed");
		std::printf("mmPatternFirmwareTest: PASS\n");
		return true;
	}
}

int main()
{
	try
	{
		return run() ? 0 : 77;
	}
	catch(const std::exception& _error)
	{
		std::printf("mmPatternFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
