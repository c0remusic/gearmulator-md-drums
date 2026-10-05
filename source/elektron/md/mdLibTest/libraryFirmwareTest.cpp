// BIBLIO's library read from the machine's RAM (md::Hardware::readLibrary) against what the firmware answers Kit
// requests ($53) and pattern requests ($68) with: each Kit's name and machines, each pattern's length, Kit and
// trigs within its length, on the Machinedrum and the Monomachine. Some Kits and patterns are changed first, so
// that the slots compared do not all hold the factory's; the slots compared span the first, the last and some
// between, as the layout gives each its own place.
//
// Firmware from GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN; 77 without either.

#include "mmFirmwareMachine.h"

#include "mdLib/mdhardware.h"
#include "mdLib/mdlibrary.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <bitset>
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

	std::string slotName(const char* _what, const uint8_t _slot)
	{
		return std::string(_what) + " " + std::to_string(_slot);
	}

	void runMachinedrum(const char* const path)
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		require(md::RomLoader::isRomForModel(rom, g_model), std::string(path) + " is not a Machinedrum firmware");
		auto hardware = std::make_unique<md::Hardware>(rom, path, g_model);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
		{
			advance(*hardware, 64);
			require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
		}
		advance(*hardware, md::g_samplerate * 20);
		require(hardware->canReadLibrary(), "no library layout for Machinedrum OS 1.63");

		// Patterns: A01 13 steps on Kit 4, B09 64 steps on Kit 22 with trigs past step 32, H16 32 steps on Kit 64
		const auto status = sysex::parseStatusResponse(g_model,
			exchange(*hardware, sysex::statusRequest(g_model, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		const auto source = exchange(*hardware, sysex::patternRequest(g_model, status->value), 0x67);
		struct Written { uint8_t slot; uint8_t length; uint8_t kit; };
		for(const auto& written : {Written{0, 13, 3}, Written{40, 64, 21}, Written{127, 32, 63}})
		{
			auto editor = sysex::MdPatternEditor::fromDump(source);
			require(editor && editor->setSlot(written.slot) && editor->setLength(64), "pattern not editable");
			editor->clear();
			for(uint8_t step = 0; step < 64; step += 3)
				require(editor->setTrig(static_cast<uint8_t>(step % 16), step, true), "trig refused");
			require(editor->setLength(written.length) && editor->setKit(written.kit), "length or Kit refused");
			send(*hardware, editor->toDump());
			advance(*hardware, md::g_samplerate);
		}

		// Kits: machines assigned to tracks 1 and 16, the live Kit saved into Kits 22 and 64
		for(const auto& [slot, machine] : std::vector<std::pair<uint8_t, uint16_t>>{{21, 17}, {63, 32}})
		{
			for(const uint8_t track : {uint8_t{0}, uint8_t{15}})
			{
				const auto assign = sysex::assignMachine(g_model, track, machine);
				require(assign.has_value(), "machine not assignable");
				send(*hardware, *assign);
				advance(*hardware, md::g_samplerate / 2);
			}
			send(*hardware, sysex::kitSave(g_model, slot));
			advance(*hardware, md::g_samplerate);
		}

		auto library = std::make_unique<md::Library>();
		const auto began = std::chrono::steady_clock::now();
		require(hardware->readLibrary(*library), "the library was not read");
		const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count();
		require(library->kitCount == 64 && library->tracks == 16, "not 64 Kits of 16 tracks");

		size_t kits = 0;
		for(const uint8_t slot : std::array<uint8_t, 9>{0, 1, 2, 20, 21, 22, 40, 62, 63})
		{
			const auto dump = sysex::parseKitDump(g_model, exchange(*hardware, sysex::kitRequest(g_model, slot), 0x52));
			require(dump.has_value() && dump->slot == slot, slotName("no dump for Kit", slot));
			const auto& kit = library->kits[slot];
			const std::vector<uint16_t> machines(kit.machines.begin(), kit.machines.begin() + library->tracks);
			require(library->name(slot) == dump->name, slotName("another name for Kit", slot) + ": \"" + library->name(slot)
				+ "\", the dump \"" + dump->name + "\"");
			require(machines == dump->machines, slotName("other machines for Kit", slot));
			++kits;
		}
		require(library->kits[21].machines[0] == 17 && library->kits[21].machines[15] == 17
			&& library->kits[63].machines[0] == 32 && library->kits[63].machines[15] == 32, "the saved Kits' machines are not read");

		size_t patterns = 0;
		for(const uint8_t slot : std::array<uint8_t, 12>{0, 1, 2, 15, 16, 39, 40, 41, 64, 100, 126, 127})
		{
			const auto dump = sysex::parseMdPatternDump(exchange(*hardware, sysex::patternRequest(g_model, slot), 0x67));
			const auto& pattern = library->patterns[slot];
			if(!dump)
			{
				require(pattern.length == 0, slotName("a length for pattern", slot) + " whose dump does not parse");
				continue;
			}
			uint16_t trigs = 0;
			const auto inLength = dump->length >= 64 ? ~uint64_t{0} : (uint64_t{1} << dump->length) - 1;
			for(const auto mask : dump->trigs)
				trigs = static_cast<uint16_t>(trigs + std::bitset<64>(mask & inLength).count());
			require(pattern.length == dump->length && pattern.kit == dump->kit && pattern.trigs == trigs,
				slotName("pattern", slot) + ": length " + std::to_string(pattern.length) + ", Kit " + std::to_string(pattern.kit)
				+ ", " + std::to_string(pattern.trigs) + " trigs; the dump " + std::to_string(dump->length) + ", "
				+ std::to_string(dump->kit) + ", " + std::to_string(trigs));
			++patterns;
		}
		require(library->patterns[0].length == 13 && library->patterns[40].length == 64 && library->patterns[40].kit == 21
			&& library->patterns[127].length == 32 && library->patterns[127].kit == 63, "the written patterns are not read");
		std::printf("mdLibraryFirmwareTest: MD read in %lld us; %zu Kits and %zu patterns as their dumps say\n",
			static_cast<long long>(micros), kits, patterns);
	}

	void runMonomachine(const char* const _path)
	{
		constexpr auto model = md::MachineModel::Monomachine;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, _path), std::string("cannot read ") + _path);
		require(md::RomLoader::isRomForModel(rom, model), std::string(_path) + " is not a Monomachine firmware");
		md::test::Monomachine machine(rom, _path);
		auto& hardware = machine.hardware();
		require(hardware.canReadLibrary(), "no library layout for Monomachine OS 1.32b");

		// Patterns: A01 13 steps on Kit 4, B09 64 steps on Kit 22, H16 47 steps on Kit 128
		const auto source = machine.readPattern(machine.status(sysex::StatusParameter::Pattern));
		struct Written { uint8_t slot; uint8_t length; uint8_t kit; };
		for(const auto& written : {Written{0, 13, 3}, Written{40, 64, 21}, Written{127, 47, 127}})
		{
			auto editor = sysex::MmPatternEditor::fromDump(source);
			require(editor && editor->setSlot(written.slot) && editor->setLength(64) && editor->setKit(written.kit),
				"pattern not editable");
			for(uint8_t step = 0; step < 64; step += 3)
				require(editor->setTrig(static_cast<uint8_t>(step % 6), step, uint8_t{60}), "trig refused");
			require(editor->setLength(written.length), "length refused");
			machine.writePattern(editor->toDump());
		}

		// Kits: machines assigned to tracks 1 and 6, the live Kit saved into Kits 22 and 128
		for(const auto& [slot, id] : std::vector<std::pair<uint8_t, uint16_t>>{{21, 4}, {127, 8}})
		{
			for(const uint8_t track : {uint8_t{0}, uint8_t{5}})
			{
				const auto assign = sysex::assignMachine(model, track, id);
				require(assign.has_value(), "machine not assignable");
				machine.send(*assign);
				advance(hardware, md::g_samplerate / 2);
			}
			machine.send(sysex::kitSave(model, slot));
			advance(hardware, md::g_samplerate);
		}

		auto library = std::make_unique<md::Library>();
		const auto began = std::chrono::steady_clock::now();
		require(hardware.readLibrary(*library), "the library was not read");
		const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count();
		require(library->kitCount == 128 && library->tracks == 6, "not 128 Kits of 6 tracks");

		size_t kits = 0;
		for(const uint8_t slot : std::array<uint8_t, 8>{0, 1, 20, 21, 22, 64, 126, 127})
		{
			const auto dump = sysex::parseKitDump(model, machine.exchange(sysex::kitRequest(model, slot), 0x52));
			require(dump.has_value() && dump->slot == slot, slotName("no dump for Kit", slot));
			const auto& kit = library->kits[slot];
			const std::vector<uint16_t> machines(kit.machines.begin(), kit.machines.begin() + library->tracks);
			require(library->name(slot) == dump->name, slotName("another name for Kit", slot) + ": \"" + library->name(slot)
				+ "\", the dump \"" + dump->name + "\"");
			require(machines == dump->machines, slotName("other machines for Kit", slot));
			++kits;
		}
		require(library->kits[21].machines[0] == 4 && library->kits[21].machines[5] == 4
			&& library->kits[127].machines[0] == 8 && library->kits[127].machines[5] == 8, "the saved Kits' machines are not read");

		size_t patterns = 0;
		for(const uint8_t slot : std::array<uint8_t, 9>{0, 1, 15, 39, 40, 41, 100, 126, 127})
		{
			const auto dump = sysex::parseMmPatternDump(machine.readPattern(slot));
			const auto& pattern = library->patterns[slot];
			if(!dump)
			{
				require(pattern.length == 0, slotName("a length for pattern", slot) + " whose dump does not parse");
				continue;
			}
			uint16_t trigs = 0;
			for(uint8_t track = 0; track < sysex::MmPatternDump::TrackCount; ++track)
			{
				for(uint8_t step = 0; step < dump->length; ++step)
					trigs = static_cast<uint16_t>(trigs + (dump->hasTrig(track, step) ? 1 : 0));
			}
			require(pattern.length == dump->length && pattern.kit == dump->kit && pattern.trigs == trigs,
				slotName("pattern", slot) + ": length " + std::to_string(pattern.length) + ", Kit " + std::to_string(pattern.kit)
				+ ", " + std::to_string(pattern.trigs) + " trigs; the dump " + std::to_string(dump->length) + ", "
				+ std::to_string(dump->kit) + ", " + std::to_string(trigs));
			++patterns;
		}
		require(library->patterns[0].length == 13 && library->patterns[40].length == 64 && library->patterns[40].kit == 21
			&& library->patterns[127].length == 47 && library->patterns[127].kit == 127, "the written patterns are not read");
		std::printf("mdLibraryFirmwareTest: MM read in %lld us; %zu Kits and %zu patterns as their dumps say\n",
			static_cast<long long>(micros), kits, patterns);
	}
}

int main()
{
	try
	{
		const auto* md = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		const auto* mm = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if((!md || !*md) && (!mm || !*mm))
		{
			std::printf("mdLibraryFirmwareTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN not set)\n");
			return 77;
		}
		if(md && *md)
			runMachinedrum(md);
		if(mm && *mm)
			runMonomachine(mm);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdLibraryFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
