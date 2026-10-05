// The Monomachine's pattern writes as the editor makes them (md::MmPatternWriter), against the real machine.
//
// The Monomachine takes a pattern dump only on its GLOBAL > FILE > SYSEX RECV page, in ORIG mode to put it
// in the slot it names. The writer drives the menu there from any screen, the MODE left on SPEC included,
// sends the dumps, leaves the menus and sends what comes after: here a request for the pattern written,
// whose answer comes back as written.
// - A02 rewritten (16 steps, three trigs) with the MODE left on SPEC: read back as written.
// - A01 playing on the internal clock, copied to A05 (its dump with A05's number) just after PLAY, when the
//   firmware drops a dump sent the moment WAITING... shows: A05 comes back as A01, the selected pattern
//   stays A01, the sequencer plays on and sounds through the write, and the Kit stays as it is (track 1's
//   machine assigned and not saved).
// - The selected pattern written reloads its Kit as stored (the assigned machine goes); with SAVE KIT
//   ($59) sent first, the machine stays.
// - A write as the controller asks, through the Device, comes back as written.
// A write with the writer's taps takes under a second and a half.
//
// Firmware from GEARMULATOR_MM_FIRMWARE_BIN; 77 without it. MM_WRITE_PANEL_PREFIX writes the LCD at the
// stages of the first write, and every 8 blocks of the copy. MM_WRITE_TIMING_SWEEP instead writes with
// shorter and shorter taps, stopped and playing, then copies just after PLAY, and tells what comes back and
// how often the dump was sent: how the writer's timing was chosen.

#include "mmFirmwareMachine.h"

#include "mdLib/mdmmpatternwriter.h"
#include "mdLib/mdromloader.h"

