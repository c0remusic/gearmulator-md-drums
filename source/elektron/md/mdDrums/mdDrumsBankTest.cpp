// The Kits' Bank (ticket 26 of the editor map): a new file holds the 16 factory Kits in Slots 1-16, Kit 1 the one a
// Machinedrum boots with, the other 48 empty; each Slot is a Kit dump OS 1.63 takes, for its slot; a write keeps the
// file's size and the other Slots; two Banks on one file keep each other's Slots; an emptied Slot reads back empty; a
// .syx file's Kits come out in their order, the empty Slots left out.

#include "mdDrumsBank.h"
#include "mdDrumsEngine.h"
#include "mdDrumsMessages.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	using Kit = md::automation::sysex::MdKit;

	std::vector<uint8_t> readFile(const std::filesystem::path& _file)
	{
		std::ifstream in(_file, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	Kit named(Kit _kit, const char* _name)
	{
		_kit.name.fill(0);
		for(size_t i = 0; _name[i] && i < _kit.name.size(); ++i)
			_kit.name[i] = static_cast<uint8_t>(_name[i]);
		return _kit;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsBankTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	const auto folder = std::filesystem::temp_directory_path()
		/ ("mdDrumsBankTest_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try
	{
		const auto factory = mdDrums::messages::factoryKits(flash);
		const auto kit1 = mdDrums::messages::factoryKit(flash);
		require(factory.size() == 16, "the flash image holds " + std::to_string(factory.size()) + " factory Kits");
		require(kit1 && factory[0] == *kit1, "Slot 1 is not the Kit a Machinedrum boots with");

		const auto file = folder / "Bank.syx";
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

		// Emptied, a Slot reads back empty; the file read as a .syx gives its Kits in order, the empty Slots left out
		require(bank.write(30, std::nullopt) && bank.isEmpty(30), "an emptied Slot is not empty");
		require(third.read() && third.isEmpty(30), "an emptied Slot reads back filled");
		const auto kits = mdDrums::Bank::parseSyx(readFile(file));
		require(kits.size() == 17 && kits.front() == factory[0] && kits.back() == b, "the Bank as a .syx gives "
			+ std::to_string(kits.size()) + " Kits");
		std::printf("as a .syx: %zu Kits, the empty Slots left out\n", kits.size());

		std::filesystem::remove_all(folder);
		std::cout << "mdDrumsBankTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::error_code error;
		std::filesystem::remove_all(folder, error);
		std::cerr << "mdDrumsBankTest: " << _error.what() << '\n';
		return 1;
	}
}
