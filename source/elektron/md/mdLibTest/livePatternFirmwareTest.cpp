// The Machinedrum's pattern edited in its RAM (md::LivePatternLayout, md::livePatternWrites), as the editor's
// JOUER does, on B09: the machine plays a stored pattern from its own blocks, A01's first, so an edit of any
// other pattern shows whether its blocks were found. A trig and its lock written there are what the next
// pattern request answers with, the trig plays
// from the next pass, it is kept when another pattern is selected, an edit made from a pattern the RAM no
// longer holds is refused without a byte written, and the edits are in the state a project saves, the last
// one too when no pattern was selected after it.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdlivepattern.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	namespace sysex = md::automation::sysex;
	constexpr auto g_model = md::MachineModel::Machinedrum;
	constexpr uint8_t g_slot = 40;	// B09

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

	void send(md::Hardware& _hardware, const sysex::Message& _message)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex.assign(_message.begin(), _message.end());
		require(_hardware.sendMidi(event), "SysEx rejected by the MIDI input");
	}

	sysex::Message exchange(md::Hardware& _hardware, const sysex::Message& _request, const uint8_t _command)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hardware.readMidiOut(events);
		send(_hardware, _request);
		for(uint32_t block = 0; block < md::g_samplerate * 4 / 64; ++block)
		{
			advance(_hardware, 64);
			events.clear();
			_hardware.readMidiOut(events);
			for(const auto& event : events)
			{
				if(event.sysex.size() > 9 && event.sysex[6] == _command)
					return sysex::Message(event.sysex.begin(), event.sysex.end());
			}
		}
		throw std::runtime_error("no answer to a request");
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

	// The output's energy over _seconds of emulated time
	double energy(md::Hardware& _hardware, const double _seconds)
	{
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		double sum = 0;
		const auto blocks = static_cast<uint32_t>(_seconds * md::g_samplerate / 256);
		for(uint32_t block = 0; block < blocks; ++block)
		{
			_hardware.processAudio(outputs, 256, 0);
			for(const auto& channel : samples)
			{
				for(const auto value : channel)
					sum += static_cast<double>(value) * value;
			}
		}
		return sum;
	}

	bool run()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("mdLivePatternFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n");
			return false;
		}
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		require(md::RomLoader::isRomForModel(rom, g_model), std::string(path) + " is not a Machinedrum firmware");
		// A Device, for the state a project saves; no home path, so no machine-local factory cache
		synthLib::DeviceCreateParams params;
		params.romData = rom;
		params.romName = path;
		params.customData = md::deviceCustomData(g_model);
		const auto device = std::make_unique<md::Device>(params);
		require(device->isValid(), "the firmware did not make a valid device");
		auto* hardware = &device->getHardware();
		const auto boot = [&]
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
			while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			{
				advance(*hardware, 64);
				require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
			}
			advance(*hardware, md::g_samplerate * 20);
		};
		boot();
		const auto layout = hardware->livePatternLayout();
		require(layout.has_value(), "no pattern RAM layout for Machinedrum OS 1.63");

		// B09: the RAM holds every stored pattern, A01 first, and the machine plays the one selected from there
		send(*hardware, sysex::patternSelect(g_model, g_slot));
		advance(*hardware, md::g_samplerate);
		const auto status = sysex::parseStatusResponse(g_model,
			exchange(*hardware, sysex::statusRequest(g_model, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value() && status->value == g_slot, "pattern B09 not selected");
		const auto slot = status->value;
		const auto readPattern = [&]
		{
			return exchange(*hardware, sysex::patternRequest(g_model, slot), 0x67);
		};

		// The pattern playing made 16 steps without a trig, written as a dump: what the edits start from
		auto blank = sysex::MdPatternEditor::fromDump(readPattern());
		require(blank.has_value() && blank->setLength(16), "pattern unreadable");
		blank->clear();
		send(*hardware, blank->toDump());
		advance(*hardware, md::g_samplerate);
		const auto from = readPattern();
		auto edited = sysex::MdPatternEditor::fromDump(from);
		require(edited.has_value() && edited->setTrig(0, 4, true) && edited->setLock(0, 3, 4, 77), "edit refused");
		const auto to = edited->toDump();

		// Heard: the blank pattern playing is silent; the trig of track 1 step 5 written in the RAM plays
		panelTap(*hardware, md::PanelControl::Play);
		const auto silent = energy(*hardware, 2.0);
		const auto writes = md::livePatternWrites(*layout, from, to);
		require(writes.has_value() && !writes->empty(), "no bytes for the edit");
		require(hardware->writeRamIfUnchanged(*writes), "the edit was refused on the pattern it was made from");
		const auto played = energy(*hardware, 2.5);
		std::printf("mdLivePatternFirmwareTest: %zu bytes written; energy over 2 s blank %.3g, with the trig %.3g\n",
			writes->size(), silent, played);
		require(played > 1.0 && silent < played / 100, "the trig written in the RAM did not play");
		panelTap(*hardware, md::PanelControl::Stop);

		// Read: the pattern request answers with the edit
		const auto readBack = sysex::parseMdPatternDump(readPattern());
		require(readBack && readBack->hasTrig(0, 4) && readBack->lock(0, 3, 4) == std::optional<uint8_t>{77},
			"the pattern request does not answer with the edit");

		// Refused: an edit made from the pattern before the first one, which the RAM no longer holds
		auto stale = sysex::MdPatternEditor::fromDump(from);
		require(stale.has_value() && stale->setTrig(0, 4, true) && stale->setLock(0, 3, 4, 99), "stale edit refused");
		const auto staleWrites = md::livePatternWrites(*layout, from, stale->toDump());
		require(staleWrites.has_value() && !hardware->writeRamIfUnchanged(*staleWrites),
			"an edit made from a pattern the RAM no longer holds was written");
		const auto afterStale = sysex::parseMdPatternDump(readPattern());
		require(afterStale && afterStale->lock(0, 3, 4) == std::optional<uint8_t>{77}, "a refused edit changed the pattern");

		// Kept: another pattern selected, then this one again
		const auto other = static_cast<uint8_t>((slot + 1) % 128);
		send(*hardware, sysex::patternSelect(g_model, other));
		advance(*hardware, md::g_samplerate);
		send(*hardware, sysex::patternSelect(g_model, slot));
		advance(*hardware, md::g_samplerate);
		const auto kept = sysex::parseMdPatternDump(readPattern());
		require(kept && kept->hasTrig(0, 4) && kept->lock(0, 3, 4) == std::optional<uint8_t>{77},
			"the edit was not kept across a pattern change");
		std::printf("mdLivePatternFirmwareTest: read back, a stale edit refused, kept across patterns %u and %u\n", other, slot);

		// Saved: a second edit, with no pattern selected after it, then the state a project saves booted again
		const auto beforeSave = readPattern();
		auto last = sysex::MdPatternEditor::fromDump(beforeSave);
		require(last.has_value() && last->setTrig(1, 8, true), "second edit refused");
		const auto lastWrites = md::livePatternWrites(*layout, beforeSave, last->toDump());
		require(lastWrites.has_value() && hardware->writeRamIfUnchanged(*lastWrites), "the second edit was refused");
		std::vector<uint8_t> state;
		require(device->getState(state, synthLib::StateTypeGlobal), "no state to save");
		md::FactoryFlashSnapshot factoryFlash;
		(void)hardware->copyFactoryFlashSnapshot(factoryFlash);
		std::string error;
		auto reboot = md::Device::prepareState(device->getPreparationContext(), state, synthLib::StateTypeGlobal,
			factoryFlash, &error);
		require(reboot && device->commitPreparedState(*reboot), "the saved state did not boot: " + error);
		reboot.reset();
		hardware = &device->getHardware();
		boot();
		const auto restoredStatus = sysex::parseStatusResponse(g_model,
			exchange(*hardware, sysex::statusRequest(g_model, sysex::StatusParameter::Pattern), 0x72));
		require(restoredStatus && restoredStatus->value == slot, "another pattern selected after the saved state");
		const auto restored = sysex::parseMdPatternDump(readPattern());
		require(restored && restored->hasTrig(0, 4) && restored->lock(0, 3, 4) == std::optional<uint8_t>{77},
			"the edit kept across patterns was lost with the saved state");
		require(restored->hasTrig(1, 8), "the edit made since the last pattern change was lost with the saved state");
		std::printf("mdLivePatternFirmwareTest: both edits in the saved state, %zu bytes\n", state.size());
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
		std::printf("mdLivePatternFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