#include "baseLib/filesystem.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	using namespace md::test;
	namespace sysex = md::automation::sysex;
	using Message = sysex::Message;
	using Pattern = sysex::MmPatternDump;
	constexpr auto Model = md::MachineModel::Monomachine;
	constexpr uint16_t GndSin = 1;

	void probe(md::Hardware& _hardware, const std::string& _name)
	{
		if(const auto* prefix = std::getenv("MM_WRITE_PANEL_PREFIX"))
			panelImage(_hardware, std::string(prefix) + "-" + _name + ".pgm");
	}

	bool samePattern(const Pattern& _a, const Pattern& _b)
	{
		return _a.length == _b.length && _a.kit == _b.kit && _a.trigs == _b.trigs && _a.ampTrigs == _b.ampTrigs
			&& _a.notes == _b.notes && _a.lockMasks == _b.lockMasks && _a.lockRows == _b.lockRows;
	}

	struct WriteRun
	{
		std::vector<Message> answers;		// SysEx the machine sent meanwhile
		std::vector<int> steps;				// the sequencer's step every half second, -1 unknown
		std::vector<float> levels;			// rms of the main output every half second
		double seconds = 0;					// until the write was done
	};

	// Services the writer as the Device does, every 256 frames, until the write is done and one more second
	// for the answers to what it sent after. _probe: the LCD every 8 blocks under MM_WRITE_PANEL_PREFIX.
	WriteRun runWrite(Monomachine& _mm, md::MmPatternWriter& _writer, md::MmPatternWriter::Write _write,
		const std::string& _probe = {})
	{
		auto& hw = _mm.hardware();
		const auto id = _write.id;
		requireThat(_writer.start(std::move(_write)), "the writer is busy");
		md::MmPatternWriter::Actions actions;
		actions.sendSysex = [&](const Message& _message) { _mm.send(_message); };
		actions.sendPanel = [&](const md::PanelPacket& _packet) { return hw.trySendPanelEvent(_packet.row, _packet.mask); };
		actions.screenDigest = [&] { return hw.lcdPagesDigest(6, 7); };
		std::array<std::vector<float>, 2> audio;
		synthLib::TAudioOutputs outputs{};
		for(size_t channel = 0; channel < audio.size(); ++channel)
		{
			audio[channel].resize(256);
			outputs[channel] = audio[channel].data();
		}
		WriteRun run;
		double sum = 0;
		size_t count = 0;
		uint64_t frames = 0;
		uint64_t doneAt = 0;
		std::vector<synthLib::SMidiEvent> events;
		while(frames < md::g_samplerate * 30)
		{
			hw.processAudio(outputs, 256, 0);
			frames += 256;
			for(const auto sample : audio[0])
			{
				sum += static_cast<double>(sample) * sample;
				++count;
			}
			if(frames % (md::g_samplerate / 2) < 256)
			{
				const auto position = hw.readSequencerPosition();
				run.steps.push_back(position ? position->step : -1);
				run.levels.push_back(count ? static_cast<float>(std::sqrt(sum / count)) : 0.0f);
				sum = 0;
				count = 0;
			}
			events.clear();
			hw.readMidiOut(events);
			for(const auto& event : events)
			{
				if(!event.sysex.empty())
					run.answers.emplace_back(event.sysex.begin(), event.sysex.end());
			}
			_writer.service(hw.getEmulatedFrames(), 1, actions);
			if(!_probe.empty() && !doneAt && (frames / 256) % 8 == 0)
				probe(hw, _probe + "-" + std::to_string(frames / 256));
			if(!doneAt && _writer.getDone() == id)
			{
				doneAt = frames;
				run.seconds = static_cast<double>(frames) / md::g_samplerate;
			}
			if(doneAt && frames >= doneAt + md::g_samplerate)
				return run;
		}
		throw std::runtime_error("the write did not end within 30 s");
	}

	std::optional<Pattern> answeredPattern(const WriteRun& _run, const uint8_t _slot)
	{
		for(const auto& answer : _run.answers)
		{
			const auto pattern = sysex::parseMmPatternDump(answer);
			if(pattern && pattern->slot == _slot)
				return pattern;
		}
		return std::nullopt;
	}

	// MM_WRITE_TIMING_SWEEP: A02 written with shorter and shorter taps, each time from the GLOBAL menu with the
	// MODE left on SPEC, stopped then with A01 playing: what comes back, how long it took, how often the dump
	// was sent. Then A01 copied to A05 just after PLAY, as the test below does: when the firmware drops the dump.
	void sweepTimings(Monomachine& _mm)
	{
		auto& hw = _mm.hardware();
		const std::pair<uint32_t, uint32_t> taps[] = {{1024, 2048}, {512, 1024}, {256, 512}, {128, 256}, {64, 128}, {32, 64}, {16, 32}};
		uint8_t variant = 0;
		uint32_t id = 1000;
		for(const bool playing : {false, true})
		{
			if(playing)
			{
				_mm.selectPattern(0);
				panelTap(hw, md::PanelControl::Play);
			}
			for(const auto& [hold, settle] : taps)
			{
				enterMmReceive(hw, false);
				panelTap(hw, md::PanelControl::Down);
				panelTap(hw, md::PanelControl::Exit);
				md::MmPatternWriter::Timing timing;
				timing.panel = {hold, settle};
				md::MmPatternWriter writer(timing);
				auto a02 = sysex::MmPatternEditor::fromDump(_mm.readPattern(1));
				requireThat(a02.has_value(), "A02 not editable");
				a02->clear();
				requireThat(a02->setLength(16) && a02->setTrig(0, variant % 16, static_cast<uint8_t>(40 + variant)), "A02 edits refused");
				++variant;
				const auto sent = sysex::parseMmPatternDump(a02->toDump());
				const auto run = runWrite(_mm, writer, {++id, {a02->toDump()}, {sysex::patternRequest(Model, 1)}});
				const auto back = answeredPattern(run, 1);
				std::printf("sweep hold %4u settle %4u %s: %s in %.2f s, sent %u times\n", hold, settle,
					playing ? "playing" : "stopped", !back ? "no answer" : samePattern(*back, *sent) ? "OK" : "DIFFERENT",
					run.seconds, writer.getSends());
			}
			if(playing)
				panelTap(hw, md::PanelControl::Stop);
		}
		auto copy = sysex::MmPatternEditor::fromDump(_mm.readPattern(0));
		requireThat(copy && copy->setSlot(4), "A01 not numbered A05");
		const auto sent = sysex::parseMmPatternDump(copy->toDump());
		for(int attempt = 0; attempt < 3; ++attempt)
		{
			auto blank = sysex::MmPatternEditor::fromDump(copy->toDump());
			blank->clear();
			md::MmPatternWriter clearing;
			runWrite(_mm, clearing, {++id, {blank->toDump()}, {}});
			panelTap(hw, md::PanelControl::Play);
			md::MmPatternWriter writer;
			const auto run = runWrite(_mm, writer, {++id, {copy->toDump()}, {sysex::patternRequest(Model, 4)}});
			const auto back = answeredPattern(run, 4);
			std::printf("A01 copied to A05 just after PLAY: %s in %.2f s, sent %u times\n",
				!back ? "no answer" : samePattern(*back, *sent) ? "OK" : "DIFFERENT", run.seconds, writer.getSends());
			panelTap(hw, md::PanelControl::Stop);
		}
	}

	uint16_t trackMachine(md::Hardware& _hardware)
	{
		const auto kit = _hardware.readLiveKit();
		requireThat(kit.has_value(), "live Kit not read");
		return kit->machines[0];
	}

	bool run()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("mmPatternWriteFirmwareTest: SKIP (GEARMULATOR_MM_FIRMWARE_BIN not set)\n");
			return false;
		}
		std::vector<uint8_t> rom;
		requireThat(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		requireThat(md::RomLoader::isRomForModel(rom, Model), std::string(path) + " is not the Monomachine firmware");
		Monomachine mm(rom, path);
		auto& hw = mm.hardware();
		if(std::getenv("MM_WRITE_TIMING_SWEEP"))
		{
			sweepTimings(mm);
			return true;
		}
		md::MmPatternWriter writer;
		std::vector<std::string> failures;
		uint32_t id = 0;

		// The MODE left on SPEC, the machine left in the GLOBAL menu
		enterMmReceive(hw, false);
		panelTap(hw, md::PanelControl::Down);
		probe(hw, "spec");
		panelTap(hw, md::PanelControl::Exit);

		// A02 rewritten: 16 steps, C3 on steps 1 and 9, C4 on step 5 of track 1
		const auto a01 = mm.readPattern(0);
		auto a02 = sysex::MmPatternEditor::fromDump(mm.readPattern(1));
		requireThat(a02.has_value(), "A02 not editable");
		a02->clear();
		requireThat(a02->setLength(16) && a02->setTrig(0, 0, 48) && a02->setTrig(0, 4, 60) && a02->setTrig(0, 8, 48),
			"A02 edits refused");
		const auto a02Sent = sysex::parseMmPatternDump(a02->toDump());
		const auto first = runWrite(mm, writer, {++id, {a02->toDump()}, {sysex::patternRequest(Model, 1)}});
		probe(hw, "after");
		const auto a02Back = answeredPattern(first, 1);
		std::printf("MM A02 written in %.1f s from the GLOBAL menu, MODE on SPEC: %s\n", first.seconds,
			!a02Back ? "no answer to the request after" : samePattern(*a02Back, *a02Sent) ? "read back as written" : "read back different");
		if(!a02Back || !samePattern(*a02Back, *a02Sent))
			failures.push_back("A02 not read back as written");
		if(first.seconds > 1.5)
			failures.push_back("a write took more than 1.5 s");

		// A01 playing, copied to A05; track 1's machine assigned, not saved
		mm.selectPattern(0);
		const auto stored = trackMachine(hw);
		const auto assign = sysex::assignMachine(Model, 0, GndSin);
		requireThat(assign.has_value(), "no GND-SIN");
		mm.send(*assign);
		advanceFrames(hw, md::g_samplerate / 2);
		requireThat(trackMachine(hw) == GndSin && stored != GndSin, "GND-SIN not assigned to track 1");
		panelTap(hw, md::PanelControl::Play);
		auto copy = sysex::MmPatternEditor::fromDump(a01);
		requireThat(copy && copy->setSlot(4), "A01 not numbered A05");
		const auto copySent = sysex::parseMmPatternDump(copy->toDump());
		const auto copied = runWrite(mm, writer, {++id, {copy->toDump()}, {sysex::patternRequest(Model, 4)}}, "copy");
		panelTap(hw, md::PanelControl::Stop);
		const auto a05 = answeredPattern(copied, 4);
		std::string steps;
		std::string levels;
		bool moving = true;
		float quietest = 1.0f;
		for(size_t i = 0; i < copied.steps.size(); ++i)
		{
			steps += " " + std::to_string(copied.steps[i]);
			char level[16];
			std::snprintf(level, sizeof(level), " %.3f", copied.levels[i]);
			levels += level;
			moving &= i == 0 || copied.steps[i] != copied.steps[i - 1];
			quietest = std::min(quietest, copied.levels[i]);
		}
		const auto selected = mm.status(Monomachine::Status::Pattern);
		const auto machine = trackMachine(hw);
		std::printf("MM A01 copied to A05 just after PLAY, in %.1f s, the dump sent %u times: %s; selected after: A0%u;"
			" track 1 machine %u (assigned %u)\n  steps every 0.5 s:%s\n  rms every 0.5 s:%s\n", copied.seconds, writer.getSends(),
			!a05 ? "no answer" : samePattern(*a05, *copySent) ? "A05 read back as A01" : "A05 read back different",
			selected + 1, machine, GndSin, steps.c_str(), levels.c_str());
		if(!a05 || !samePattern(*a05, *copySent))
		{
			if(a05)
			{
				std::printf("  A05 back: length %u/%u kit %u/%u trigs %d ampTrigs %d notes %d lockMasks %d rows %zu/%zu\n",
					a05->length, copySent->length, a05->kit, copySent->kit, a05->trigs == copySent->trigs,
					a05->ampTrigs == copySent->ampTrigs, a05->notes == copySent->notes, a05->lockMasks == copySent->lockMasks,
					a05->lockRows.size(), copySent->lockRows.size());
				size_t answers = 0;
				for(const auto& answer : copied.answers)
				{
					if(const auto pattern = sysex::parseMmPatternDump(answer))
						std::printf("  answer %zu: pattern slot %u length %u\n", answers, pattern->slot, pattern->length);
					++answers;
				}
			}
			failures.push_back("A05 not read back as the copy of A01");
		}
		if(selected != 0)
			failures.push_back("the copy changed the selected pattern");
		if(!moving || quietest < 0.01f)
			failures.push_back("the sequencer stopped or went silent during the write");
		if(machine != GndSin)
			failures.push_back("the copy to another slot reloaded the Kit");

		// The selected pattern written as it is: the Kit reloaded, then the same with SAVE KIT first
		const auto kit = mm.status(Monomachine::Status::Kit);
		const auto selectedDump = mm.readPattern(0);
		runWrite(mm, writer, {++id, {selectedDump}, {}});
		const auto reloaded = trackMachine(hw);
		mm.send(*assign);
		advanceFrames(hw, md::g_samplerate / 2);
		mm.send(sysex::kitSave(Model, kit));
		advanceFrames(hw, md::g_samplerate / 2);
		runWrite(mm, writer, {++id, {selectedDump}, {}});
		const auto saved = trackMachine(hw);
		std::printf("MM selected pattern written: track 1 machine %u (stored %u); with SAVE KIT %u first: %u\n",
			reloaded, stored, kit + 1, saved);
		if(reloaded != stored)
			failures.push_back("writing the selected pattern did not reload the Kit: the controller's SAVE KIT may no longer be needed");
		if(saved != GndSin)
			failures.push_back("SAVE KIT before the write did not keep the assigned machine");

		// Through the Device, as the plug-in's controller asks: the write queued on the control, the Device's
		// rendering (Device::process) driving it; A06 written, the request sent after it answered as written
		{
			auto control = std::make_shared<md::MmPatternWriteControl>();
			mm.device().setMmPatternWriteControl(control);
			auto a06 = sysex::MmPatternEditor::fromDump(mm.readPattern(5));
			requireThat(a06.has_value(), "A06 not editable");
			a06->clear();
			requireThat(a06->setLength(8) && a06->setTrig(1, 0, 55) && a06->setTrig(1, 4, 58), "A06 edits refused");
			const auto a06Sent = sysex::parseMmPatternDump(a06->toDump());
			const auto request = control->request({a06->toDump()}, {sysex::patternRequest(Model, 5)});
			std::array<std::vector<float>, 2> in;
			std::array<std::vector<float>, 6> out;
			synthLib::TAudioInputs inputs{};
			synthLib::TAudioOutputs outputs{};
			for(size_t channel = 0; channel < in.size(); ++channel)
			{
				in[channel].assign(256, 0.0f);
				inputs[channel] = in[channel].data();
			}
			for(size_t channel = 0; channel < out.size(); ++channel)
			{
				out[channel].assign(256, 0.0f);
				outputs[channel] = out[channel].data();
			}
			std::optional<Pattern> answer;
			std::vector<synthLib::SMidiEvent> midiIn, midiOut;
			uint64_t frames = 0;
			uint64_t doneAt = 0;
			while(frames < md::g_samplerate * 30 && (!doneAt || frames < doneAt + md::g_samplerate))
			{
				midiOut.clear();
				mm.device().process(inputs, outputs, 256, midiIn, midiOut);
				frames += 256;
				for(const auto& event : midiOut)
				{
					const Message bytes(event.sysex.begin(), event.sysex.end());
					if(const auto pattern = sysex::parseMmPatternDump(bytes); pattern && pattern->slot == 5)
						answer = pattern;
				}
				if(!doneAt && control->getDone() == request)
					doneAt = frames;
			}
			std::printf("MM A06 written through the Device in %.1f s: %s\n", static_cast<double>(doneAt) / md::g_samplerate,
				!doneAt ? "not done" : !answer ? "no answer" : samePattern(*answer, *a06Sent) ? "read back as written" : "read back different");
			if(!doneAt || !answer || !samePattern(*answer, *a06Sent))
				failures.push_back("A06 not written through the Device");
		}

		for(const auto& failure : failures)
			std::printf("  FAIL %s\n", failure.c_str());
		requireThat(failures.empty(), std::to_string(failures.size()) + " checks failed");
		std::printf("mmPatternWriteFirmwareTest: PASS\n");
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
		std::printf("mmPatternWriteFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
