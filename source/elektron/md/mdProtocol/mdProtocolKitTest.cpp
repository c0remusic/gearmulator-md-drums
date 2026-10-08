// The Kit dump writer against the 32 factory Kits in OS 1.63's flash image (container sections 3 and 4, the
// factory patch memory of a non-UW and a UW machine): each record made into an MdKit, written as a dump and read
// back, by parseMdKit and by parseKitDump, gives its record field for field, Links and Chokes included. Also the
// SET TRIG GROUP and SET MUTE GROUP messages, and the dumps parseMdKit refuses.
//
// The flash image from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdProtocol/mdkit.h"

#include "Firmware.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	namespace sysex = md::automation::sysex;
	using sysex::MdKit;

	constexpr size_t g_firstKit = 0x8ca;
	constexpr size_t g_workingKit = 0x0a;
	constexpr uint8_t g_tracks = 16;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// The first field two Kits differ in, or empty
	std::string firstDifference(const MdKit& _a, const MdKit& _b)
	{
		if(_a.name != _b.name) return "name";
		for(uint8_t t = 0; t < g_tracks; ++t)
		{
			const auto track = " of track " + std::to_string(t + 1);
			if(_a.parameters[t] != _b.parameters[t]) return "parameters" + track;
			if(_a.levels[t] != _b.levels[t]) return "level" + track;
			if(_a.machines[t] != _b.machines[t]) return "machine" + track;
			if(_a.lfos[t] != _b.lfos[t]) return "LFO" + track;
			if(_a.links[t] != _b.links[t]) return "Link" + track;
			if(_a.chokes[t] != _b.chokes[t]) return "Choke" + track;
		}
		if(_a.masterEffects != _b.masterEffects) return "master effects";
		return {};
	}

	// What parseKitDump, the editor's reader, takes from the dump, against the Kit written
	void compareWithKitDump(const MdKit& _kit, const sysex::Message& _dump, const std::string& _what)
	{
		const auto parsed = sysex::parseKitDump(md::MachineModel::Machinedrum, _dump);
		require(parsed.has_value(), _what + ": parseKitDump refused the dump");
		require(parsed->name == _kit.displayName(), _what + ": parseKitDump read another name");
		require(parsed->machines.size() == g_tracks && parsed->lfos && parsed->masterEffects,
			_what + ": parseKitDump missed the machines, LFOs or master effects");
		require(*parsed->masterEffects == _kit.masterEffectValues(), _what + ": master effects differ");
		for(uint8_t t = 0; t < g_tracks; ++t)
		{
			require(parsed->machines[t] == _kit.machine(t), _what + ": a machine differs");
			require((*parsed->lfos)[t] == _kit.lfo(t), _what + ": an LFO differs");
		}
		for(const auto& change : parsed->parameters)
		{
			const auto expected = change.page == md::automation::machinedrum::Level ? _kit.levels[change.track]
				: _kit.parameters[change.track][change.page * 8 + change.index];
			require(change.value == expected, _what + ": a parameter differs");
		}
	}

	void factoryKits(const std::vector<uint8_t>& _flash, const bool _uw)
	{
		const auto image = md::fw::loadPatchImageFromFlash(_flash, _uw);
		const std::string which = _uw ? "UW" : "non-UW";
		require(image.size() == 0x80000, which + " factory image missing");

		const MdKit fresh;
		for(uint8_t slot = 0; slot < 16; ++slot)
		{
			const auto* record = &image[g_firstKit + MdKit::RecordSize * slot];
			const auto kit = MdKit::fromRecord(record);
			const auto what = which + " Kit " + std::to_string(slot + 1) + " " + kit.displayName();

			std::vector<uint8_t> again(MdKit::RecordSize);
			kit.toRecord(again.data());
			require(std::equal(again.begin(), again.end(), record), what + ": the record does not come back whole");

			const auto dump = sysex::mdKitDump(kit, slot);
			require(dump.size() == sysex::MdKitDumpSize, what + ": dump of " + std::to_string(dump.size()) + " bytes");
			uint8_t readSlot = 0xff;
			const auto parsed = sysex::parseMdKit(dump, &readSlot);
			require(parsed.has_value() && readSlot == slot, what + ": parseMdKit refused the dump or lost its slot");
			const auto difference = firstDifference(*parsed, kit);
			require(difference.empty(), what + ": " + difference + " differs after the dump");
			require(sysex::mdKitDump(*parsed, slot) == dump, what + ": the dump does not come back byte for byte");
			compareWithKitDump(kit, dump, what);

			// The values a new Kit gets are the ones every factory Kit holds where parseKitDump reads nothing
			for(uint8_t t = 0; t < g_tracks; ++t)
			{
				require(std::equal(kit.lfos[t].begin() + 5, kit.lfos[t].end(), fresh.lfos[t].begin() + 5),
					what + ": an LFO's state is not the one a new Kit gets");
				require((kit.machines[t] >> 8) == 0, what + ": a machine word has upper bits");
				require(kit.links[t] == MdKit::Off, what + ": a factory Kit holds a Link");
				require(kit.chokes[t] == MdKit::Off || kit.chokes[t] < g_tracks, what + ": a Choke out of range");
			}

			std::printf("%s: record, dump and back, %u Choke(s)\n", what.c_str(),
				static_cast<unsigned>(std::count_if(kit.chokes.begin(), kit.chokes.end(),
					[](const uint8_t _c) { return _c != MdKit::Off; })));
		}

		// Known Kits: Kit 1 and 16 of each image, the closed hat on track 9 choking the open hat on track 10
		const auto first = MdKit::fromRecord(&image[g_firstKit]);
		const auto last = MdKit::fromRecord(&image[g_firstKit + MdKit::RecordSize * 15]);
		require(first.displayName() == (_uw ? "TRX UW" : "TRX"), which + ": Kit 1 is " + first.displayName());
		require(last.displayName() == (_uw ? "SEACLONES" : "FUGLESANG"), which + ": Kit 16 is " + last.displayName());
		require(first.chokes[8] == 9, which + ": Kit 1's track 9 does not choke track 10");
		for(uint8_t t = 0; t < g_tracks; ++t)
			require(t == 8 || first.chokes[t] == MdKit::Off, which + ": Kit 1 has another Choke");
		require(MdKit::fromRecord(&image[g_firstKit + MdKit::RecordSize * 16]).isEmptySlot(),
			which + ": Kit 17 is not empty");
		if(_uw)
			require(MdKit::fromRecord(&image[g_workingKit]) == first, "the working Kit is not Kit 1");
	}

	void groupMessages()
	{
		using M = sysex::Message;
		require(sysex::linkChange(0, 3) == M{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x65, 0x00, 0x03, 0xf7},
			"SET TRIG GROUP");
		require(sysex::chokeChange(8, MdKit::Off) == M{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x66, 0x08, 0x7f, 0xf7},
			"SET MUTE GROUP off");
		require(sysex::chokeChange(15, 15).has_value(), "a self-reference refused");
		require(!sysex::linkChange(16, 0) && !sysex::chokeChange(0, 16) && !sysex::chokeChange(0, 0x7f),
			"a Track out of range accepted");
	}

	void refusedDumps()
	{
		MdKit kit;
		kit.links[2] = 5;
		kit.chokes[3] = MdKit::Off;
		auto dump = sysex::mdKitDump(kit, 63);
		require(sysex::parseMdKit(dump).has_value(), "a fresh Kit's dump refused");

		auto corrupt = dump;
		corrupt[0x20] ^= 1;
		require(!sysex::parseMdKit(corrupt), "a dump with a wrong checksum accepted");

		// The checksum starts after the version byte: the same payload under another version stays valid
		for(const uint8_t version : {1, 2, 3, 64})
		{
			auto other = dump;
			other[7] = version;
			require(sysex::parseMdKit(other).has_value() == (version == 3),
				"version " + std::to_string(version) + " read wrongly");
		}

		auto slot64 = sysex::mdKitDump(kit, 64);
		require(!sysex::parseMdKit(slot64), "a dump for slot 64 accepted");
		auto shorter = dump;
		shorter.erase(shorter.begin() + 20);
		require(!sysex::parseMdKit(shorter), "a short dump accepted");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		groupMessages();
		refusedDumps();

		const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path || !*path)
		{
			std::printf("mdProtocolKitTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN not set)\n");
			return 77;
		}
		const auto flash = md::fw::readFile(path);
		factoryKits(flash, true);
		factoryKits(flash, false);
		std::printf("mdProtocolKitTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdProtocolKitTest: FAIL: %s\n", _error.what());
		return 1;
	}
}
