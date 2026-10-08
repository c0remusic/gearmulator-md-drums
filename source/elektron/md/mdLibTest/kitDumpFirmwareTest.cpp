// The Kit dump codec of mdProtocol (MdKit) against the Machinedrum's own firmware:
// - the 16 factory Kits the firmware sends, read and written again, come out byte for byte;
// - a Kit written by mdKitDump, with its name, Links, Chokes and an LFO changed, is stored by the firmware and sent
//   back byte for byte;
// - SET TRIG GROUP and SET MUTE GROUP ($65, $66), OFF included, change the live Kit as the dump then reads them.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "mdProtocol/mdkit.h"

#include "baseLib/filesystem.h"

#include <algorithm>
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
	using sysex::MdKit;
	constexpr auto g_model = md::MachineModel::Machinedrum;

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

	sysex::Message requestKit(md::Hardware& _hardware, const uint8_t _slot)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hardware.readMidiOut(events);
		send(_hardware, sysex::kitRequest(g_model, _slot));
		for(uint32_t block = 0; block < md::g_samplerate * 4 / 64; ++block)
		{
			advance(_hardware, 64);
			events.clear();
			_hardware.readMidiOut(events);
			for(const auto& event : events)
			{
				if(event.sysex.size() > 9 && event.sysex[6] == 0x52)
					return sysex::Message(event.sysex.begin(), event.sysex.end());
			}
		}
		throw std::runtime_error("no answer to the request for Kit " + std::to_string(_slot + 1));
	}

	std::string hex(const sysex::Message& _a, const sysex::Message& _b)
	{
		if(_a.size() != _b.size())
			return "sizes " + std::to_string(_a.size()) + " and " + std::to_string(_b.size());
		const auto at = static_cast<size_t>(std::mismatch(_a.begin(), _a.end(), _b.begin()).first - _a.begin());
		char text[64];
		std::snprintf(text, sizeof(text), "byte $%03zx: $%02x against $%02x", at, _a[at], _b[at]);
		return text;
	}

	void run(const char* _path)
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, _path), std::string("cannot read ") + _path);
		require(md::RomLoader::isRomForModel(rom, g_model), std::string(_path) + " is not a Machinedrum firmware");
		auto hardware = std::make_unique<md::Hardware>(rom, _path, g_model);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
		{
			advance(*hardware, 64);
			require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
		}
		advance(*hardware, md::g_samplerate * 20);

		// The factory Kits as the firmware sends them, read and written again
		sysex::Message first;
		for(uint8_t slot = 0; slot < 16; ++slot)
		{
			const auto dump = requestKit(*hardware, slot);
			uint8_t readSlot = 0xff;
			const auto kit = sysex::parseMdKit(dump, &readSlot);
			require(kit.has_value() && readSlot == slot, "Kit " + std::to_string(slot + 1) + " not read");
			const auto again = sysex::mdKitDump(*kit, slot);
			require(again == dump, "Kit " + std::to_string(slot + 1) + " " + kit->displayName()
				+ " written again differs at " + hex(again, dump));
			std::printf("Kit %2u %-10s: %zu bytes, written again byte for byte\n", slot + 1,
				kit->displayName().c_str(), dump.size());
			if(slot == 0)
				first = dump;
		}

		// A Kit of ours stored by the firmware and sent back as written
		auto kit = *sysex::parseMdKit(first);
		const char name[] = "MD DRUMS";
		kit.name.fill(0);
		std::copy(name, name + sizeof(name) - 1, kit.name.begin());
		kit.links[0] = 3;
		kit.links[5] = 5;
		kit.chokes[8] = MdKit::Off;
		kit.chokes[2] = 14;
		kit.setLfo(4, {7, 12, md::LfoSettings::Square, md::LfoSettings::Random, md::LfoSettings::Hold});
		kit.machines[6] = 129;		// ROM-02
		kit.levels[11] = 33;
		const auto written = sysex::mdKitDump(kit, 40);
		send(*hardware, written);
		advance(*hardware, md::g_samplerate);
		const auto stored = requestKit(*hardware, 40);
		require(stored == written, "the firmware stored another Kit than written: " + hex(stored, written));
		std::printf("Kit 41 %s: written, stored and sent back byte for byte\n", kit.displayName().c_str());

		// Links and Chokes of the live Kit (factory Kit 1), saved into Kit 42
		for(const auto& message : {sysex::linkChange(1, 7), sysex::chokeChange(9, 1), sysex::chokeChange(8, MdKit::Off),
			sysex::linkChange(12, 12)})
		{
			require(message.has_value(), "no group message");
			send(*hardware, *message);
			advance(*hardware, md::g_samplerate / 10);
		}
		send(*hardware, sysex::kitSave(g_model, 41));
		advance(*hardware, md::g_samplerate);
		const auto saved = sysex::parseMdKit(requestKit(*hardware, 41));
		require(saved.has_value(), "Kit 42 not read");
		auto expected = *sysex::parseMdKit(first);
		expected.links[1] = 7;
		expected.links[12] = 12;
		expected.chokes[9] = 1;
		expected.chokes[8] = MdKit::Off;
		require(saved->links == expected.links && saved->chokes == expected.chokes,
			"SET TRIG GROUP or SET MUTE GROUP did not reach the Kit as sent");
		std::printf("$65 and $66: Links and Chokes set, $7f stored as OFF ($%02x)\n", saved->chokes[8]);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::printf("mdKitDumpFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n");
		return 77;
	}
	try
	{
		run(path);
		std::printf("mdKitDumpFirmwareTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdKitDumpFirmwareTest: FAIL: %s\n", _error.what());
		return 1;
	}
}
