// Pattern chaining on the real Monomachine: what a chain the plug-in plays needs to know there. The
// length of a pattern, read from its dump ($67) where MCL's MNMPattern puts it, and when the machine
// takes the next pattern while its sequencer plays.
//
// The machine follows MIDI clock and transport through md::HostSync, as in the plug-in (on the
// Monomachine, a front-panel macro). A01 plays from START, A02 is selected (SET STATUS CURRENT
// PATTERN) at ticks around the end of A01's second pass, and CURRENT PATTERN is asked for on every
// clock tick near that end. The machine answers the next pattern from the tick it commits to it: the
// first answer of A02 tells where the machine committed, and so which boundary A02 takes over at.
//
// The rule checked (SFX-60 OS 1.32b): the Monomachine commits on the last tick of the playing pattern,
// 1 tick before its end where the Machinedrum commits 7 before (patternChainFirmwareTest). A02 asked
// for up to 2 ticks before the end takes over at it; asked on the last tick or later, it waits one more
// pass. The same holds with A01 shortened to half through its dump, written in GLOBAL > SYSEX RECV:
// the length the chain reads is the one the sequencer plays. And, as on the Machinedrum, a pattern
// asked for and still waiting at STOP plays at the next START, though CURRENT PATTERN still answers
// the old one.
//
// Firmware from GEARMULATOR_MM_FIRMWARE_BIN; 77 without it.

