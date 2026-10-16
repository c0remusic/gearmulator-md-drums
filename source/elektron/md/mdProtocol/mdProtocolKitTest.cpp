// The Kit dump writer against the 32 factory Kits in OS 1.63's flash image (container sections 3 and 4, the
// factory patch memory of a non-UW and a UW machine): each record made into an MdKit, written as a dump and read
// back, by parseMdKit and by parseKitDump, gives its record field for field, Links and Chokes included. Also the
// SET TRIG GROUP and SET MUTE GROUP messages, the Kit-change codec (mdkitchange.h: round trips, the allocating
// encoders, what OS 1.63 ignores, what ASSIGN MACHINE does), and the dumps parseMdKit refuses.
//
// The flash image from GEARMULATOR_MD_FIRMWARE_BIN; 77 without it.

#include "mdProtocol/mdkit.h"
#include "mdProtocol/mdkitchange.h"

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
		// The record of stored Kit _kit, 0 for Kit 1
		const auto stored = [&](const size_t _kit)
		{
			return &image[MdKit::ImageStoredKits + _kit * MdKit::RecordSize];
		};

		const MdKit fresh;
		for(uint8_t slot = 0; slot < MdKit::FactoryKitCount; ++slot)
		{
			const auto* record = stored(slot);
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
		const auto first = MdKit::fromRecord(stored(0));
		const auto last = MdKit::fromRecord(stored(MdKit::FactoryKitCount - 1));
		require(first.displayName() == (_uw ? "TRX UW" : "TRX"), which + ": Kit 1 is " + first.displayName());
		require(last.displayName() == (_uw ? "SEACLONES" : "FUGLESANG"), which + ": Kit 16 is " + last.displayName());
		require(first.chokes[8] == 9, which + ": Kit 1's track 9 does not choke track 10");
		for(uint8_t t = 0; t < g_tracks; ++t)
			require(t == 8 || first.chokes[t] == MdKit::Off, which + ": Kit 1 has another Choke");
		require(MdKit::fromRecord(stored(MdKit::FactoryKitCount)).isEmptySlot(), which + ": Kit 17 is not empty");
		if(_uw)
			require(MdKit::fromRecord(&image[MdKit::ImageWorkingKit]) == first, "the working Kit is not Kit 1");
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

	// The Kit-change codec (mdkitchange.h) without the ROM: every change it encodes it decodes back, the allocating
	// encoders give its bytes, what OS 1.63 ignores it refuses, and what ASSIGN MACHINE leaves in a Track's parameters
	void kitChanges()
	{
		using Kind = sysex::KitChange::Kind;
		using sysex::KitChange;
		const auto bytes = [](const sysex::KitChangeMessage& _m)
		{
			return sysex::Message(_m.bytes.begin(), _m.bytes.begin() + _m.size);
		};
		const auto roundTrip = [&](const KitChange& _change)
		{
			const auto message = sysex::encodeKitChange(_change);
			require(message.has_value(), "a valid change was not encoded");
			const auto view = bytes(*message);
			const auto back = sysex::decodeKitChange(view);
			require(back && *back == _change, "a change did not decode back from its message");
			return view;
		};
		size_t count = 0;
		for(uint8_t t = 0; t < g_tracks; ++t)
		{
			for(int id = 0; id <= 191; ++id)
			{
				for(const uint8_t scope : {uint8_t{0}, uint8_t{1}, KitChange::AssignScope})
					roundTrip({Kind::Machine, t, scope, static_cast<uint8_t>(id)}), ++count;
			}
			for(uint8_t field = 0; field < KitChange::LfoFields; ++field)
			{
				const auto& maxima = md::LfoSettings::FieldMaximum;
				const int maximum = field < maxima.size() ? maxima[field] : 127;
				for(int value = 0; value <= maximum; ++value)
					roundTrip({Kind::Lfo, t, field, static_cast<uint8_t>(value)}), ++count;
			}
			for(uint8_t target = 0; target <= g_tracks; ++target)
			{
				const auto value = target == g_tracks ? MdKit::Off : target;
				roundTrip({Kind::Link, t, 0, value});
				roundTrip({Kind::Choke, t, 0, value});
				count += 2;
			}
		}
		for(uint8_t index = 0; index < 32; ++index)
			for(int value = 0; value < 128; ++value)
				roundTrip({Kind::Master, 0, index, static_cast<uint8_t>(value)}), ++count;
		std::printf("Kit changes: %zu encoded and decoded back\n", count);

		// The allocating encoders are the codec's bytes
		require(sysex::assignMachine(md::MachineModel::Machinedrum, 2, 130)
			== bytes(*sysex::encodeKitChange({Kind::Machine, 2, KitChange::AssignScope, 130})), "assignMachine");
		require(sysex::masterEffectChange(sysex::MasterEffect::Eq, 3, 99)
			== bytes(*sysex::encodeKitChange({Kind::Master, 0, 2 * 8 + 3, 99})), "masterEffectChange");
		require(sysex::lfoChange(4, 1, 23) == bytes(*sysex::encodeKitChange({Kind::Lfo, 4, 1, 23})), "lfoChange");
		require(sysex::linkChange(1, 5) == bytes(*sysex::encodeKitChange({Kind::Link, 1, 0, 5})), "linkChange");
		require(sysex::chokeChange(1, MdKit::Off) == bytes(*sysex::encodeKitChange({Kind::Choke, 1, 0, MdKit::Off})),
			"chokeChange");

		// What OS 1.63 ignores
		const auto decode = [](std::initializer_list<uint8_t> _body)
		{
			sysex::Message m{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00};
			m.insert(m.end(), _body);
			m.push_back(0xf7);
			return sysex::decodeKitChange(m);
		};
		require(decode({0x5b, 1, 0, 2})->value == 0 && decode({0x5b, 1, 0, 1})->value == 128,
			"only a UW flag of 1 adds 128");
		require(decode({0x5b, 1, 0})->index == KitChange::AssignScope && decode({0x5b, 1, 0, 0, 1})->index == 1,
			"a $5B without and with its scope");
		require(!decode({0x5b, 16, 0, 0}) && !decode({0x5b, 0, 0x40, 1}), "a $5B out of range");
		require(!decode({0x62, 0 << 3 | 0, 16}) && !decode({0x62, 3 << 3 | 2, 6}),
			"a $62 value past its field's range");
		require(decode({0x62, 3 << 3 | 7, 127})->value == 127, "$62's field 7");
		require(!decode({0x65, 0, 16}) && !decode({0x66, 0, 0x7e}) && decode({0x66, 0, 0x7f})->value == MdKit::Off,
			"a Link or Choke target from 16 to 126");
		require(!decode({0x5d, 8, 0}) && !decode({0x61, 0, 0}), "a master parameter or command out of range");

		// What ASSIGN MACHINE leaves: the scope's pages, a machine's defaults, MID's, CTR-AL's, CTR-RE's and CTR-8P's
		MdKit kit;
		for(uint8_t t = 0; t < g_tracks; ++t)
			std::fill(kit.parameters[t].begin(), kit.parameters[t].end(), static_cast<uint8_t>(90 + t));
		const std::array<uint8_t, 8> defaults{1, 2, 3, 4, 5, 6, 7, 8};
		const auto page = [&](const uint8_t _track, const int _page)
		{
			std::array<uint8_t, 8> values{};
			std::copy_n(kit.parameters[_track].begin() + 8 * _page, 8, values.begin());
			return values;
		};
		const std::array<uint8_t, 8> kept{91, 91, 91, 91, 91, 91, 91, 91};
		require(!sysex::applyKitChange({Kind::Machine, 1, KitChange::AssignScope, 17}, kit, nullptr)
			&& kit.machine(1) == 0, "a machine the OS lacks was taken");
		kit.machines[1] = 0x01000000;
		require(sysex::applyKitChange({Kind::Machine, 1, 0, 17}, kit, &defaults) && kit.machines[1] == 17,
			"the machine word was not written whole");
		require(page(1, 0) == defaults && page(1, 1) == kept && page(1, 2) == kept, "scope 0 set more than SYN1-8");
		sysex::applyKitChange({Kind::Machine, 1, 1, 17}, kit, &defaults);
		require(page(1, 1) == sysex::os163::Effects && page(1, 2) == kept,
			"scope 1 did not set the effects page alone");
		sysex::applyKitChange({Kind::Machine, 1, KitChange::AssignScope, 17}, kit, &defaults);
		require(page(1, 2) == sysex::os163::Routing, "scope 2 did not set the routing page");
		sysex::applyKitChange({Kind::Machine, 3, KitChange::AssignScope, 96}, kit, &defaults);
		require(page(3, 1) == sysex::os163::MidEffects && page(3, 2) == sysex::os163::MidRouting, "MID-01's pages");
		sysex::applyKitChange({Kind::Machine, 3, KitChange::AssignScope, 112}, kit, &defaults);
		require(page(3, 1) == sysex::os163::CtrAl && page(3, 2) == sysex::os163::CtrAl, "CTR-AL's pages");
		sysex::applyKitChange({Kind::Master, 0, 0 * 8 + 2, 99}, kit);	// the Rhythm Echo's third
		sysex::applyKitChange({Kind::Machine, 3, KitChange::AssignScope, 120}, kit, &defaults);
		std::array<uint8_t, 8> echo{};
		std::copy_n(kit.masterEffects.begin() + 8, 8, echo.begin());
		require(echo[2] == 99 && page(3, 0) == echo, "CTR-RE's SYN1-8 are not the Kit's Rhythm Echo");
		for(uint8_t t = 0; t < 8; ++t)
			kit.parameters[t][0] = static_cast<uint8_t>(10 + t);
		sysex::applyKitChange({Kind::Machine, 2, KitChange::AssignScope, 113}, kit, &defaults);
		const std::array<uint8_t, 8> aimed{10, 11, 10, 13, 14, 15, 17, 10};	// Track 3's own SYN1 after the first knob
		require(page(2, 0) == aimed && page(2, 1) == sysex::os163::Ctr8pEffects
			&& page(2, 2) == sysex::os163::Ctr8pRouting, "CTR-8P's parameters are not its knobs' targets");
		kit.parameters[2][8] = 99;	// a track past 15
		kit.parameters[2][9] = 20;	// a parameter, in range
		sysex::applyKitChange({Kind::Machine, 2, 0, 113}, kit, &defaults);
		require(kit.parameters[2][8] == sysex::os163::Ctr8pEffects[0] && kit.parameters[2][9] == 20,
			"CTR-8P's scope 0 did not check its targets");

		// $62's fields 5-7 are LFOS, LFOD and LFOM
		sysex::applyKitChange({Kind::Lfo, 5, 5, 77}, kit);
		sysex::applyKitChange({Kind::Lfo, 5, 7, 79}, kit);
		require(kit.parameters[5][21] == 77 && kit.parameters[5][23] == 79, "$62's fields 5-7 are not LFOS to LFOM");
		std::printf("Kit changes: refusals and ASSIGN MACHINE as OS 1.63\n");
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
		kitChanges();
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
