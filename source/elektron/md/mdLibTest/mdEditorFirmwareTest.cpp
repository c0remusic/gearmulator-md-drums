// The editor's firmware assumptions, checked against the real Machinedrum and
// Monomachine: machine assignment ($5B) and what it does to a track, that a Kit
// dump request returns the stored Kit, the Machinedrum pattern dump ($67) as
// MdPatternEditor reads it (with trigs and a lock placed on the front panel,
// unsaved), and a pattern written back to its slot, stopped and playing. Every
// message comes from the codec the editor uses (mdsysexautomation.h). The
// silence around a write during playback is printed, not checked.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN;
// a model without its firmware is skipped, 77 when neither ran.

#include "mdLib/mdhardware.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	namespace sysex = md::automation::sysex;
	using Bytes = std::vector<uint8_t>;

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

	void boot(md::Hardware& _hardware)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(!_hardware.isFirmwareMidiReady() || !_hardware.isAudioReady())
		{
			advance(_hardware, 64);
			require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
		}
		// MIDI-ready comes before the startup tasks settle (see mmSysexExportFirmwareTest)
		advance(_hardware, md::g_samplerate * 20);
	}

	void send(md::Hardware& _hardware, const Bytes& _message)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex.assign(_message.begin(), _message.end());
		require(_hardware.sendMidi(event), "SysEx rejected by the MIDI input");
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

	// Turns _encoder by _steps detents while _control is held
	void holdAndTurn(md::Hardware& _hardware, const md::PanelControl _control, const md::PanelEncoder _encoder,
		const int _steps)
	{
		const auto packet = md::panelPacket(_hardware.getModel(), _control);
		const auto command = md::panelEncoderCommand(_hardware.getModel(), _encoder);
		require(packet && command, "unknown panel control or encoder");
		require(_hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
		advance(_hardware, 2048);
		for(int step = 0; step < std::abs(_steps); ++step)
		{
			require(_hardware.trySendPanelEvent(*command, _steps > 0 ? 0x01 : 0xff), "encoder turn rejected");
			advance(_hardware, 1024);
		}
		require(_hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
		advance(_hardware, 6400);
	}

	// Peak level of consecutive 256-frame windows of the main output
	std::vector<float> renderPeaks(md::Hardware& _hardware, const size_t _windows)
	{
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::vector<float> peaks;
		for(size_t window = 0; window < _windows; ++window)
		{
			_hardware.processAudio(outputs, 256, 0);
			float peak = 0;
			for(const auto& channel : samples)
			{
				for(const auto sample : channel)
					peak = std::max(peak, std::abs(sample));
			}
			peaks.push_back(peak);
		}
		return peaks;
	}

	// Longest run of silent windows, in milliseconds
	double longestSilence(const std::vector<float>& _peaks)
	{
		size_t longest = 0;
		size_t run = 0;
		for(const auto peak : _peaks)
		{
			run = peak < 1e-5f ? run + 1 : 0;
			longest = std::max(longest, run);
		}
		return static_cast<double>(longest) * 256.0 * 1000.0 / md::g_samplerate;
	}

	// Sends _request and returns the first SysEx the firmware answers with
	// _command, empty after three seconds of machine time.
	Bytes exchange(md::Hardware& _hardware, const Bytes& _request, const uint8_t _command)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hardware.readMidiOut(events);
		send(_hardware, _request);
		for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
		{
			advance(_hardware, 64);
			events.clear();
			_hardware.readMidiOut(events);
			for(const auto& event : events)
			{
				if(event.sysex.size() > 9 && event.sysex[6] == _command)
					return Bytes(event.sysex.begin(), event.sysex.end());
			}
		}
		return {};
	}

	uint8_t status(md::Hardware& _hardware, const sysex::StatusParameter _parameter)
	{
		const auto reply = exchange(_hardware, sysex::statusRequest(_hardware.getModel(), _parameter), 0x72);
		const auto parsed = sysex::parseStatusResponse(_hardware.getModel(), reply);
		require(parsed && parsed->parameter == _parameter, "no status response");
		return parsed->value;
	}

	sysex::KitDump readKit(md::Hardware& _hardware, const uint8_t _slot)
	{
		const auto reply = exchange(_hardware, sysex::kitRequest(_hardware.getModel(), _slot), 0x52);
		auto kit = sysex::parseKitDump(_hardware.getModel(), reply);
		require(kit.has_value(), "no Kit dump for slot " + std::to_string(_slot));
		return *kit;
	}

	std::vector<uint8_t> trackValues(const sysex::KitDump& _kit, const uint8_t _track)
	{
		std::vector<uint8_t> values;
		for(const auto& change : _kit.parameters)
		{
			if(change.track == _track)
				values.push_back(change.value);
		}
		return values;
	}

	std::string machineName(const md::MachineModel _model, const uint16_t _id)
	{
		const auto* machine = md::machines::find(_model, _id);
		return machine ? std::string(machine->name) : "id " + std::to_string(_id);
	}

	// $5B on a track, with the first of _candidates the track does not hold yet:
	// a Kit request before and after saving the Kit shows whether the firmware
	// took the machine and whether a request returns the live or the stored Kit;
	// the track's values show what the assignment did to them.
	void checkAssignment(md::Hardware& _hardware, const uint8_t _track, const std::vector<uint16_t>& _candidates)
	{
		const auto model = _hardware.getModel();
		const auto name = model == md::MachineModel::Monomachine ? "MM" : "MD";
		const auto slot = status(_hardware, sysex::StatusParameter::Kit);
		const auto before = readKit(_hardware, slot);
		require(_track < before.machines.size(), "Kit dump without machines");
		const auto previous = before.machines[_track];
		const auto candidate = std::find_if(_candidates.begin(), _candidates.end(),
			[previous](const uint16_t _id) { return _id != previous; });
		require(candidate != _candidates.end(), "no test machine differs from the track's");
		const auto machine = *candidate;

		const auto message = sysex::assignMachine(model, _track, machine);
		require(message.has_value(), "codec refused the assignment");
		send(_hardware, *message);
		advance(_hardware, md::g_samplerate);

		const auto live = readKit(_hardware, slot);
		send(_hardware, sysex::kitSave(model, slot));
		advance(_hardware, md::g_samplerate);
		const auto saved = readKit(_hardware, slot);

		const auto valuesBefore = trackValues(before, _track);
		const auto valuesAfter = trackValues(saved, _track);
		size_t changed = 0;
		std::string indices;
		for(size_t index = 0; index < std::min(valuesBefore.size(), valuesAfter.size()); ++index)
		{
			if(valuesBefore[index] == valuesAfter[index])
				continue;
			++changed;
			indices += " " + std::to_string(index + 1);
		}

		std::printf("%s $5B kit %u track %u: %s -> %s; request before save shows %s, after save %s;"
			" %zu of %zu track values changed:%s\n",
			name, slot + 1, _track + 1, machineName(model, previous).c_str(), machineName(model, machine).c_str(),
			machineName(model, live.machines[_track]).c_str(), machineName(model, saved.machines[_track]).c_str(),
			changed, valuesAfter.size(), indices.c_str());
		require(saved.machines[_track] == machine, "the saved Kit does not hold the assigned machine");
		// The controller counts on this: a Kit request returns the stored Kit, not the live one
		require(live.machines[_track] == previous, "a Kit request returned the live Kit");
	}

	std::string describeTrigs(const sysex::PatternDump& _pattern)
	{
		std::string text;
		for(uint8_t track = 0; track < 16; ++track)
		{
			if(!_pattern.trigs[track])
				continue;
			text += " T" + std::to_string(track + 1) + ":";
			for(uint8_t step = 0; step < 32; ++step)
			{
				if(_pattern.hasTrig(track, step))
					text += " " + std::to_string(step + 1);
			}
		}
		return text.empty() ? " none" : text;
	}

	Bytes readPattern(md::Hardware& _hardware, const uint8_t _slot)
	{
		const auto reply = exchange(_hardware, sysex::patternRequest(md::MachineModel::Machinedrum, _slot), 0x67);
		require(!reply.empty(), "no pattern dump for slot " + std::to_string(_slot));
		return reply;
	}

	std::string differences(const Bytes& _a, const Bytes& _b)
	{
		if(_a.size() != _b.size())
			return "sizes " + std::to_string(_a.size()) + " and " + std::to_string(_b.size());
		std::string text;
		size_t count = 0;
		for(size_t index = 0; index < _a.size(); ++index)
		{
			if(_a[index] == _b[index])
				continue;
			if(++count <= 8)
			{
				char offset[16];
				std::snprintf(offset, sizeof(offset), " 0x%zx", index);
				text += offset;
			}
		}
		return std::to_string(count) + " bytes differ, first at" + text;
	}

	// The pattern dump against MdPatternEditor, trigs placed on the front panel,
	// and a pattern written back to its slot.
	void checkPattern(md::Hardware& _hardware)
	{
		const auto slot = status(_hardware, sysex::StatusParameter::Pattern);
		const auto first = readPattern(_hardware, slot);
		const auto parsed = sysex::parseMdPatternDump(first);
		require(parsed.has_value() && parsed->slot == slot, "pattern dump not decoded");
		const auto editor = sysex::MdPatternEditor::fromDump(first);
		require(editor.has_value(), "pattern dump not editable");
		size_t lockRows = 0;
		for(const auto mask : parsed->lockMasks)
			lockRows += std::bitset<32>(mask).count();
		std::printf("MD $67 pattern %u: %zu bytes, length %u, %zu lock rows in use, trigs%s; re-encoded %s\n",
			slot, first.size(), parsed->length, lockRows, describeTrigs(*parsed).c_str(),
			editor->toDump() == first ? "identical" : differences(editor->toDump(), first).c_str());
		require(editor->toDump() == first, "MdPatternEditor does not re-encode the firmware's dump byte for byte");

		// Grid recording, not saved: two tracks down from track 1, then steps 1 and 5 toggled
		panelTap(_hardware, md::PanelControl::Record);
		panelTap(_hardware, md::PanelControl::Down);
		panelTap(_hardware, md::PanelControl::Down);
		panelTap(_hardware, md::PanelControl::Trigger1);
		panelTap(_hardware, md::PanelControl::Trigger5);
		// A lock from the panel: step 5 held, DATA ENTRY B (second parameter of the page) turned
		holdAndTurn(_hardware, md::PanelControl::Trigger5, md::PanelEncoder::DataEntryB, 10);
		panelTap(_hardware, md::PanelControl::Record);
		const auto recorded = readPattern(_hardware, slot);
		const auto recordedPattern = sysex::parseMdPatternDump(recorded);
		require(recordedPattern.has_value(), "pattern dump after grid recording not decoded");
		std::string changedTracks;
		for(uint8_t track = 0; track < 16; ++track)
		{
			const auto toggled = recordedPattern->trigs[track] ^ parsed->trigs[track];
			if(!toggled)
				continue;
			changedTracks += " T" + std::to_string(track + 1) + ":";
			for(uint8_t step = 0; step < 32; ++step)
			{
				if((toggled >> step) & 1u)
					changedTracks += " " + std::to_string(step + 1);
			}
		}
		std::string locks;
		for(uint8_t track = 0; track < 16; ++track)
		{
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
			{
				for(uint8_t step = 0; step < 32; ++step)
				{
					if(const auto value = recordedPattern->lock(track, parameter, step))
						locks += " T" + std::to_string(track + 1) + " P" + std::to_string(parameter + 1) + " step "
							+ std::to_string(step + 1) + " = " + std::to_string(*value) + ";";
				}
			}
		}
		std::printf("MD $67 after grid recording (RECORD, DOWN twice, TRIG 1 and 5, TRIG 5 held with DATA ENTRY B"
			" turned 10 up), trigs toggled:%s; locks:%s\n",
			changedTracks.empty() ? " none" : changedTracks.c_str(), locks.empty() ? " none" : locks.c_str());
		// Unsaved edits of the active track (track 1) show in the dump, where the decoder expects them
		require(recordedPattern->trigs[0] == (parsed->trigs[0] ^ 0x11u), "grid-recorded trigs not at track 1, steps 1 and 5");
		for(uint8_t track = 1; track < 16; ++track)
			require(recordedPattern->trigs[track] == parsed->trigs[track], "grid recording changed another track");
		require(recordedPattern->lock(0, 1, 4).has_value(), "panel lock not at track 1, parameter 2, step 5");

		// Written back: a trig on track 6 step 3 with a lock on its second parameter
		auto edited = sysex::MdPatternEditor::fromDump(recorded);
		require(edited.has_value(), "recorded pattern not editable");
		require(edited->setTrig(5, 2, true) && edited->setLock(5, 1, 2, 77), "pattern edit refused");
		const auto written = edited->toDump();
		send(_hardware, written);
		advance(_hardware, md::g_samplerate);
		const auto readBack = readPattern(_hardware, slot);
		const auto readBackPattern = sysex::parseMdPatternDump(readBack);
		std::printf("MD $67 written to pattern %u: read back %s; trigs%s; lock T6 P2 step 3 = %d\n",
			slot, readBack == written ? "as sent" : differences(readBack, written).c_str(),
			readBackPattern ? describeTrigs(*readBackPattern).c_str() : " (not decoded)",
			readBackPattern && readBackPattern->lock(5, 1, 2) ? int(*readBackPattern->lock(5, 1, 2)) : -1);
		require(readBack == written, "the firmware did not keep the written pattern as sent");

		// The same kind of write while the sequencer plays: silence before and during
		auto playing = sysex::MdPatternEditor::fromDump(readBack);
		require(playing.has_value() && playing->setLock(5, 1, 2, 90), "second pattern edit refused");
		panelTap(_hardware, md::PanelControl::Play);
		const auto before = renderPeaks(_hardware, 516);
		send(_hardware, playing->toDump());
		const auto during = renderPeaks(_hardware, 516);
		panelTap(_hardware, md::PanelControl::Stop);
		const auto playedBack = readPattern(_hardware, slot);
		std::printf("MD $67 written while playing: longest silence %.0f ms in the 3 s before, %.0f ms in the 3 s after"
			" the write; read back %s\n", longestSilence(before), longestSilence(during),
			playedBack == playing->toDump() ? "as sent" : differences(playedBack, playing->toDump()).c_str());
	}

	std::unique_ptr<md::Hardware> start(const char* const _variable, const md::MachineModel _model)
	{
		const auto* path = std::getenv(_variable);
		if(!path || !*path)
		{
			std::printf("mdEditorFirmwareTest: SKIP %s (%s not set)\n",
				_model == md::MachineModel::Monomachine ? "MM" : "MD", _variable);
			return {};
		}
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		require(md::RomLoader::isRomForModel(rom, _model), std::string(path) + " is not the expected firmware");
		auto hardware = std::make_unique<md::Hardware>(rom, path, _model);
		boot(*hardware);
		return hardware;
	}

	bool runMachinedrum()
	{
		const auto hardware = start("GEARMULATOR_MD_FIRMWARE_BIN", md::MachineModel::Machinedrum);
		if(!hardware)
			return false;
		const auto kit = readKit(*hardware, status(*hardware, sysex::StatusParameter::Kit));
		std::string machines;
		for(size_t track = 0; track < kit.machines.size(); ++track)
			machines += " T" + std::to_string(track + 1) + " " + machineName(md::MachineModel::Machinedrum, kit.machines[track]);
		std::printf("MD Kit %u \"%s\", pattern %u, machines:%s\n", kit.slot + 1, kit.name.c_str(),
			status(*hardware, sysex::StatusParameter::Pattern), machines.c_str());
		checkAssignment(*hardware, 2, {17, 33});     // TRX-SD or EFM-SD: the plain machine table
		checkAssignment(*hardware, 3, {128, 129});   // ROM-01 or ROM-02: sent with the UW flag
		checkPattern(*hardware);
		std::printf("mdEditorFirmwareTest: MD PASS\n");
		return true;
	}

	bool runMonomachine()
	{
		const auto hardware = start("GEARMULATOR_MM_FIRMWARE_BIN", md::MachineModel::Monomachine);
		if(!hardware)
			return false;
		const auto kit = readKit(*hardware, status(*hardware, sysex::StatusParameter::Kit));
		std::printf("MM Kit %u \"%s\", pattern %u\n", kit.slot + 1, kit.name.c_str(),
			status(*hardware, sysex::StatusParameter::Pattern));
		checkAssignment(*hardware, 1, {32, 33});     // DPRO-DDRW or DPRO-DENS, sent without page initialisation
		std::printf("mdEditorFirmwareTest: MM PASS\n");
		return true;
	}
}

int main()
{
	try
	{
		const bool ranMachinedrum = runMachinedrum();
		const bool ranMonomachine = runMonomachine();
		return ranMachinedrum || ranMonomachine ? 0 : 77;
	}
	catch(const std::exception& error)
	{
		std::printf("mdEditorFirmwareTest: FAIL %s\n", error.what());
		return 1;
	}
}