#include "sysexPanelDriver.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhostsync.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdpatternchain.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace md::test;
	namespace sysex = md::automation::sysex;
	using Status = sysex::StatusParameter;
	using Target = md::HostSync::Target;
	using State = md::HostSync::State;
	constexpr auto Model = md::MachineModel::Monomachine;

	// 150 BPM: a MIDI clock tick lasts 735 frames
	constexpr uint32_t FramesPerTick = md::g_samplerate * 60 / (150 * 24);
	constexpr uint32_t Chunk = 49;
	constexpr int TicksPerStep = 6;

	void requireThat(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	sysex::Message wrap(const md::midiProtocol::SysexBody& _body)
	{
		sysex::Message message{0xf0};
		message.insert(message.end(), _body.begin(), _body.end());
		message.push_back(0xf7);
		return message;
	}

	class Monomachine
	{
	public:
		Monomachine(std::vector<uint8_t> _rom, const char* _path) : m_sync(Model)
		{
			synthLib::DeviceCreateParams params;
			params.romData = std::move(_rom);
			params.romName = _path;
			params.customData = md::deviceCustomData(Model);
			m_device = std::make_unique<md::Device>(params);
			advanceFrames(hardware(), md::g_samplerate * 20);
			requireThat(hardware().isFirmwareMidiReady(), "firmware boot incomplete");
			m_slot = status(Status::Global);
		}

		md::Hardware& hardware() { return m_device->getHardware(); }

		void send(const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_bytes.front() == 0xf0)
			{
				event.sysex.assign(_bytes.begin(), _bytes.end());
			}
			else
			{
				event.a = _bytes[0];
				event.b = _bytes.size() > 1 ? _bytes[1] : 0;
				event.c = _bytes.size() > 2 ? _bytes[2] : 0;
			}
			requireThat(hardware().sendMidi(event), "MIDI rejected by the input");
		}

		// Sends _request and returns the first SysEx answering with _command
		sysex::Message exchange(const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware().readMidiOut(events);
			send(_request);
			for(unsigned attempt = 0; attempt < 60; ++attempt)
			{
				advanceFrames(hardware(), md::g_samplerate / 10);
				events.clear();
				hardware().readMidiOut(events);
				for(const auto& event : events)
				{
					if(event.sysex.size() > 9 && event.sysex[6] == _command)
						return sysex::Message(event.sysex.begin(), event.sysex.end());
				}
			}
			throw std::runtime_error("no SysEx answer");
		}

		uint8_t status(const Status _parameter)
		{
			const auto reply = exchange(sysex::statusRequest(Model, _parameter), 0x72);
			const auto parsed = sysex::parseStatusResponse(Model, reply);
			requireThat(parsed && parsed->parameter == _parameter, "no status response");
			return parsed->value;
		}

		// Follows MIDI clock and transport, as HostSync makes it in the plug-in
		void follow()
		{
			m_sync.setTarget(Target::Follow);
			for(unsigned i = 0; i < 120; ++i)
			{
				std::vector<synthLib::SMidiEvent> events;
				for(uint32_t frames = 0; frames < md::g_samplerate / 2; frames += 256)
				{
					hardware().processAudio(256, 0);
					m_frames += 256;
					events.clear();
					hardware().readMidiOut(events);
					for(const auto& event : events)
					{
						const sysex::Message bytes(event.sysex.begin(), event.sysex.end());
						const auto dump = sysex::parseGlobalDump(Model, bytes);
						if(dump && dump->slot == m_slot)
							m_sync.onGlobalDump(bytes);
					}
					md::HostSync::Actions actions;
					actions.sendSysex = [this](const sysex::Message& _message) { send(_message); };
					actions.sendPanel = [this](const md::PanelPacket& _packet)
					{
						return hardware().trySendPanelEvent(_packet.row, _packet.mask);
					};
					actions.requestGlobal = [this] { send(sysex::globalRequest(Model, m_slot)); };
					m_sync.service(m_frames, 1, actions);
				}
				if(m_sync.getState() == State::Following)
					return;
				requireThat(m_sync.getState() != State::Failed, "host sync failed");
			}
			throw std::runtime_error("the Monomachine does not follow the host");
		}

		void selectPattern(const uint8_t _pattern)
		{
			send(wrap(md::midiProtocol::selectPattern(Model, _pattern)));
			advanceFrames(hardware(), md::g_samplerate / 4);
			requireThat(status(Status::Pattern) == _pattern, "pattern not selected");
		}

		sysex::Message readPattern(const uint8_t _slot)
		{
			return exchange(sysex::patternRequest(Model, _slot), 0x67);
		}

	private:
		std::unique_ptr<md::Device> m_device;
		md::HostSync m_sync;
		uint64_t m_frames = 0;
		uint8_t m_slot = 0;
	};

	// A01 from START; A02 asked for at _requestTick; CURRENT PATTERN asked on every tick from
	// _askFrom on. Returns the tick of the first answer A02.
	std::optional<double> firstA02(Monomachine& _mm, const int _requestTick, const int _askFrom, const int _ticks)
	{
		_mm.send({0xfc});
		advanceFrames(_mm.hardware(), FramesPerTick * 12);
		_mm.selectPattern(0);
		std::vector<synthLib::SMidiEvent> events;
		_mm.hardware().readMidiOut(events);
		std::optional<double> answer;
		_mm.send({0xfa});
		for(int tick = 0; tick < _ticks; ++tick)
		{
			_mm.send({0xf8});
			if(tick == _requestTick)
				_mm.send(wrap(md::midiProtocol::selectPattern(Model, 1)));
			if(tick >= _askFrom)
				_mm.send(sysex::statusRequest(Model, Status::Pattern));
			for(uint32_t frame = 0; frame < FramesPerTick; frame += Chunk)
			{
				_mm.hardware().advance(Chunk);
				events.clear();
				_mm.hardware().readMidiOut(events);
				for(const auto& event : events)
				{
					const sysex::Message bytes(event.sysex.begin(), event.sysex.end());
					const auto status = sysex::parseStatusResponse(Model, bytes);
					if(status && status->parameter == Status::Pattern && status->value == 1 && !answer)
						answer = tick + static_cast<double>(frame + Chunk) / FramesPerTick;
				}
			}
		}
		_mm.send({0xfc});
		advanceFrames(_mm.hardware(), FramesPerTick * 12);
		return answer;
	}

	bool runMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("mmPatternChainFirmwareTest: SKIP (GEARMULATOR_MM_FIRMWARE_BIN not set)\n");
			return false;
		}
		std::vector<uint8_t> rom;
		requireThat(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		requireThat(md::RomLoader::isRomForModel(rom, Model), std::string(path) + " is not the Monomachine firmware");
		Monomachine mm(rom, path);
		mm.follow();
		std::printf("MM follows MIDI clock and transport\n");

		// The factory patterns' lengths, Kits and double tempo
		std::vector<std::string> failures;
		for(uint8_t slot = 0; slot < 4; ++slot)
		{
			const auto dump = mm.readPattern(slot);
			const auto parsed = sysex::parseMmPatternDump(dump);
			requireThat(parsed.has_value() && parsed->slot == slot, "pattern dump not decoded");
			std::printf("MM pattern %u: %zu bytes, length %u, double tempo %d, Kit %u\n", slot + 1, dump.size(),
				parsed->length, parsed->doubleTempo ? 1 : 0, parsed->kit + 1);
		}
		const int commit = static_cast<int>(md::PatternChain::commitTicks(Model));

		// Where the machine commits, at A01's own length, then at half of it
		for(const bool shortened : {false, true})
		{
			auto dump = mm.readPattern(0);
			auto length = static_cast<int>(sysex::parseMmPatternDump(dump)->length);
			if(shortened)
			{
				// The Monomachine takes a dump only in GLOBAL > SYSEX RECV
				length /= 2;
				const auto written = sysex::withMmPatternLength(dump, static_cast<uint8_t>(length));
				requireThat(written.has_value(), "length not encoded");
				enterMmReceive(mm.hardware(), false);
				mm.send(*written);
				advanceFrames(mm.hardware(), md::g_samplerate * 2);
				exitMenus(mm.hardware());
				advanceFrames(mm.hardware(), md::g_samplerate);
				const auto readBack = sysex::parseMmPatternDump(mm.readPattern(0));
				requireThat(readBack && readBack->length == length, "the shortened length did not come back, read "
					+ (readBack ? std::to_string(readBack->length) : std::string("nothing")));
			}
			const int pass = length * TicksPerStep;
			const int end = 2 * pass;
			for(const int before : {9, 6, 3, 2, 1, 0})
			{
				const auto answer = firstA02(mm, end - before, end - 12, end + pass + 2 * TicksPerStep);
				std::printf("MM A01 of %d steps, A02 asked %d ticks before the end of its second pass (tick %d): "
					"CURRENT PATTERN first answered A02 at tick %s\n", length, before, end - before,
					answer ? std::to_string(*answer).c_str() : "never");
				// On time up to the tick before the commit, else at the end of one more pass
				const int switchTick = before > commit ? end : end + pass;
				if(!answer || *answer < switchTick - commit || *answer >= switchTick)
				{
					failures.push_back("A01 of " + std::to_string(length) + " steps, A02 asked " + std::to_string(before)
						+ " ticks before the end: the commit was not on the tick before tick " + std::to_string(switchTick));
				}
			}
		}
		// A02 asked for, then STOP before A01 ends: the status, then the pattern START plays
		{
			mm.send({0xfc});
			advanceFrames(mm.hardware(), FramesPerTick * 12);
			mm.selectPattern(0);
			mm.send({0xfa});
			for(int tick = 0; tick < 12 * TicksPerStep; ++tick)
			{
				mm.send({0xf8});
				if(tick == 2 * TicksPerStep)
					mm.send(wrap(md::midiProtocol::selectPattern(Model, 1)));
				advanceFrames(mm.hardware(), FramesPerTick);
			}
			mm.send({0xfc});
			advanceFrames(mm.hardware(), FramesPerTick * 12);
			const auto stopped = mm.status(Status::Pattern);
			mm.send({0xfa});
			for(int tick = 0; tick < 2 * TicksPerStep; ++tick)
			{
				mm.send({0xf8});
				advanceFrames(mm.hardware(), FramesPerTick);
			}
			const auto started = mm.status(Status::Pattern);
			mm.send({0xfc});
			advanceFrames(mm.hardware(), FramesPerTick * 12);
			std::printf("MM A02 asked for at step 3 of A01, STOP at step 12: CURRENT PATTERN %u stopped, %u after START\n",
				stopped + 1, started + 1);
			if(stopped != 0 || started != 1)
				failures.push_back("a pattern asked for before STOP did not wait, unseen, for START");
		}
		for(const auto& failure : failures)
			std::printf("  FAIL %s\n", failure.c_str());
		requireThat(failures.empty(), std::to_string(failures.size()) + " checks did not follow the rule");
		std::printf("mmPatternChainFirmwareTest: MM PASS\n");
		return true;
	}
}

int main()
{
	try
	{
		return runMonomachine() ? 0 : 77;
	}
	catch(const std::exception& _error)
	{
		std::printf("mmPatternChainFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
