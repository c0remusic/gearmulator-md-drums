// The editor's firmware assumptions, checked against the real Machinedrum and
// Monomachine: machine assignment ($5B) and what it does to a track, that a Kit
// dump request returns the stored Kit, the Machinedrum master effects in the
// Kit dump and their changes ($5D to $60), the track routing in the Global dump
// and its change ($5C), the Machinedrum pattern dump ($67) as
// MdPatternEditor reads it (with trigs and a lock placed on the front panel,
// unsaved), and a pattern written back to its slot, stopped and playing; the
// sequencer's step and running state as JOUER reads them in the firmware's RAM.
// Every message comes from the codec the editor uses (mdsysexautomation.h). The
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
#include <functional>
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
	// the track's values show what the assignment did to them. The live Kit in
	// the machine's RAM shows the machine at once, and once saved equals the dump.
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
		// The machine's RAM shows the new machine at once (Hardware::readLiveKit)
		const auto liveKit = _hardware.readLiveKit();
		require(liveKit.has_value(), "no live Kit for this firmware");
		require(liveKit->machines[_track] == machine, "the live Kit does not show the assigned machine");
		send(_hardware, sysex::kitSave(model, slot));
		advance(_hardware, md::g_samplerate);
		const auto saved = readKit(_hardware, slot);
		// Saved, the Kit is the live one: every value and machine of its dump is where readLiveKit reads it
		const auto liveSaved = _hardware.readLiveKit();
		for(const auto& change : saved.parameters)
		{
			require(liveSaved->value(change.track, change.page, change.index) == change.value,
				"the live Kit differs from the saved Kit at track " + std::to_string(change.track + 1) + " page "
				+ std::to_string(change.page) + " index " + std::to_string(change.index));
		}
		for(size_t track = 0; track < saved.machines.size(); ++track)
			require(liveSaved->machines[track] == saved.machines[track], "the live Kit's machines differ from the saved Kit's");
		if(model == md::MachineModel::Machinedrum)
		{
			require(saved.lfos.has_value() && saved.masterEffects.has_value(), "the Kit dump holds no LFOs or master effects");
			for(size_t track = 0; track < saved.lfos->size(); ++track)
				require(liveSaved->lfos[track] == (*saved.lfos)[track], "the live Kit's LFO of track " + std::to_string(track + 1) + " differs from the saved Kit's");
			require(sysex::masterEffectsFromKit(liveSaved->masterEffects.data()) == *saved.masterEffects,
				"the live Kit's master effects differ from the saved Kit's");
		}

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

	// Peak of the main output while the sequencer plays _windows of 256 frames from PLAY
	float playPeak(md::Hardware& _hardware, const size_t _windows)
	{
		panelTap(_hardware, md::PanelControl::Play);
		const auto peaks = renderPeaks(_hardware, _windows);
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		return *std::max_element(peaks.begin(), peaks.end());
	}

	// A pattern over 32 steps (48, a trig and a lock on step 40 of track 1), written and read back as sent,
	// its steps 33 to 64 read; then 24 steps, step 40 kept past the length. The firmware sends every
	// pattern in the long form (A01 of 32 steps too); sent in the short form, a pattern loses its steps 33
	// to 64.
	void checkLongPattern(md::Hardware& _hardware)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		const auto slot = status(_hardware, sysex::StatusParameter::Pattern);
		const auto original = exchange(_hardware, sysex::patternRequest(model, slot), 0x67);
		require(original.size() == 0x1522 && sysex::parseMdPatternDump(original)->steps == 64,
			"the firmware sent a pattern in the short form");
		auto editor = sysex::MdPatternEditor::fromDump(original);
		require(editor && editor->setLength(48), "the codec did not make the pattern long");
		require(editor->setTrig(0, 39, true) && editor->setLock(0, 2, 39, 99), "the codec did not set step 40");
		const auto sent = editor->toDump();
		require(sent.size() == 0x1522, "the long form is not 0x1522 bytes");
		send(_hardware, sent);
		advance(_hardware, md::g_samplerate);
		const auto back = exchange(_hardware, sysex::patternRequest(model, slot), 0x67);
		const auto parsed = sysex::parseMdPatternDump(back);
		std::printf("MD pattern %u made 48 steps: read back %zu bytes, %s\n", slot + 1, back.size(),
			back == sent ? "as sent" : differences(back, sent).c_str());
		require(parsed && parsed->steps == 64 && parsed->length == 48 && parsed->hasTrig(0, 39) && parsed->lock(0, 2, 39) == uint8_t{99},
			"the long pattern did not come back with its step 40");
		require(back == sent, "the long pattern came back changed");

		auto shorter = sysex::MdPatternEditor::fromDump(back);
		require(shorter && shorter->setLength(24), "the codec did not shorten the pattern");
		const auto shortSent = shorter->toDump();
		send(_hardware, shortSent);
		advance(_hardware, md::g_samplerate);
		const auto shortDump = exchange(_hardware, sysex::patternRequest(model, slot), 0x67);
		const auto shortBack = sysex::parseMdPatternDump(shortDump);
		std::printf("MD pattern %u made 24 steps: sent %zu bytes, read back %zu bytes, length %d, step 40 %s\n", slot + 1,
			shortSent.size(), shortDump.size(), shortBack ? int(shortBack->length) : -1,
			shortBack && shortBack->hasTrig(0, 39) ? "kept" : "gone");
		require(shortBack && shortBack->length == 24 && shortBack->hasTrig(0, 39) && shortDump == shortSent,
			"24 steps did not come back as sent, step 40 kept past the length");
		// The pattern as it was, for the checks after this one
		send(_hardware, original);
		advance(_hardware, md::g_samplerate);
	}

	void sendControlChange(md::Hardware& _hardware, const md::automation::ControlChange& _change)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.a = _change[0];
		event.b = _change[1];
		event.c = _change[2];
		require(_hardware.sendMidi(event), "CC rejected by the MIDI input");
	}

	// A copy as Controller::copyPattern makes it: the selected pattern's dump with another slot's number ($67),
	// sent. The firmware stores it in that slot, which comes back as sent, and the selected pattern stays.
	// Track 1 playing on the beats, silenced by a VOL of 0 sent by CC, stays silent across the copy: unlike a
	// write to the selected pattern (checkLiveKitAcrossPatternWrite), a copy to another slot does not reload
	// the Kit, and needs no SAVE KIT. Both patterns as they were, for the checks after this one.
	void checkPatternCopy(md::Hardware& _hardware)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		const auto selected = status(_hardware, sysex::StatusParameter::Pattern);
		const auto destination = static_cast<uint8_t>((selected + 16) % 128);	// the next bank
		const auto original = readPattern(_hardware, selected);
		const auto overwritten = readPattern(_hardware, destination);
		const auto globalSlot = status(_hardware, sysex::StatusParameter::Global);
		const auto global = sysex::parseGlobalDump(model, exchange(_hardware, sysex::globalRequest(model, globalSlot), 0x50));
		require(global.has_value(), "no Global dump");

		auto beats = sysex::MdPatternEditor::fromDump(original);
		require(beats.has_value(), "pattern not editable");
		beats->clear();
		for(uint8_t step = 0; step < 16; step += 4)
			require(beats->setTrig(0, step, true), "trig refused");
		send(_hardware, beats->toDump());
		advance(_hardware, md::g_samplerate);
		constexpr size_t twoSeconds = 344;
		const auto loud = playPeak(_hardware, twoSeconds);
		const auto volume = md::automation::encodeParameterChange(model,
			{md::automation::machinedrum::Routing, 0, 1, 0}, global->baseChannel);
		require(volume.has_value(), "no CC for track 1 VOL");
		sendControlChange(_hardware, *volume);
		advance(_hardware, md::g_samplerate / 4);
		const auto muted = playPeak(_hardware, twoSeconds);

		auto copy = sysex::MdPatternEditor::fromDump(readPattern(_hardware, selected));
		require(copy && copy->setSlot(destination), "the codec did not number the copy");
		const auto sent = copy->toDump();
		send(_hardware, sent);
		advance(_hardware, md::g_samplerate);
		const auto back = readPattern(_hardware, destination);
		const auto stillSelected = status(_hardware, sysex::StatusParameter::Pattern);
		const auto afterCopy = playPeak(_hardware, twoSeconds);
		std::printf("MD pattern %u copied to %u: read back %s; selected after the copy: %u; track 1 peak %.3f, VOL 0 by CC"
			" %.4f, after the copy %.4f\n", selected + 1, destination + 1, back == sent ? "as sent" : differences(back, sent).c_str(),
			stillSelected + 1, loud, muted, afterCopy);
		require(back == sent, "the copy did not come back as sent");
		require(stillSelected == selected, "the copy changed the selected pattern");
		require(loud > 0.01f && muted < loud / 20, "track 1 VOL by CC does not silence it");
		require(afterCopy < loud / 20, "the copy reloaded the Kit: Controller::copyPattern needs a SAVE KIT");

		// As they were: the selected pattern written back reloads its Kit as stored, its VOL with it
		send(_hardware, overwritten);
		send(_hardware, original);
		advance(_hardware, md::g_samplerate);
	}

	// What a pattern written to its slot does to the live Kit, track 1 playing alone on the beats: a VOL of 0
	// sent by CC (as the editor's SON sends it) silences it until the pattern is written, stopped or playing;
	// the Machinedrum then reloads the pattern's Kit as stored, as selecting the pattern does. With the Kit
	// saved first (SAVE KIT, $59), the write keeps the change: Controller::sendPattern saves it.
	void checkLiveKitAcrossPatternWrite(md::Hardware& _hardware)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		const auto pattern = status(_hardware, sysex::StatusParameter::Pattern);
		const auto kit = status(_hardware, sysex::StatusParameter::Kit);
		const auto globalSlot = status(_hardware, sysex::StatusParameter::Global);
		const auto global = sysex::parseGlobalDump(model, exchange(_hardware, sysex::globalRequest(model, globalSlot), 0x50));
		require(global.has_value(), "no Global dump");
		auto editor = sysex::MdPatternEditor::fromDump(readPattern(_hardware, pattern));
		require(editor.has_value(), "pattern not editable");
		editor->clear();
		for(uint8_t step = 0; step < 16; step += 4)
			require(editor->setTrig(0, step, true), "trig refused");
		send(_hardware, editor->toDump());
		advance(_hardware, md::g_samplerate);
		constexpr size_t twoSeconds = 344;
		const auto loud = playPeak(_hardware, twoSeconds);

		const auto volume = md::automation::encodeParameterChange(model,
			{md::automation::machinedrum::Routing, 0, 1, 0}, global->baseChannel);
		require(volume.has_value(), "no CC for track 1 VOL");
		const auto mute = [&]
		{
			sendControlChange(_hardware, *volume);
			advance(_hardware, md::g_samplerate / 4);
		};
		mute();
		const auto muted = playPeak(_hardware, twoSeconds);

		// A trig added and the pattern written, as JOUER writes it, stopped
		require(editor->setTrig(0, 2, true), "trig refused");
		send(_hardware, editor->toDump());
		advance(_hardware, md::g_samplerate);
		const auto writtenStopped = playPeak(_hardware, twoSeconds);

		// The same while playing: the peak after the write
		mute();
		require(editor->setTrig(0, 6, true), "trig refused");
		panelTap(_hardware, md::PanelControl::Play);
		const auto beforeWrite = renderPeaks(_hardware, twoSeconds);
		send(_hardware, editor->toDump());
		const auto afterWrite = renderPeaks(_hardware, twoSeconds * 2);
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		const auto writtenPlaying = *std::max_element(afterWrite.begin() + twoSeconds, afterWrite.end());
		const auto mutedPlaying = *std::max_element(beforeWrite.begin(), beforeWrite.end());

		// The Kit saved first
		mute();
		send(_hardware, sysex::kitSave(model, kit));
		advance(_hardware, md::g_samplerate / 2);
		require(editor->setTrig(0, 10, true), "trig refused");
		send(_hardware, editor->toDump());
		advance(_hardware, md::g_samplerate);
		const auto savedThenWritten = playPeak(_hardware, twoSeconds);

		std::printf("MD live Kit across a pattern write (pattern %u, Kit %u), track 1 peak: %.3f; VOL 0 by CC: %.4f;"
			" pattern written stopped: %.3f; VOL 0 again, playing %.4f, written while playing: %.3f;"
			" VOL 0, Kit saved, pattern written: %.4f\n", pattern + 1, kit + 1, loud, muted, writtenStopped, mutedPlaying,
			writtenPlaying, savedThenWritten);
		require(loud > 0.01f && muted < loud / 20 && mutedPlaying < loud / 20, "track 1 VOL by CC does not silence it");
		// The firmware rule the editor works around
		require(writtenStopped > loud / 2 && writtenPlaying > loud / 2,
			"a pattern write kept the unsaved Kit: Controller::sendPattern's SAVE KIT may no longer be needed");
		require(savedThenWritten < loud / 20, "the saved Kit did not survive the pattern write");
	}

	// SET LFO PARAM ($62): each field of track 3's LFO shows in the live Kit at once, and the saved Kit holds it
	void checkLfo(md::Hardware& _hardware)
	{
		constexpr uint8_t track = 2;
		const md::LfoSettings wanted{5, 17, md::LfoSettings::Ramp, md::LfoSettings::Exponential, md::LfoSettings::Hold};
		const uint8_t fields[] = {wanted.track, wanted.parameter, wanted.shape1, wanted.shape2, wanted.update};
		for(uint8_t field = 0; field < 5; ++field)
		{
			const auto message = sysex::lfoChange(track, field, fields[field]);
			require(message.has_value(), "codec refused an LFO change");
			send(_hardware, *message);
		}
		require(!sysex::lfoChange(track, 2, md::LfoSettings::ShapeCount) && !sysex::lfoChange(track, 5, 0),
			"codec took a shape or a field out of range");
		advance(_hardware, md::g_samplerate / 2);
		const auto live = _hardware.readLiveKit();
		require(live && live->lfos[track] == wanted, "the live Kit does not show the LFO changed by $62");
		const auto slot = status(_hardware, sysex::StatusParameter::Kit);
		send(_hardware, sysex::kitSave(md::MachineModel::Machinedrum, slot));
		advance(_hardware, md::g_samplerate);
		const auto saved = readKit(_hardware, slot);
		require(saved.lfos && (*saved.lfos)[track] == wanted, "the saved Kit does not hold the LFO changed by $62");
		std::printf("MD $62: track %u LFO -> track %u parameter %u, shapes %u and %u, update %u, live and saved\n",
			track + 1, wanted.track + 1, wanted.parameter, wanted.shape1, wanted.shape2, wanted.update);
	}

	std::string describeEffects(const sysex::MasterEffects& _effects)
	{
		static constexpr const char* names[] = {"echo", "reverb", "EQ", "dynamix"};
		std::string text;
		for(uint8_t effect = 0; effect < sysex::MasterEffectCount; ++effect)
		{
			text += std::string(" ") + names[effect] + ":";
			for(const auto value : _effects[effect])
				text += " " + std::to_string(value);
		}
		return text;
	}

	// The master effects in the Kit dump, and $5D to $60: parameter n of the n-th
	// effect set, the Kit saved and read again, shows where each effect sits.
	void checkMasterEffects(md::Hardware& _hardware)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		const auto slot = status(_hardware, sysex::StatusParameter::Kit);
		const auto beforeDump = exchange(_hardware, sysex::kitRequest(model, slot), 0x52);
		const auto before = sysex::parseKitDump(model, beforeDump);
		require(before && before->masterEffects, "no master effects in the Kit dump");
		std::printf("MD master effects of Kit %u:%s\n", slot + 1, describeEffects(*before->masterEffects).c_str());

		auto expected = *before->masterEffects;
		for(uint8_t effect = 0; effect < sysex::MasterEffectCount; ++effect)
		{
			auto& value = expected[effect][effect];
			value = static_cast<uint8_t>((value + 37 + effect * 11) & 0x7f);
			const auto message = sysex::masterEffectChange(static_cast<sysex::MasterEffect>(effect), effect, value);
			require(message.has_value(), "codec refused a master effect change");
			send(_hardware, *message);
			advance(_hardware, md::g_samplerate / 10);
		}
		advance(_hardware, md::g_samplerate / 2);
		// The live Kit shows them before any save
		const auto live = _hardware.readLiveKit();
		require(live && sysex::masterEffectsFromKit(live->masterEffects.data()) == expected,
			"the live Kit does not show the master effects $5D-$60 set");
		send(_hardware, sysex::kitSave(model, slot));
		advance(_hardware, md::g_samplerate);
		const auto afterDump = exchange(_hardware, sysex::kitRequest(model, slot), 0x52);
		const auto after = sysex::parseKitDump(model, afterDump);
		require(after && after->masterEffects, "no master effects in the saved Kit dump");
		std::printf("MD $5D-$60, parameter n of effect n, Kit saved:%s; dump: %s\n",
			describeEffects(*after->masterEffects).c_str(), differences(afterDump, beforeDump).c_str());
		require(*after->masterEffects == expected, "master effect changes not where the Kit dump decoder reads them");
	}

	std::string describeOutputs(const sysex::TrackOutputs& _outputs)
	{
		std::string text;
		for(size_t track = 0; track < _outputs.size(); ++track)
		{
			const auto output = _outputs[track];
			text += " T" + std::to_string(track + 1) + " "
				+ (output == sysex::TrackOutput::Main ? std::string("MAIN") : std::string(1, static_cast<char>('A' + static_cast<int>(output))));
		}
		return text;
	}

	// The track routing in the Global dump, and $5C for one track.
	void checkTrackRouting(md::Hardware& _hardware)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		const auto slot = status(_hardware, sysex::StatusParameter::Global);
		const auto beforeDump = exchange(_hardware, sysex::globalRequest(model, slot), 0x50);
		const auto before = sysex::parseGlobalDump(model, beforeDump);
		require(before && before->trackOutputs, "no track routing in the Global dump");
		std::printf("MD Global %u routing:%s\n", slot + 1, describeOutputs(*before->trackOutputs).c_str());

		constexpr uint8_t track = 4;
		const auto output = (*before->trackOutputs)[track] == sysex::TrackOutput::C ? sysex::TrackOutput::D : sysex::TrackOutput::C;
		const auto message = sysex::trackRouting(track, output);
		require(message.has_value(), "codec refused a track routing");
		send(_hardware, *message);
		advance(_hardware, md::g_samplerate / 2);
		const auto afterDump = exchange(_hardware, sysex::globalRequest(model, slot), 0x50);
		const auto after = sysex::parseGlobalDump(model, afterDump);
		require(after && after->trackOutputs, "no track routing in the Global dump after $5C");
		std::printf("MD $5C track %u to %c: Global request shows%s; dump: %s\n", track + 1,
			static_cast<char>('A' + static_cast<int>(output)), describeOutputs(*after->trackOutputs).c_str(),
			differences(afterDump, beforeDump).c_str());
		auto expected = *before->trackOutputs;
		expected[track] = output;
		require(*after->trackOutputs == expected, "$5C not where the Global dump decoder reads the routing");
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

	// The playing step and whether the sequencer runs, as JOUER lights them (Hardware::readSequencerPosition,
	// read from the firmware's RAM): a pattern of 12 steps played from PLAY counts 0 to 11 and starts over,
	// and STOP stops it
	void checkSequencerPosition(md::Hardware& _hardware)
	{
		// Whether it plays comes from the sequencer's tick moving between two reads: read as Device does,
		// every block
		const auto readFor = [&](const uint32_t _frames, const std::function<void(const md::Hardware::SequencerPosition&, uint32_t)>& _seen)
		{
			for(uint32_t frame = 0; frame < _frames; frame += 256)
			{
				advance(_hardware, 256);
				const auto position = _hardware.readSequencerPosition();
				require(position.has_value(), "sequencer position unknown");
				_seen(*position, frame);
			}
		};
		bool stopped = true;
		readFor(md::g_samplerate / 2, [&](const md::Hardware::SequencerPosition& _position, uint32_t)
		{
			stopped &= !_position.playing;
		});
		require(stopped, "the sequencer plays before PLAY");
		auto editor = sysex::MdPatternEditor::fromDump(readPattern(_hardware, status(_hardware, sysex::StatusParameter::Pattern)));
		require(editor && editor->setLength(12), "pattern length refused");
		send(_hardware, editor->toDump());
		advance(_hardware, md::g_samplerate);
		panelTap(_hardware, md::PanelControl::Play);
		std::vector<uint8_t> steps;
		bool playing = true;
		readFor(3 * md::g_samplerate, [&](const md::Hardware::SequencerPosition& _position, const uint32_t _frame)
		{
			// From its first tick seen on
			if(_frame >= md::g_samplerate / 4)
				playing &= _position.playing;
			if(steps.empty() || steps.back() != _position.step)
				steps.push_back(_position.step);
		});
		panelTap(_hardware, md::PanelControl::Stop);
		std::optional<md::Hardware::SequencerPosition> after;
		readFor(md::g_samplerate / 2, [&](const md::Hardware::SequencerPosition& _position, uint32_t) { after = _position; });
		bool counts = steps.size() >= 12;
		for(size_t i = 1; i < steps.size(); ++i)
			counts &= steps[i] == (steps[i - 1] + 1) % 12;
		std::string seen;
		for(const auto step : steps)
			seen += " " + std::to_string(step);
		std::printf("MD sequencer position, 12 steps for 3 s:%s; after STOP %s\n", seen.c_str(),
			after && !after->playing ? "stopped" : "still playing");
		require(playing && counts, "the sequencer position does not count the steps of the pattern");
		require(after && !after->playing, "the sequencer position plays on after STOP");
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
		checkMasterEffects(*hardware);
		checkLfo(*hardware);
		checkTrackRouting(*hardware);
		checkPattern(*hardware);
		checkLongPattern(*hardware);
		checkPatternCopy(*hardware);
		checkLiveKitAcrossPatternWrite(*hardware);
		checkSequencerPosition(*hardware);
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
