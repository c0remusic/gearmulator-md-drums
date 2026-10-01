// Pattern chaining, checked against the real Machinedrum: what a chain the plug-in
// plays can rely on when it selects the next pattern while the sequencer plays.
//
// The machine follows MIDI clock and transport, as when the plug-in follows the
// host, and sends Program Changes. Tracks 1 and 2 hold MID-01 and MID-02, so
// every step that plays leaves a note on the MIDI output: pattern A01 plays
// track 1 on each of its steps and A02 track 2, each step's note locked to its
// step number. A01 plays once, then A02 is selected (SET STATUS CURRENT PATTERN,
// or a Program Change on the base channel) at points of A01's second pass: A01
// at 32 steps and at 16, A02 at 32, at 98, 150 and 210 BPM.
//
// The rule checked: the machine commits its next pattern 7 clock ticks before
// the end of the playing one, on the last tick of its second-to-last step,
// whatever the tempo and the length. A pattern selected before that tick plays
// at the end of the playing pattern, from its first step; one selected on that
// tick or later waits one more pass. No step is lost or played twice. CURRENT
// PATTERN answers the next pattern from the commit on, not before. The
// machine's Program Change comes 12 ticks before the end, as the playing
// pattern enters its second-to-last step, and only for a pattern selected by
// then: one selected between the two still plays on time, and the machine
// announces it 12 ticks before the end of that pattern instead. The test
// prints, for each request, the steps that played around the switch, when the
// status first answered A02 and when the machine sent its Program Change.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdLib/mdhardware.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	namespace sysex = md::automation::sysex;
	using Bytes = std::vector<uint8_t>;
	using Status = sysex::StatusParameter;
	constexpr auto Model = md::MachineModel::Machinedrum;

	// A tempo whose MIDI clock tick is a whole number of frames, and how often the MIDI output is read
	struct Tempo
	{
		int bpm;
		uint32_t chunk;

		uint32_t framesPerTick() const { return md::g_samplerate * 60 / static_cast<uint32_t>(bpm * 24); }
		bool valid() const
		{
			return md::g_samplerate * 60 % static_cast<uint32_t>(bpm * 24) == 0 && framesPerTick() % chunk == 0;
		}
	};
	constexpr int TicksPerStep = 6;
	// Where the machine commits its next pattern: this many clock ticks before the end of the playing one
	constexpr int CommitTicks = 7;
	// Where it sends its Program Change for a pattern selected by then: when the playing one
	// enters its second-to-last step
	constexpr int AnnounceTicks = 2 * TicksPerStep;
	// Global v6 (MD OS 1.63): bit 0 Program Change IN, bit 1 OUT (see mdProgramChangeFirmwareTest)
	constexpr size_t ProgramChangeOffset = 0xbe;
	constexpr uint8_t ProgramChangeInOut = 3;
	// MID-01 and MID-02 (mdmachines.cpp), on tracks 1 and 2
	constexpr uint16_t MidMachines[2] = {96, 97};
	// The note locked on step 1: A01 plays 24 and up, A02 72 and up
	constexpr int FirstNotes[2] = {24, 72};

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Bytes wrap(const md::midiProtocol::SysexBody& _body)
	{
		Bytes message{0xf0};
		message.insert(message.end(), _body.begin(), _body.end());
		message.push_back(0xf7);
		return message;
	}

	// The checksum, length and F7 after a dump's data
	void finishDump(Bytes& _message)
	{
		uint32_t checksum = 0;
		for(size_t index = 9; index < _message.size(); ++index)
			checksum += _message[index];
		checksum &= 0x3fff;
		const auto length = static_cast<uint16_t>(_message.size() - 5);
		_message.push_back(static_cast<uint8_t>(checksum >> 7));
		_message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		_message.push_back(static_cast<uint8_t>(length >> 7));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	std::string patternName(const int _slot)
	{
		char name[8];
		std::snprintf(name, sizeof(name), "%c%02d", 'A' + _slot / 16, _slot % 16 + 1);
		return name;
	}

	// A step that played, from the note it left on the MIDI output
	struct PlayedStep
	{
		double tick;	// clock ticks since the first one after START, when the note was read
		int pattern;	// 0 for A01, 1 for A02
		int step;		// 0-based, from the locked note
	};

	// When and how A02 is selected during A01's second pass
	struct Request
	{
		bool programChange;	// a Program Change on the base channel, else SET STATUS
		int step;			// 0-based step of A01
		int tick;			// clock tick within that step, 0 to 5, sent right after it
	};

	struct Run
	{
		int requestTick = 0;
		std::vector<PlayedStep> steps;
		std::vector<int> otherNotes;
		std::optional<double> statusA02;	// the first CURRENT PATTERN status answering A02
		std::vector<std::pair<double, uint8_t>> programChanges;	// sent by the machine
	};

	class Machinedrum
	{
	public:
		Machinedrum(const std::vector<uint8_t>& _rom, const std::string& _path)
			: m_hardware(std::make_unique<md::Hardware>(_rom, _path, Model))
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
			while(!m_hardware->isFirmwareMidiReady() || !m_hardware->isAudioReady())
			{
				advance(64);
				require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
			}
			// MIDI-ready comes before the startup tasks settle (see mmSysexExportFirmwareTest)
			advance(md::g_samplerate * 20);
		}

		md::Hardware& hardware() { return *m_hardware; }

		void advance(uint32_t _frames)
		{
			while(_frames)
			{
				const auto count = std::min<uint32_t>(_frames, 64);
				m_hardware->advance(count);
				_frames -= count;
			}
		}

		void send(const Bytes& _message)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_message.front() == 0xf0)
			{
				event.sysex.assign(_message.begin(), _message.end());
			}
			else
			{
				event.a = _message[0];
				event.b = _message.size() > 1 ? _message[1] : 0;
				event.c = _message.size() > 2 ? _message[2] : 0;
			}
			require(m_hardware->sendMidi(event), "MIDI rejected by the input");
		}

		void drain()
		{
			std::vector<synthLib::SMidiEvent> events;
			m_hardware->readMidiOut(events);
		}

		// Sends _request and returns the first SysEx answering with _command, empty after three seconds
		Bytes exchange(const Bytes& _request, const uint8_t _command)
		{
			drain();
			send(_request);
			std::vector<synthLib::SMidiEvent> events;
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(64);
				events.clear();
				m_hardware->readMidiOut(events);
				for(const auto& event : events)
				{
					if(event.sysex.size() > 9 && event.sysex[6] == _command)
						return Bytes(event.sysex.begin(), event.sysex.end());
				}
			}
			return {};
		}

		uint8_t status(const Status _parameter)
		{
			const auto reply = exchange(sysex::statusRequest(Model, _parameter), 0x72);
			const auto parsed = sysex::parseStatusResponse(Model, reply);
			require(parsed && parsed->parameter == _parameter, "no status response");
			return parsed->value;
		}

		// With the sequencer stopped
		void selectPattern(const uint8_t _pattern)
		{
			send(wrap(md::midiProtocol::selectPattern(Model, _pattern)));
			advance(md::g_samplerate / 4);
			require(status(Status::Pattern) == _pattern, "pattern " + patternName(_pattern) + " not selected");
		}

		Bytes readPattern(const uint8_t _slot)
		{
			const auto dump = exchange(sysex::patternRequest(Model, _slot), 0x67);
			require(!dump.empty(), "no pattern dump for " + patternName(_slot));
			return dump;
		}

	private:
		std::unique_ptr<md::Hardware> m_hardware;
	};

	// Follow MIDI clock and transport as the plug-in's host sync sets the Global, and
	// Program Change in and out. Returns the base channel.
	uint8_t configureGlobal(Machinedrum& _md)
	{
		const auto slot = _md.status(Status::Global);
		const auto global = _md.exchange(sysex::globalRequest(Model, slot), 0x50);
		const auto parsed = sysex::parseGlobalDump(Model, global);
		require(parsed && global.size() == 197 && global[7] == 6, "fixture requires the MD OS 1.63 v6 Global layout");
		require(parsed->baseChannel < 16, "fixture requires a base channel");
		auto patched = *sysex::withGlobalSync(Model, global, {true, true});
		patched.resize(patched.size() - 5);
		patched[ProgramChangeOffset] = ProgramChangeInOut;
		finishDump(patched);
		_md.send(patched);
		_md.advance(md::g_samplerate / 2);
		_md.send(sysex::globalReload(Model, slot));
		_md.advance(md::g_samplerate);
		const auto stored = _md.exchange(sysex::globalRequest(Model, slot), 0x50);
		const auto sync = sysex::parseGlobalSync(Model, stored);
		require(sync && *sync == sysex::GlobalSync{true, true} && stored.size() > ProgramChangeOffset
			&& stored[ProgramChangeOffset] == ProgramChangeInOut, "Global settings not applied");
		std::printf("MD Global %u: follows MIDI clock and transport, Program Change in and out, base channel %u\n",
			slot + 1, parsed->baseChannel + 1);
		return parsed->baseChannel;
	}

	// Both patterns on one Kit whose tracks 1 and 2 hold MID-01 and MID-02, saved, so that a
	// pattern change that loads the Kit again keeps them
	void prepareKit(Machinedrum& _md)
	{
		_md.selectPattern(0);
		const auto kit = _md.status(Status::Kit);
		_md.selectPattern(1);
		const auto kitA02 = _md.status(Status::Kit);
		if(kitA02 != kit)
		{
			// Extended mode: the selected pattern remembers the Kit selected with it
			_md.send(wrap(md::midiProtocol::setStatus(Model, static_cast<uint8_t>(Status::Kit), kit)));
			_md.advance(md::g_samplerate / 2);
		}
		_md.selectPattern(0);
		for(uint8_t track = 0; track < 2; ++track)
		{
			const auto message = sysex::assignMachine(Model, track, MidMachines[track]);
			require(message.has_value(), "codec refused a MID machine");
			_md.send(*message);
			_md.advance(md::g_samplerate / 2);
		}
		_md.send(sysex::kitSave(Model, kit));
		_md.advance(md::g_samplerate);
		const auto saved = sysex::parseKitDump(Model, _md.exchange(sysex::kitRequest(Model, kit), 0x52));
		require(saved && saved->machines.size() > 1 && saved->machines[0] == MidMachines[0]
			&& saved->machines[1] == MidMachines[1], "the saved Kit does not hold MID-01 and MID-02");
		_md.selectPattern(1);
		require(_md.status(Status::Kit) == kit, "A02 does not use A01's Kit");
		std::printf("MD Kit %u \"%s\" for A01 and A02 (A02 had Kit %u): tracks 1 and 2 hold MID-01 and MID-02\n",
			kit + 1, saved->name.c_str(), kitA02 + 1);
	}

	// Pattern _slot (0 or 1), _length steps, plays track _slot + 1 alone on each step, each step's note locked
	void writePattern(Machinedrum& _md, const uint8_t _slot, const int _length)
	{
		const auto dump = _md.readPattern(_slot);
		auto editor = sysex::MdPatternEditor::fromDump(dump);
		require(editor && editor->setLength(32), "pattern dump of " + patternName(_slot) + " not editable");
		for(uint8_t track = 0; track < 16; ++track)
		{
			for(uint8_t step = 0; step < 32; ++step)
				editor->setTrig(track, step, false);
		}
		for(int step = 0; step < _length; ++step)
		{
			const auto note = static_cast<uint8_t>(FirstNotes[_slot] + step);
			require(editor->setTrig(_slot, static_cast<uint8_t>(step), true)
				&& editor->setLock(_slot, 0, static_cast<uint8_t>(step), note), "pattern edit refused");
		}
		require(editor->setLength(static_cast<uint8_t>(_length)), "length refused");
		const auto written = editor->toDump();
		_md.send(written);
		_md.advance(md::g_samplerate);
		require(_md.readPattern(_slot) == written, "the firmware did not keep " + patternName(_slot) + " as sent");
		std::printf("MD %s: %d steps, track %u on every step, NOTE locked to %d and up\n", patternName(_slot).c_str(),
			_length, _slot + 1, FirstNotes[_slot]);
	}

	void record(Run& _run, const synthLib::SMidiEvent& _event, const double _tick)
	{
		if(!_event.sysex.empty())
		{
			const Bytes bytes(_event.sysex.begin(), _event.sysex.end());
			const auto status = sysex::parseStatusResponse(Model, bytes);
			if(status && status->parameter == Status::Pattern && status->value == 1 && !_run.statusA02)
				_run.statusA02 = _tick;
			return;
		}
		switch(_event.a & 0xf0)
		{
		case 0xc0:
			_run.programChanges.emplace_back(_tick, _event.b);
			break;
		case 0x90:
			if(_event.c == 0)
				break;
			{
				const int pattern = _event.b >= FirstNotes[1] ? 1 : 0;
				const int step = _event.b - FirstNotes[pattern];
				if(_event.b < FirstNotes[0] || step >= 32)
					_run.otherNotes.push_back(_event.b);
				else
					_run.steps.push_back({_tick, pattern, step});
			}
			break;
		default:
			break;
		}
	}

	// A01 from START, A02 asked for as _request says, until A02 has played eight steps
	// after the latest switch the rule allows
	Run play(Machinedrum& _md, const Tempo& _tempo, const int _lengthA01, const Request& _request, const uint8_t _baseChannel)
	{
		const auto framesPerTick = _tempo.framesPerTick();
		_md.send({0xfc});
		_md.advance(framesPerTick * 12);
		_md.selectPattern(0);
		_md.drain();

		const int pass = _lengthA01 * TicksPerStep;
		Run run;
		run.requestTick = pass + _request.step * TicksPerStep + _request.tick;
		auto& hardware = _md.hardware();
		std::vector<synthLib::SMidiEvent> events;
		_md.send({0xfa});
		for(int tick = 0; tick < 3 * pass + 8 * TicksPerStep; ++tick)
		{
			_md.send({0xf8});
			if(tick == run.requestTick)
			{
				if(_request.programChange)
					_md.send({static_cast<uint8_t>(0xc0 | _baseChannel), 1});
				else
					_md.send(wrap(md::midiProtocol::selectPattern(Model, 1)));
			}
			// CURRENT PATTERN, asked in the middle of every step
			if(tick % TicksPerStep == TicksPerStep / 2)
				_md.send(sysex::statusRequest(Model, Status::Pattern));
			for(uint32_t frame = 0; frame < framesPerTick; frame += _tempo.chunk)
			{
				hardware.advance(_tempo.chunk);
				events.clear();
				hardware.readMidiOut(events);
				const double now = tick + static_cast<double>(frame + _tempo.chunk) / framesPerTick;
				for(const auto& event : events)
					record(run, event, now);
			}
		}
		_md.send({0xfc});
		_md.advance(framesPerTick * 12);
		return run;
	}

	std::string describe(const PlayedStep& _step)
	{
		return patternName(_step.pattern) + " " + std::to_string(_step.step + 1);
	}

	// Prints what happened around the switch; returns what departs from the rule, empty when nothing does
	std::string check(const Run& _run, const Tempo& _tempo, const int _lengthA01, const int _lengthA02, const Request& _request)
	{
		const int pass = _lengthA01 * TicksPerStep;
		const int boundary = (_run.requestTick / pass + 1) * pass;
		const int commit = boundary - CommitTicks;
		const int switchTick = _run.requestTick < commit ? boundary : boundary + pass;
		const std::string how = _request.programChange ? "Program Change" : "SET STATUS";
		std::printf("MD %d BPM, A01 %d steps, %s for A02 at A01 step %d tick %d (clock tick %d, commit %d, boundary %d):"
			" A02 expected at tick %d\n", _tempo.bpm, _lengthA01, how.c_str(), _request.step + 1, _request.tick,
			_run.requestTick, commit, boundary, switchTick);

		std::string around;
		for(const auto& step : _run.steps)
		{
			if(step.tick >= switchTick - 3 * TicksPerStep && step.tick < switchTick + 3 * TicksPerStep)
			{
				char text[64];
				std::snprintf(text, sizeof(text), " %s @%.1f", describe(step).c_str(), step.tick);
				around += text;
			}
		}
		std::printf("  steps around it:%s\n", around.empty() ? " none" : around.c_str());
		if(_run.statusA02)
			std::printf("  CURRENT PATTERN first answered A02 at tick %.1f\n", *_run.statusA02);
		else
			std::printf("  CURRENT PATTERN never answered A02\n");
		std::string changes;
		for(const auto& [tick, program] : _run.programChanges)
		{
			char text[48];
			std::snprintf(text, sizeof(text), " %u @%.1f", program, tick);
			changes += text;
		}
		std::printf("  Program Changes from the machine:%s\n", changes.empty() ? " none" : changes.c_str());
		if(!_run.otherNotes.empty())
			return std::to_string(_run.otherNotes.size()) + " notes no step locked, the first " + std::to_string(_run.otherNotes.front());

		// One note per step, at its tick: A01 before the switch, A02 from its first step on
		const int steps = 3 * _lengthA01 + 8;
		const int switchStep = switchTick / TicksPerStep;
		std::vector<std::vector<PlayedStep>> byStep(static_cast<size_t>(steps));
		for(const auto& step : _run.steps)
		{
			const auto index = static_cast<int>(std::floor(step.tick / TicksPerStep));
			if(index >= 0 && index < steps)
				byStep[static_cast<size_t>(index)].push_back(step);
		}
		for(int index = 0; index < steps; ++index)
		{
			const int pattern = index >= switchStep ? 1 : 0;
			const int expected = pattern ? (index - switchStep) % _lengthA02 : index % _lengthA01;
			const auto& played = byStep[static_cast<size_t>(index)];
			if(played.size() != 1 || played.front().pattern != pattern || played.front().step != expected)
			{
				std::string found;
				for(const auto& step : played)
					found += " " + describe(step);
				return "clock step " + std::to_string(index) + " played" + (found.empty() ? std::string(" nothing") : found)
					+ ", expected " + patternName(pattern) + " " + std::to_string(expected + 1);
			}
		}

		// The machine's Program Change, for A02 selected by the second-to-last step; none
		// before A02 plays when selected later
		const int announce = switchTick - AnnounceTicks;
		if(_run.requestTick < announce)
		{
			if(_run.programChanges.size() != 1 || _run.programChanges.front().second != 1
				|| _run.programChanges.front().first < announce || _run.programChanges.front().first >= announce + 1)
				return "no single Program Change for A02 from the machine at tick " + std::to_string(announce);
		}
		else if(std::any_of(_run.programChanges.begin(), _run.programChanges.end(),
			[switchTick](const std::pair<double, uint8_t>& _change) { return _change.first < switchTick; }))
		{
			return "a Program Change from the machine before A02 played, though A02 was selected after its time";
		}
		// The status: A02 from the commit on
		const int committed = switchTick - CommitTicks;
		if(!_run.statusA02 || *_run.statusA02 < committed || *_run.statusA02 >= switchTick + TicksPerStep)
			return "CURRENT PATTERN did not first answer A02 in the last step before A02 played";
		return {};
	}

	bool runMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("patternChainFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n");
			return false;
		}
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		require(md::RomLoader::isRomForModel(rom, Model), std::string(path) + " is not the Machinedrum firmware");
		Machinedrum machine(rom, path);

		const auto baseChannel = configureGlobal(machine);
		prepareKit(machine);
		constexpr int LengthA02 = 32;
		writePattern(machine, 1, LengthA02);

		struct Case
		{
			Tempo tempo;
			int lengthA01;
			std::vector<Request> requests;
		};
		// Early, in the middle, on either side of the Program Change and of the commit, and late;
		// as SET STATUS and as a Program Change. The commit at three tempos.
		const std::vector<Case> cases{
			{{150, 49}, 32, {{false, 0, 1}, {false, 16, 0}, {false, 29, 5}, {false, 30, 0}, {false, 30, 3}, {false, 30, 4},
				{false, 30, 5}, {false, 31, 0}, {false, 31, 5}, {true, 16, 0}, {true, 30, 4}, {true, 30, 5}}},
			{{150, 49}, 16, {{false, 8, 0}, {false, 14, 3}, {false, 14, 4}, {false, 14, 5}, {false, 15, 0}}},
			{{98, 45}, 16, {{false, 14, 3}, {false, 14, 4}, {false, 14, 5}}},
			{{210, 35}, 16, {{false, 14, 3}, {false, 14, 4}, {false, 14, 5}}}};
		size_t count = 0;
		std::vector<std::string> failures;
		for(const auto& c : cases)
		{
			require(c.tempo.valid(), "a tempo whose clock tick is not a whole number of chunks");
			writePattern(machine, 0, c.lengthA01);
			for(const auto& request : c.requests)
			{
				++count;
				const auto failure = check(play(machine, c.tempo, c.lengthA01, request, baseChannel), c.tempo,
					c.lengthA01, LengthA02, request);
				if(failure.empty())
					continue;
				std::printf("  FAIL %s\n", failure.c_str());
				failures.push_back(failure);
			}
		}
		require(failures.empty(), std::to_string(failures.size()) + " of " + std::to_string(count)
			+ " requests did not follow the rule");
		std::printf("patternChainFirmwareTest: MD PASS\n");
		return true;
	}
}

int main()
{
	try
	{
		return runMachinedrum() ? 0 : 77;
	}
	catch(const std::exception& _error)
	{
		std::printf("patternChainFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
