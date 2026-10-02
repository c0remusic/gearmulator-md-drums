// Probe, not a test: what shows the playing step while the sequencer plays. It boots the firmware,
// presses PLAY and prints, every time it changes, the front panel's step LEDs, and counts what the
// machine sends on its MIDI output (clock, start, stop, song position). For the editor's JOUER page,
// which has no playhead yet. Firmware from GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN.

#include "mmFirmwareMachine.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto count = std::min<uint32_t>(_frames, 64);
			_hardware.advance(count);
			_frames -= count;
		}
	}

	void panelTap(md::Hardware& _hardware, const md::PanelControl _control)
	{
		const auto packet = md::panelPacket(_hardware.getModel(), _control);
		require(packet.has_value(), "unknown panel control");
		require(_hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
		advance(_hardware, 2048);
		require(_hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
		advance(_hardware, 6400);
	}

	std::string stepLeds(const md::Hardware& _hardware)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		std::string leds;
		for(uint32_t step = 0; step < 16; ++step)
		{
			if(_hardware.isMonomachine())
			{
				const auto color = panel.getMonomachineStepLedColor(step);
				leds += color == md::FrontPanel::LedColor::Off ? '.' : color == md::FrontPanel::LedColor::Green ? 'g'
					: color == md::FrontPanel::LedColor::Red ? 'r' : 'y';
			}
			else
				leds += panel.getStepLed(step) ? '#' : '.';
		}
		return leds;
	}

	// The RAM bytes that count steps while the sequencer plays: one more at each step (the LEDs give the
	// step rate), back to 0 at the end of the pattern. Main RAM, patch RAM and the ColdFire's SRAM are
	// sampled every 256 frames for _seconds of play.
	void scanRam(md::Hardware& _hardware, const char* _name, const double _seconds)
	{
		struct Region { uint32_t begin, size; };
		const Region regions[] = {{0x00200000, 0x00100000}, {0x00100000, 0x00100000}, {0x01000000, 0x00002000}};
		auto& uc = _hardware.getUC();
		struct Stats { uint8_t last = 0; uint16_t changes = 0, increments = 0, wraps = 0, other = 0; };
		std::vector<std::vector<Stats>> stats;
		for(const auto& region : regions)
		{
			stats.emplace_back(region.size);
			for(uint32_t i = 0; i < region.size; ++i)
				stats.back()[i].last = uc.read8(region.begin + i);
		}
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		const auto windows = static_cast<uint32_t>(_seconds * md::g_samplerate / 256);
		std::vector<synthLib::SMidiEvent> events;
		for(uint32_t window = 0; window < windows; ++window)
		{
			_hardware.processAudio(outputs, 256, 0);
			events.clear();
			_hardware.readMidiOut(events);
			for(size_t r = 0; r < std::size(regions); ++r)
			{
				for(uint32_t i = 0; i < regions[r].size; ++i)
				{
					auto& s = stats[r][i];
					const auto value = uc.read8(regions[r].begin + i);
					if(value == s.last)
						continue;
					++s.changes;
					if(value == static_cast<uint8_t>(s.last + 1))
						++s.increments;
					else if(value == 0)
						++s.wraps;
					else
						++s.other;
					s.last = value;
				}
			}
		}
		std::printf("%s RAM step counter candidates (%.1f s of play):\n", _name, _seconds);
		for(size_t r = 0; r < std::size(regions); ++r)
		{
			for(uint32_t i = 0; i < regions[r].size; ++i)
			{
				const auto& s = stats[r][i];
				if(s.changes < 8 || s.changes > 400 || s.other > 1 || s.increments < s.changes * 3 / 4)
					continue;
				std::printf("  $%08x: %u changes, %u +1, %u to 0, %u other, now %u\n", regions[r].begin + i,
					s.changes, s.increments, s.wraps, s.other, s.last);
			}
		}
	}

	void probe(const char* _variable, const md::MachineModel _model)
	{
		const auto* path = std::getenv(_variable);
		if(!path)
			return;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, _model);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
		{
			advance(*hardware, 64);
			require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
		}
		advance(*hardware, md::g_samplerate * 20);
		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		std::printf("%s stopped: step LEDs %s\n", name, stepLeds(*hardware).c_str());

		std::vector<synthLib::SMidiEvent> events;
		hardware->readMidiOut(events);
		panelTap(*hardware, md::PanelControl::Play);
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::string shown;
		uint32_t clocks = 0, starts = 0, stops = 0, positions = 0, other = 0;
		constexpr uint32_t windows = 4 * md::g_samplerate / 256;
		for(uint32_t window = 0; window < windows; ++window)
		{
			hardware->processAudio(outputs, 256, 0);
			events.clear();
			hardware->readMidiOut(events);
			for(const auto& event : events)
			{
				if(!event.sysex.empty())
					++other;
				else if(event.a == 0xf8)
					++clocks;
				else if(event.a == 0xfa || event.a == 0xfb)
					++starts;
				else if(event.a == 0xfc)
					++stops;
				else if(event.a == 0xf2)
					++positions;
				else
					++other;
			}
			const auto leds = stepLeds(*hardware);
			if(leds != shown)
			{
				shown = leds;
				std::printf("%s %7.1f ms  %s  clocks %u\n", name, 1000.0 * window * 256 / md::g_samplerate, leds.c_str(), clocks);
			}
		}
		std::printf("%s MIDI out in 4 s of play: %u clocks, %u starts, %u stops, %u song positions, %u other\n",
			name, clocks, starts, stops, positions, other);
		scanRam(*hardware, name, 4.0);
		panelTap(*hardware, md::PanelControl::Stop);
	}

	// The candidates' values while a pattern of 12 steps plays from PLAY: the step counter goes 0 to 11
	void trace(md::Hardware& _hardware, const char* _name, const std::vector<uint32_t>& _addresses, const double _seconds)
	{
		auto& uc = _hardware.getUC();
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::vector<int> shown(_addresses.size(), -1);
		std::vector<synthLib::SMidiEvent> events;
		std::printf("%s 12 steps from PLAY:", _name);
		for(const auto address : _addresses)
			std::printf(" $%08x", address);
		std::printf("\n");
		const auto windows = static_cast<uint32_t>(_seconds * md::g_samplerate / 256);
		for(uint32_t window = 0; window < windows; ++window)
		{
			bool changed = false;
			std::vector<int> values;
			for(const auto address : _addresses)
				values.push_back(uc.read8(address));
			changed = values != shown;
			if(changed)
			{
				shown = values;
				std::printf("%s %7.1f ms %s ", _name, 1000.0 * window * 256 / md::g_samplerate, stepLeds(_hardware).c_str());
				for(const auto value : values)
					std::printf(" %3d", value);
				std::printf("\n");
			}
			_hardware.processAudio(outputs, 256, 0);
			events.clear();
			_hardware.readMidiOut(events);
		}
	}

	// Bytes holding one value whenever the sequencer is stopped and another whenever it plays
	void scanRunFlag(md::Hardware& _hardware, const char* _name)
	{
		auto& uc = _hardware.getUC();
		const uint32_t begin = 0x00200000, size = 0x00100000;
		const uint32_t sramBegin = 0x01000000, sramSize = 0x2000;
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(size + sramSize);
			for(uint32_t i = 0; i < size; ++i)
				bytes[i] = uc.read8(begin + i);
			for(uint32_t i = 0; i < sramSize; ++i)
				bytes[size + i] = uc.read8(sramBegin + i);
			return bytes;
		};
		const auto address = [&](const uint32_t _index) { return _index < size ? begin + _index : sramBegin + _index - size; };
		std::vector<std::vector<uint8_t>> stopped, playing;
		stopped.push_back(snapshot());
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Play);
		for(int i = 0; i < 4; ++i)
		{
			advance(_hardware, md::g_samplerate / 3);
			playing.push_back(snapshot());
		}
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Play);
		advance(_hardware, md::g_samplerate / 2);
		playing.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		std::printf("%s run flag candidates (stopped value -> playing value):\n", _name);
		int shownCount = 0;
		for(uint32_t i = 0; i < size + sramSize && shownCount < 60; ++i)
		{
			const auto s = stopped.front()[i];
			const auto p = playing.front()[i];
			if(s == p)
				continue;
			bool consistent = true;
			for(const auto& snap : stopped)
				consistent &= snap[i] == s;
			for(const auto& snap : playing)
				consistent &= snap[i] == p;
			if(!consistent)
				continue;
			std::printf("  $%08x: %u -> %u\n", address(i), s, p);
			++shownCount;
		}
	}

	// The same, the sequencer started and stopped by MIDI START and STOP with MIDI clock (120 BPM), as when
	// the machine follows the host
	void scanRunFlagMidi(md::Hardware& _hardware, const std::function<void(uint8_t)>& _send, const char* _name)
	{
		auto& uc = _hardware.getUC();
		const uint32_t begin = 0x00200000, size = 0x00100000;
		const uint32_t sramBegin = 0x01000000, sramSize = 0x2000;
		constexpr uint32_t framesPerTick = md::g_samplerate * 60 / (120 * 24);
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(size + sramSize);
			for(uint32_t i = 0; i < size; ++i)
				bytes[i] = uc.read8(begin + i);
			for(uint32_t i = 0; i < sramSize; ++i)
				bytes[size + i] = uc.read8(sramBegin + i);
			return bytes;
		};
		const auto address = [&](const uint32_t _index) { return _index < size ? begin + _index : sramBegin + _index - size; };
		const auto clocks = [&](const int _ticks)
		{
			for(int tick = 0; tick < _ticks; ++tick)
			{
				_send(0xf8);
				advance(_hardware, framesPerTick);
			}
		};
		std::vector<std::vector<uint8_t>> stopped, playing;
		for(int round = 0; round < 2; ++round)
		{
			advance(_hardware, md::g_samplerate / 2);
			stopped.push_back(snapshot());
			_send(0xfa);
			clocks(24);
			playing.push_back(snapshot());
			clocks(24);
			playing.push_back(snapshot());
			_send(0xfc);
			advance(_hardware, md::g_samplerate / 2);
			stopped.push_back(snapshot());
		}
		std::printf("%s run flag candidates with MIDI transport (stopped value -> playing value):\n", _name);
		int shownCount = 0;
		for(uint32_t i = 0; i < size + sramSize && shownCount < 80; ++i)
		{
			const auto s = stopped.front()[i];
			const auto p = playing.front()[i];
			if(s == p)
				continue;
			bool consistent = true;
			for(const auto& snap : stopped)
				consistent &= snap[i] == s;
			for(const auto& snap : playing)
				consistent &= snap[i] == p;
			if(!consistent)
				continue;
			std::printf("  $%08x: %u -> %u\n", address(i), s, p);
			++shownCount;
		}
	}

	void syncMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		const auto sendBytes = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_bytes.front() == 0xf0)
				event.sysex.assign(_bytes.begin(), _bytes.end());
			else
				event.a = _bytes[0];
			require(hardware->sendMidi(event), "MIDI rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			sendBytes(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto slot = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Global), 0x72));
		require(slot.has_value(), "no Global status");
		const auto patched = sysex::withGlobalSync(model, exchange(sysex::globalRequest(model, slot->value), 0x50), {true, true});
		require(patched.has_value(), "Global not patched");
		sendBytes(*patched);
		advance(*hardware, md::g_samplerate);
		scanRunFlagMidi(*hardware, [&](const uint8_t _byte) { sendBytes({_byte}); }, "MD");

		// The step and the sequencer's tick as Hardware::readSequencerPosition reads them, from MIDI START
		constexpr uint32_t framesPerTick = md::g_samplerate * 60 / (120 * 24);
		sendBytes({0xfa});
		std::string line;
		for(int tick = 0; tick < 36; ++tick)
		{
			sendBytes({0xf8});
			advance(*hardware, framesPerTick);
			const auto position = hardware->readSequencerPosition();
			line += " " + std::to_string(hardware->getUC().read8(0x00261aa7)) + "/" + std::to_string(hardware->getUC().read8(0x01001f33))
				+ (position && position->playing ? "p" : "s");
		}
		sendBytes({0xfc});
		for(int i = 0; i < 12; ++i)
		{
			advance(*hardware, md::g_samplerate / 24);
			const auto position = hardware->readSequencerPosition();
			line += " |" + std::to_string(hardware->getUC().read8(0x00261aa7)) + "/" + std::to_string(hardware->getUC().read8(0x01001f33))
				+ (position && position->playing ? "p" : "s");
		}
		std::printf("MD MIDI START, 36 ticks, STOP (step/tick, p playing, s stopped):%s\n", line.c_str());
	}

	void syncMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path)
			return;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		md::test::Monomachine machine(rom, path);
		machine.follow();
		scanRunFlagMidi(machine.hardware(), [&](const uint8_t _byte) { machine.send({_byte}); }, "MM");
	}

	void confirmMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_request.begin(), _request.end());
			require(hardware->sendMidi(event), "SysEx rejected");
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto status = sysex::parseStatusResponse(md::MachineModel::Machinedrum,
			exchange(sysex::statusRequest(md::MachineModel::Machinedrum, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		auto editor = sysex::MdPatternEditor::fromDump(exchange(sysex::patternRequest(md::MachineModel::Machinedrum, status->value), 0x67));
		require(editor && editor->setLength(12), "pattern length refused");
		synthLib::SMidiEvent write(synthLib::MidiEventSource::Host);
		const auto dump = editor->toDump();
		write.sysex.assign(dump.begin(), dump.end());
		require(hardware->sendMidi(write), "pattern write rejected");
		advance(*hardware, md::g_samplerate);
		trace(*hardware, "MD stopped", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 1.0);
		panelTap(*hardware, md::PanelControl::Play);
		trace(*hardware, "MD", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 0.5);
		panelTap(*hardware, md::PanelControl::Stop);
		trace(*hardware, "MD after STOP", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 1.0);
		advance(*hardware, md::g_samplerate);
		scanRunFlag(*hardware, "MD");
	}

	void confirmMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		md::test::Monomachine machine(rom, path);
		const auto pattern = machine.status(sysex::StatusParameter::Pattern);
		auto editor = sysex::MmPatternEditor::fromDump(machine.readPattern(pattern));
		require(editor && editor->setLength(12), "pattern length refused");
		machine.writePattern(editor->toDump());
		trace(machine.hardware(), "MM stopped", {0x002bc287, 0x002bc29b, 0x002bc29f}, 1.0);
		panelTap(machine.hardware(), md::PanelControl::Play);
		trace(machine.hardware(), "MM", {0x002bc287, 0x002bc29b, 0x002bc29f}, 0.5);
		panelTap(machine.hardware(), md::PanelControl::Stop);
		trace(machine.hardware(), "MM after STOP", {0x002bc287, 0x002bc29b, 0x002bc29f}, 1.0);
		advance(machine.hardware(), md::g_samplerate);
		scanRunFlag(machine.hardware(), "MM");
	}
}

int main(const int _argc, const char* const* _argv)
{
	try
	{
		if(_argc > 1 && std::string(_argv[1]) == "--confirm")
		{
			confirmMachinedrum();
			confirmMonomachine();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--sync")
		{
			syncMachinedrum();
			syncMonomachine();
			return 0;
		}
		probe("GEARMULATOR_MD_FIRMWARE_BIN", md::MachineModel::Machinedrum);
		probe("GEARMULATOR_MM_FIRMWARE_BIN", md::MachineModel::Monomachine);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdPlayheadProbe: FAIL %s\n", _error.what());
		return 1;
	}
}
