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
// More checks. SONG POSITION and CONTINUE, which the plug-in sends when the host
// starts later in the song, start A01 at that position within the pattern,
// modulo its length, on the first clock. With A02 on another Kit, which the
// machine loads at the switch, A02 still takes over on time: with TRX-CH on
// track 3 of both patterns, every step sounds across the switch, its attack as
// early as before, with no silence. Audio tracks send a note of their own on
// each trig (track 3: note 40), below the locked notes. Selecting, while
// stopped, the pattern the machine already has loads its Kit again: a change
// to the Kit not saved is lost. A pattern asked for while playing and still
// waiting at STOP plays at the next START, though CURRENT PATTERN still
// answers the old one.
//
// Last, md::ChainPlayer between the transport and the machine, as the Device
// runs it: the chain A01 (16 steps) twice then A02 plays every step as the
// chain says, from START over two rounds and from a song position inside A02,
// and a chain beginning with A02 starts on A02 though the machine had A01.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdLib/mdchainplayer.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
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
	// The note locked on step 1: A01 plays 60 to 91, A02 92 to 123, above the notes the audio tracks send
	constexpr int FirstNotes[2] = {60, 92};

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

	// How a run starts and what it asks for
	struct Playback
	{
		int songPosition = 0;			// in sixteenths; above 0, SONG POSITION and CONTINUE instead of START
		std::optional<Request> request;
		int ticks = 0;
		bool selectA01 = true;			// select A01 before starting, as it is already
	};

	struct Run
	{
		int requestTick = -1;
		std::vector<PlayedStep> steps;
		std::vector<int> otherNotes;
		std::optional<double> statusA02;	// the first CURRENT PATTERN status answering A02
		std::vector<std::pair<double, uint8_t>> programChanges;	// sent by the machine
		std::vector<std::pair<double, float>> peaks;	// the main output's peak of every chunk, at its end
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

	// Track 3 of _kit holds TRX-CH, a short sound that shows on the main output when each step plays;
	// tracks 1 and 2 MID-01 and MID-02. Saved.
	void prepareAudioKit(Machinedrum& _md, const uint8_t _kit)
	{
		constexpr uint16_t trxCh = 22;
		for(uint8_t track = 0; track < 3; ++track)
		{
			const auto message = sysex::assignMachine(Model, track, track < 2 ? MidMachines[track] : trxCh);
			require(message.has_value(), "codec refused a machine");
			_md.send(*message);
			_md.advance(md::g_samplerate / 2);
		}
		_md.send(sysex::kitSave(Model, _kit));
		_md.advance(md::g_samplerate);
		const auto saved = sysex::parseKitDump(Model, _md.exchange(sysex::kitRequest(Model, _kit), 0x52));
		require(saved && saved->machines.size() > 2 && saved->machines[0] == MidMachines[0]
			&& saved->machines[1] == MidMachines[1] && saved->machines[2] == trxCh, "Kit " + std::to_string(_kit + 1)
			+ " does not hold MID-01, MID-02 and TRX-CH");
	}

	// Pattern _slot (0 or 1), _length steps, plays track _slot + 1 on each step, each step's note locked;
	// with _audio, track 3 on each step too
	void writePattern(Machinedrum& _md, const uint8_t _slot, const int _length, const bool _audio = false)
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
				&& editor->setLock(_slot, 0, static_cast<uint8_t>(step), note)
				&& (!_audio || editor->setTrig(2, static_cast<uint8_t>(step), true)), "pattern edit refused");
		}
		require(editor->setLength(static_cast<uint8_t>(_length)), "length refused");
		const auto written = editor->toDump();
		_md.send(written);
		_md.advance(md::g_samplerate);
		require(_md.readPattern(_slot) == written, "the firmware did not keep " + patternName(_slot) + " as sent");
		std::printf("MD %s: %d steps, track %u on every step, NOTE locked to %d and up%s\n", patternName(_slot).c_str(),
			_length, _slot + 1, FirstNotes[_slot], _audio ? ", track 3 on every step" : "");
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

	// A01 from START, or from a song position as synthLib::MidiClock starts the machine later in the
	// song; A02 asked for during A01's second pass as the request says
	Run play(Machinedrum& _md, const Tempo& _tempo, const int _lengthA01, const Playback& _playback, const uint8_t _baseChannel)
	{
		const auto framesPerTick = _tempo.framesPerTick();
		_md.send({0xfc});
		_md.advance(framesPerTick * 12);
		if(_playback.selectA01)
			_md.selectPattern(0);
		_md.drain();

		Run run;
		if(_playback.request)
			run.requestTick = (_lengthA01 + _playback.request->step) * TicksPerStep + _playback.request->tick;
		auto& hardware = _md.hardware();
		std::vector<synthLib::SMidiEvent> events;
		std::array<std::vector<float>, 2> audio;
		synthLib::TAudioOutputs outputs{};
		for(size_t channel = 0; channel < audio.size(); ++channel)
		{
			audio[channel].resize(_tempo.chunk);
			outputs[channel] = audio[channel].data();
		}
		if(_playback.songPosition > 0)
		{
			_md.send({0xf2, static_cast<uint8_t>(_playback.songPosition & 0x7f), static_cast<uint8_t>(_playback.songPosition >> 7)});
			_md.send({0xfb});
		}
		else
		{
			_md.send({0xfa});
		}
		for(int tick = 0; tick < _playback.ticks; ++tick)
		{
			_md.send({0xf8});
			if(tick == run.requestTick)
			{
				if(_playback.request->programChange)
					_md.send({static_cast<uint8_t>(0xc0 | _baseChannel), 1});
				else
					_md.send(wrap(md::midiProtocol::selectPattern(Model, 1)));
			}
			// CURRENT PATTERN, asked in the middle of every step
			if(tick % TicksPerStep == TicksPerStep / 2)
				_md.send(sysex::statusRequest(Model, Status::Pattern));
			for(uint32_t frame = 0; frame < framesPerTick; frame += _tempo.chunk)
			{
				hardware.processAudio(outputs, _tempo.chunk, 0);
				const double now = tick + static_cast<double>(frame + _tempo.chunk) / framesPerTick;
				float peak = 0;
				for(const auto& channel : audio)
				{
					for(const auto sample : channel)
						peak = std::max(peak, std::abs(sample));
				}
				run.peaks.emplace_back(now, peak);
				events.clear();
				hardware.readMidiOut(events);
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
	// _audioTrack: track 3 plays too, and sends its own notes
	std::string check(const Run& _run, const Tempo& _tempo, const int _lengthA01, const int _lengthA02, const Request& _request,
		const bool _audioTrack)
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
		{
			const auto text = std::to_string(_run.otherNotes.size()) + " notes no step locked, the first "
				+ std::to_string(_run.otherNotes.front());
			if(!_audioTrack)
				return text;
			std::printf("  %s (track 3)\n", text.c_str());
		}

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

	// SONG POSITION and CONTINUE: where A01 starts, for a position inside the pattern and one past its length
	void checkSongPosition(Machinedrum& _md, const Tempo& _tempo, const int _length, std::vector<std::string>& _failures)
	{
		for(const int position : {8, _length + 8})
		{
			const Playback playback{position, std::nullopt, 2 * _length * TicksPerStep};
			const auto run = play(_md, _tempo, _length, playback, 0);
			std::string first;
			for(size_t i = 0; i < run.steps.size() && i < 4; ++i)
			{
				char text[64];
				std::snprintf(text, sizeof(text), " %s @%.1f", describe(run.steps[i]).c_str(), run.steps[i].tick);
				first += text;
			}
			std::printf("MD SONG POSITION %d (sixteenths) and CONTINUE, A01 of %d steps: first steps%s\n", position, _length,
				first.empty() ? " none" : first.c_str());
			// At the position within the pattern, on the first clock
			const int expected = position % _length;
			if(run.steps.empty() || run.steps.front().pattern != 0 || run.steps.front().step != expected || run.steps.front().tick >= 1)
			{
				_failures.push_back("SONG POSITION " + std::to_string(position) + " did not start A01 at step "
					+ std::to_string(expected + 1) + " on the first clock");
			}
		}
	}

	// A02 on another Kit, which the machine loads when A02 takes over. Track 3 plays TRX-CH on every
	// step of both patterns: the main output shows whether every step still sounds, and when, across
	// the switch.
	template<typename RunRequest>
	void checkKitChange(Machinedrum& _md, const Tempo& _tempo, std::vector<std::string>& _failures, const RunRequest& _runRequest)
	{
		_md.selectPattern(0);
		const auto kit = _md.status(Status::Kit);
		prepareAudioKit(_md, kit);
		const auto other = static_cast<uint8_t>((kit + 1) % 64);
		_md.selectPattern(1);
		// Extended mode: the selected pattern remembers the Kit selected with it
		_md.send(wrap(md::midiProtocol::setStatus(Model, static_cast<uint8_t>(Status::Kit), other)));
		_md.advance(md::g_samplerate / 2);
		prepareAudioKit(_md, other);
		_md.selectPattern(0);
		_md.selectPattern(1);
		require(_md.status(Status::Kit) == other, "A02 did not keep Kit " + std::to_string(other + 1));
		writePattern(_md, 0, 32, true);
		writePattern(_md, 1, 32, true);
		std::printf("MD A01 on Kit %u, A02 on Kit %u, track 3 TRX-CH on every step of both\n", kit + 1, other + 1);

		const auto run = _runRequest(Request{false, 16, 0});
		// The loudest chunk of each step, and the attack: how long after the step's clock tick
		// the output first reached half of it
		const int steps = 3 * 32 + 8;
		const int switchStep = 2 * 32;
		const double msPerTick = 60000.0 / (_tempo.bpm * 24.0);
		const auto stepOf = [](const double _tick) { return static_cast<int>(std::floor((_tick - 1e-9) / TicksPerStep)); };
		std::vector<float> loudest(static_cast<size_t>(steps), 0.0f);
		std::vector<double> delay(static_cast<size_t>(steps), -1.0);
		size_t silent = 0;
		size_t longestSilence = 0;
		for(const auto& [tick, peak] : run.peaks)
		{
			const auto index = stepOf(tick);
			if(std::abs(tick - switchStep * TicksPerStep) < 2 * TicksPerStep)
			{
				silent = peak < 1e-4f ? silent + 1 : 0;
				longestSilence = std::max(longestSilence, silent);
			}
			if(index >= 0 && index < steps)
				loudest[static_cast<size_t>(index)] = std::max(loudest[static_cast<size_t>(index)], peak);
		}
		for(const auto& [tick, peak] : run.peaks)
		{
			const auto index = stepOf(tick);
			if(index < 0 || index >= steps || delay[static_cast<size_t>(index)] >= 0
				|| peak < loudest[static_cast<size_t>(index)] / 2)
				continue;
			delay[static_cast<size_t>(index)] = (tick - index * TicksPerStep) * msPerTick;
		}
		std::vector<float> steady(loudest.begin() + 4, loudest.begin() + switchStep - 4);
		std::sort(steady.begin(), steady.end());
		const auto median = steady[steady.size() / 2];
		std::string around;
		for(int index = switchStep - 3; index < switchStep + 4; ++index)
		{
			char text[64];
			std::snprintf(text, sizeof(text), " %d:%.3f@%+.1fms", index + 1, loudest[static_cast<size_t>(index)],
				delay[static_cast<size_t>(index)]);
			around += text;
		}
		std::printf("  track 3 on the main output, median step peak %.3f; steps around the switch (clock step:peak@delay):%s;"
			" longest silence within two steps of it %.1f ms\n", median, around.c_str(),
			static_cast<double>(longestSilence) * _tempo.chunk * 1000.0 / md::g_samplerate);
		for(int index = 1; index < steps; ++index)
		{
			if(loudest[static_cast<size_t>(index)] < median / 4)
			{
				_failures.push_back("with a Kit change, clock step " + std::to_string(index + 1) + " did not sound");
				break;
			}
		}
	}

	// Whether selecting the pattern the machine already has, while stopped, loads its Kit again and so
	// drops the Kit's unsaved changes: track 3 (TRX-CH on every step of A01) turned down live, then
	// played without selecting A01 and after selecting it
	bool reselectionReloadsKit(Machinedrum& _md, const Tempo& _tempo, const uint8_t _baseChannel)
	{
		const auto loudest = [](const Run& _run)
		{
			float peak = 0;
			for(const auto& [tick, value] : _run.peaks)
				peak = std::max(peak, value);
			return peak;
		};
		const auto turnedDown = md::automation::encodeParameterChange(Model,
			{md::automation::machinedrum::Level, 2, 0, 0}, _baseChannel);
		require(turnedDown.has_value(), "codec refused the Level of track 3");
		_md.send({(*turnedDown)[0], (*turnedDown)[1], (*turnedDown)[2]});
		_md.advance(md::g_samplerate / 2);
		Playback playback{0, std::nullopt, 8 * TicksPerStep, false};
		const auto kept = loudest(play(_md, _tempo, 32, playback, _baseChannel));
		playback.selectA01 = true;
		const auto reselected = loudest(play(_md, _tempo, 32, playback, _baseChannel));
		std::printf("MD track 3 turned down live: peak %.4f when played as it is, %.4f after selecting A01 again%s\n",
			kept, reselected, reselected > 10 * std::max(kept, 1e-4f) ? ": the Kit was loaded again" : ": the Kit kept the change");
		require(kept < 0.01f, "the live Level change did not silence track 3");
		return reselected > 10 * std::max(kept, 1e-4f);
	}

	// A02 asked for while A01 plays, then STOP before A01 ends: the pattern the machine has then, by its
	// status and by what START plays
	std::pair<uint8_t, int> patternAfterStop(Machinedrum& _md, const Tempo& _tempo, const uint8_t _baseChannel)
	{
		const Playback asked{0, Request{false, 0, 1}, 36 * TicksPerStep};
		(void)play(_md, _tempo, 32, asked, _baseChannel);
		const auto status = _md.status(Status::Pattern);
		const Playback resumed{0, std::nullopt, 2 * TicksPerStep, false};
		const auto run = play(_md, _tempo, 32, resumed, _baseChannel);
		const int played = run.steps.empty() ? -1 : run.steps.front().pattern;
		std::printf("MD A02 asked for at A01's second pass, STOP four steps later: CURRENT PATTERN %s, START plays %s\n",
			patternName(status).c_str(), played < 0 ? "nothing" : patternName(played).c_str());
		return {status, played};
	}

	// The chain the plug-in plays: the transport goes through ChainPlayer, as the Device runs it, then to
	// the machine, and what the machine sends goes back to the player
	Run playChain(Machinedrum& _md, const Tempo& _tempo, md::ChainPlayer& _player, const int _songPosition, const int _ticks)
	{
		const auto framesPerTick = _tempo.framesPerTick();
		auto& hardware = _md.hardware();
		std::vector<synthLib::SMidiEvent> out;
		const auto forward = [&](const synthLib::SMidiEvent& _event)
		{
			out.clear();
			_player.process(_event, out);
			for(const auto& event : out)
				require(hardware.sendMidi(event), "MIDI rejected by the input");
		};
		const auto transport = [&](const uint8_t _a, const uint8_t _b = 0, const uint8_t _c = 0)
		{
			forward(synthLib::SMidiEvent(synthLib::MidiEventSource::Internal, _a, _b, _c));
		};
		Run run;
		std::vector<synthLib::SMidiEvent> events;
		const auto listen = [&](const std::optional<double> _tick)
		{
			events.clear();
			hardware.readMidiOut(events);
			for(const auto& event : events)
			{
				_player.observe(event);
				if(_tick)
					record(run, event, *_tick);
			}
		};
		const auto idle = [&](const uint32_t _ticks)
		{
			for(uint32_t frame = 0; frame < framesPerTick * _ticks; frame += _tempo.chunk)
			{
				hardware.advance(_tempo.chunk);
				listen(std::nullopt);
			}
		};

		transport(0xfc);
		idle(12);
		if(_songPosition > 0)
		{
			transport(0xf2, static_cast<uint8_t>(_songPosition & 0x7f), static_cast<uint8_t>(_songPosition >> 7));
			transport(0xfb);
		}
		else
		{
			transport(0xfa);
		}
		for(int tick = 0; tick < _ticks; ++tick)
		{
			transport(0xf8);
			for(uint32_t frame = 0; frame < framesPerTick; frame += _tempo.chunk)
			{
				hardware.advance(_tempo.chunk);
				listen(tick + static_cast<double>(frame + _tempo.chunk) / framesPerTick);
			}
		}
		transport(0xfc);
		idle(12);
		return run;
	}

	// Every clock step played what the chain plays there, from _songPosition: one note, its pattern and step
	std::string checkChain(const Run& _run, const md::PatternChain& _chain, const int _songPosition, const int _ticks)
	{
		const int steps = _ticks / TicksPerStep;
		std::vector<std::vector<PlayedStep>> byStep(static_cast<size_t>(steps));
		for(const auto& step : _run.steps)
		{
			const auto index = static_cast<int>(std::floor(step.tick / TicksPerStep));
			if(index >= 0 && index < steps)
				byStep[static_cast<size_t>(index)].push_back(step);
		}
		for(int index = 0; index < steps; ++index)
		{
			const auto tick = static_cast<uint64_t>(_songPosition + index) * TicksPerStep;
			const auto pass = _chain.passAt(tick);
			const int expected = static_cast<int>((tick - pass->start) / TicksPerStep);
			const auto& played = byStep[static_cast<size_t>(index)];
			if(played.size() == 1 && played.front().pattern == pass->pattern && played.front().step == expected)
				continue;
			std::string found;
			for(const auto& step : played)
				found += " " + describe(step);
			return "clock step " + std::to_string(index) + " played" + (found.empty() ? std::string(" nothing") : found)
				+ ", the chain plays " + patternName(pass->pattern) + " " + std::to_string(expected + 1);
		}
		return {};
	}

	// ChainPlayer between the transport and the machine: the chain A01 (16 steps) twice then A02 (32 steps),
	// from START over two rounds and from a song position inside A02; then a chain beginning with A02 while
	// the machine has A01, which START must play from its first step
	void checkChainPlayer(Machinedrum& _md, const Tempo& _tempo, std::vector<std::string>& _failures)
	{
		writePattern(_md, 0, 16);
		writePattern(_md, 1, 32);
		md::PatternChain chain;
		require(chain.setEntries({{0, 2}, {1, 1}}), "chain refused");
		chain.setLength(0, 16);
		chain.setLength(1, 32);
		md::ChainPlayer player(Model);
		player.setChain(std::make_shared<md::PatternChain>(chain));

		const auto report = [&](const char* _what, const std::string& _failure)
		{
			std::printf("MD chain A01 x2, A02 %s: %s\n", _what, _failure.empty() ? "every step as the chain plays it" : _failure.c_str());
			if(!_failure.empty())
				_failures.push_back("chain " + std::string(_what) + ": " + _failure);
		};
		const int round = static_cast<int>(chain.roundTicks());
		report("from START, two rounds", checkChain(playChain(_md, _tempo, player, 0, 2 * round + 8 * TicksPerStep), chain, 0,
			2 * round + 8 * TicksPerStep));
		report("from SONG POSITION 40 (A02's ninth step)", checkChain(playChain(_md, _tempo, player, 40, round), chain, 40, round));

		// The machine on A01, selected through the player as the editor would
		synthLib::SMidiEvent selection(synthLib::MidiEventSource::Host);
		const auto body = md::midiProtocol::selectPattern(Model, 0);
		selection.sysex.push_back(0xf0);
		selection.sysex.insert(selection.sysex.end(), body.begin(), body.end());
		selection.sysex.push_back(0xf7);
		std::vector<synthLib::SMidiEvent> out;
		player.process(selection, out);
		for(const auto& event : out)
			require(_md.hardware().sendMidi(event), "MIDI rejected by the input");
		_md.advance(md::g_samplerate / 2);
		require(_md.status(Status::Pattern) == 0 && player.getStartPattern() == uint8_t{0}, "A01 not selected");
		md::PatternChain fromA02;
		require(fromA02.setEntries({{1, 1}, {0, 1}}), "chain refused");
		fromA02.setLength(0, 16);
		fromA02.setLength(1, 32);
		player.setChain(std::make_shared<md::PatternChain>(fromA02));
		const int ticks = static_cast<int>(fromA02.roundTicks()) + 8 * TicksPerStep;
		const auto failure = checkChain(playChain(_md, _tempo, player, 0, ticks), fromA02, 0, ticks);
		std::printf("MD chain A02, A01 from START with the machine on A01: %s\n",
			failure.empty() ? "A02 selected before START and played from its first step" : failure.c_str());
		if(!failure.empty())
			_failures.push_back("chain beginning with A02: " + failure);
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
		const Tempo tempo{150, 49};
		const std::vector<Case> cases{
			{tempo, 32, {{false, 0, 1}, {false, 16, 0}, {false, 29, 5}, {false, 30, 4}, {false, 30, 5}, {false, 31, 5},
				{true, 16, 0}, {true, 30, 4}, {true, 30, 5}}},
			{tempo, 16, {{false, 8, 0}, {false, 14, 4}, {false, 14, 5}, {false, 15, 0}}},
			{{98, 45}, 16, {{false, 14, 4}, {false, 14, 5}}},
			{{210, 35}, 16, {{false, 14, 4}, {false, 14, 5}}}};
		size_t count = 0;
		std::vector<std::string> failures;
		const auto run = [&](const Tempo& _tempo, const int _lengthA01, const Request& _request, const bool _audioTrack)
		{
			++count;
			const Playback playback{0, _request, 3 * _lengthA01 * TicksPerStep + 8 * TicksPerStep};
			const auto result = play(machine, _tempo, _lengthA01, playback, baseChannel);
			const auto failure = check(result, _tempo, _lengthA01, LengthA02, _request, _audioTrack);
			if(!failure.empty())
			{
				std::printf("  FAIL %s\n", failure.c_str());
				failures.push_back(failure);
			}
			return result;
		};
		for(const auto& c : cases)
		{
			require(c.tempo.valid(), "a tempo whose clock tick is not a whole number of chunks");
			writePattern(machine, 0, c.lengthA01);
			for(const auto& request : c.requests)
				run(c.tempo, c.lengthA01, request, false);
		}

		// SONG POSITION and CONTINUE, as the plug-in starts the machine when the host starts later in the song
		writePattern(machine, 0, 32);
		checkSongPosition(machine, tempo, 32, failures);

		// A02 on another Kit, with an audible track on both patterns
		checkKitChange(machine, tempo, failures, [&](const Request& _request) { return run(tempo, 32, _request, true); });

		// Selecting the pattern the machine already has loads its Kit again: ChainPlayer avoids it
		if(!reselectionReloadsKit(machine, tempo, baseChannel))
			failures.push_back("selecting the pattern the machine has kept the Kit's unsaved change");
		// A request still queued when the transport stops plays at START, though the status does not show it
		if(patternAfterStop(machine, tempo, baseChannel) != std::pair<uint8_t, int>{0, 1})
			failures.push_back("a pattern asked for before STOP did not wait, unseen, for START");

		// The chain the plug-in plays, through ChainPlayer
		checkChainPlayer(machine, tempo, failures);

		require(failures.empty(), std::to_string(failures.size()) + " checks of " + std::to_string(count)
			+ " requests and the song positions did not follow the rule");
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
