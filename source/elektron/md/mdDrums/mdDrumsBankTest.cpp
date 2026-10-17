// The Kits' Bank (ticket 26 of the editor map): a new file holds the 16 factory Kits in Slots 1-16, Kit 1 the one a
// Machinedrum boots with, the other 48 empty; each Slot is a Kit dump OS 1.63 takes, for its slot; a write keeps the
// file's size and the other Slots; two Banks on one file keep each other's Slots; an emptied Slot reads back empty; a
// .syx file's Kits come out in their order, the empty Slots left out; one write puts several Slots in place; a Bank
// follows a Slot another one wrote. Every factory Kit, and a new one, seeds the LFO RND generator.

#include "mdDrumsBank.h"
#include "mdDrumsMessages.h"

#include "mdDrumsTestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using mdDrums::test::readFile;
	using mdDrums::test::require;

	using Kit = md::automation::sysex::MdKit;

	Kit named(Kit _kit, const char* _name)
	{
		_kit.setName(_name);
		return _kit;
	}
}

int main()
{
	return mdDrums::test::run("mdDrumsBankTest", [](const std::vector<uint8_t>& _flash)
	{
		const mdDrums::test::TempFolder folder("mdDrumsBankTest_");
		const auto factory = mdDrums::messages::factoryKits(_flash);
		const auto kit1 = mdDrums::messages::factoryKit(_flash);
		require(factory.size() == 16, "the flash image holds " + std::to_string(factory.size()) + " factory Kits");
		require(kit1 && factory[0] == *kit1, "Slot 1 is not the Kit a Machinedrum boots with");

		// The LFO RND generator's two words (each LFO block's bytes 24-31, which a Kit loads into the running LFO): mdEngine
		// starts them at 0, where RND never moves; every Kit the Device plays seeds them, a new one too
		const auto seeds = [](const auto& _lfo)
		{
			return std::any_of(_lfo.begin() + 24, _lfo.begin() + 32, [](const uint8_t _b) { return _b != 0; });
		};
		int seeded = 0;
		for(const auto& kit : factory)
			for(const auto& lfo : kit.lfos)
				seeded += seeds(lfo);
		const Kit blank;
		const bool blankSeeded = seeds(blank.lfos[0]);
		std::printf("LFO RND: %d of %d factory LFO blocks seed it, a new Kit's %s\n", seeded, 16 * 16,
			blankSeeded ? "too" : "not");
		require(seeded == 16 * 16 && blankSeeded, "a Kit leaves the LFO RND generator unseeded");

		const auto file = folder.path() / "Bank.syx";
		mdDrums::Bank bank(file, factory);
		require(bank.read(), "a new Bank cannot be written");
		const auto bytes = readFile(file);
		require(bytes.size() == mdDrums::Bank::SlotCount * mdDrums::Bank::SlotSize, "a new Bank is "
			+ std::to_string(bytes.size()) + " bytes");
		size_t filled = 0;
		std::string names;
		for(int slot = 0; slot < mdDrums::Bank::SlotCount; ++slot)
		{
			// Every Slot a dump OS 1.63 takes, for its own slot
			const std::vector<uint8_t> message(bytes.begin() + static_cast<std::ptrdiff_t>(slot * mdDrums::Bank::SlotSize),
				bytes.begin() + static_cast<std::ptrdiff_t>((slot + 1) * mdDrums::Bank::SlotSize));
			uint8_t dumpSlot = 0xff;
			const auto parsed = md::automation::sysex::parseMdKit(message, &dumpSlot);
			require(parsed && dumpSlot == slot, "Slot " + std::to_string(slot + 1) + " is no Kit dump for its slot");
			if(bank.isEmpty(slot))
				continue;
			require(slot < 16 && *bank.slot(slot) == factory[static_cast<size_t>(slot)], "Slot " + std::to_string(slot + 1)
				+ " is not the factory's");
			++filled;
			names += (names.empty() ? "" : ", ") + bank.slot(slot)->displayName();
		}
		std::printf("new Bank: %zu bytes, %zu factory Kits (%s), %zu empty Slots\n", bytes.size(), filled, names.c_str(),
			mdDrums::Bank::SlotCount - filled);
		require(filled == 16, "a new Bank does not hold the 16 factory Kits");

		// Two instances: each writes its Slot after the other has read the file; both Slots stay
		mdDrums::Bank other(file, {});
		require(other.read(), "a second Bank cannot read the file");
		const auto a = named(factory[3], "FROM A"), b = named(factory[5], "FROM B");
		require(bank.write(30, a), "the first Bank cannot write");
		require(other.write(31, b), "the second Bank cannot write");
		require(other.slot(30) && *other.slot(30) == a, "the second Bank did not read the file again before writing");
		mdDrums::Bank third(file, {});
		require(third.read() && third.slot(30) && *third.slot(30) == a && third.slot(31) && *third.slot(31) == b,
			"a Slot written by one Bank was lost by the other");
		require(readFile(file).size() == bytes.size(), "a write changed the file's size");
		std::printf("two Banks on one file: Slots 31 \"%s\" and 32 \"%s\" both kept\n", third.slot(30)->displayName().c_str(),
			third.slot(31)->displayName().c_str());

		// A Bank follows a Slot another one wrote, once the file's write time moved, and reads nothing otherwise
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		require(!third.follow(), "a Bank read the file again with nothing written");
		const auto c = named(factory[7], "FROM C");
		require(bank.write(32, c), "the first Bank cannot write");
		require(third.follow() && third.slot(32) && *third.slot(32) == c, "a Bank does not follow another's write");
		require(!third.follow(), "a Bank read the file again twice for one write");
		require(bank.write(32, std::nullopt), "the first Bank cannot empty a Slot");
		std::printf("a Bank follows a Slot another one wrote\n");

		// Emptied, a Slot reads back empty; the file read as a .syx gives its Kits in order, the empty Slots left out
		require(bank.write(30, std::nullopt) && bank.isEmpty(30), "an emptied Slot is not empty");
		require(third.read() && third.isEmpty(30), "an emptied Slot reads back filled");
		const auto kits = mdDrums::Bank::parseSyx(readFile(file));
		require(kits.size() == 17 && kits.front() == factory[0] && kits.back() == b, "the Bank as a .syx gives "
			+ std::to_string(kits.size()) + " Kits");
		std::printf("as a .syx: %zu Kits, the empty Slots left out\n", kits.size());

		// Several Slots in one write, as an import makes them: each in place, with the Slots between and the file's
		// size kept
		require(bank.write({{40, a}, {42, b}}), "the Bank cannot write two Slots at once");
		require(third.read() && third.slot(40) && *third.slot(40) == a && third.isEmpty(41) && third.slot(42)
			&& *third.slot(42) == b && third.slot(31) && readFile(file).size() == bytes.size(),
			"two Slots written at once were not both kept");
		std::printf("one write: Slots 41 \"%s\" and 43 \"%s\", 42 left empty\n", third.slot(40)->displayName().c_str(),
			third.slot(42)->displayName().c_str());

		// The folder goes, a failure to remove it a failure of the test (TempFolder removes it after one too)
		std::filesystem::remove_all(folder.path());
	});
}
