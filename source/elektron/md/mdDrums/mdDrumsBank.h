#pragma once

#include "mdProtocol/mdkit.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace mdDrums
{
	// The Kits' Bank: 64 Slots in one file, shared by every set and every instance of the plug-in. Slot n is a Kit dump
	// ($52, MdKitDumpSize bytes) for slot n at n times that size, so that the file is a .syx that a Machinedrum and
	// Machinemodule read (.scratch/md-drums-editor/research/04-kits.md); an empty Slot is a dump whose name starts with
	// $7f, as OS 1.63 sends one. A missing file starts with the factory Kits from Slot 1 on. The file is read again
	// before each write, and a write touches its own Slot's bytes only, so that two instances keep each other's Slots
	// (ticket 10 of the editor map). Without JUCE, for any thread but one at a time.
	class Bank
	{
	public:
		using Kit = md::automation::sysex::MdKit;
		static constexpr int SlotCount = 64;
		static constexpr size_t SlotSize = md::automation::sysex::MdKitDumpSize;

		Bank(std::filesystem::path _file, std::vector<Kit> _factory);

		// Reads the file again, writing it first with the factory Kits when it does not exist. False when it can be
		// neither read nor written (the Slots are then all empty).
		bool read();

		// The Kit in a Slot (0-63); empty for an empty Slot and out of range
		const std::optional<Kit>& slot(int _slot) const;
		bool isEmpty(int _slot) const { return !slot(_slot).has_value(); }

		// Writes a Slot, an empty one for no Kit, after reading the file again. False when the file cannot be written.
		bool write(int _slot, const std::optional<Kit>& _kit);

		const std::filesystem::path& file() const { return m_file; }

		// A Slot's dump: the Kit's for slot _slot, or an empty Slot's
		static std::vector<uint8_t> dump(const std::optional<Kit>& _kit, int _slot);
		// The Kits in a .syx file's bytes, in their order: every Kit dump OS 1.63 takes, the empty Slots' left out
		static std::vector<Kit> parseSyx(const std::vector<uint8_t>& _bytes);

	private:
		std::filesystem::path m_file;
		std::vector<Kit> m_factory;
		std::array<std::optional<Kit>, SlotCount> m_slots{};
	};
}
