#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace md
{
	// The stored Kits and patterns as BIBLIO lists them, read from the machine's RAM (Hardware::readLibrary)
	// instead of asked for one dump at a time: 192 dumps keep the Machinedrum's MIDI line busy for about four
	// minutes, the Monomachine's for one.
	struct Library
	{
		static constexpr size_t MaxKits = 128;
		static constexpr size_t PatternCount = 128;
		static constexpr size_t MaxTracks = 16;
		static constexpr size_t MaxNameSize = 16;

		struct Kit
		{
			std::array<uint8_t, MaxNameSize> name{};		// as stored: name() makes it text
			std::array<uint16_t, MaxTracks> machines{};		// md::machines id per track
		};
		struct Pattern
		{
			uint8_t length = 0;		// 1 to 64; 0 for bytes no pattern dump would hold
			uint8_t kit = 0;
			uint16_t trigs = 0;		// within the length
		};

		uint64_t frame = 0;		// the machine's emulated frame when it was read (Hardware::getEmulatedFrames)
		uint8_t kitCount = 0;
		uint8_t tracks = 0;
		uint8_t nameSize = 0;
		std::array<Kit, MaxKits> kits{};
		std::array<Pattern, PatternCount> patterns{};

		// A Kit's name as parseKitDump gives it: up to its first zero, bytes outside printable ASCII as '?', trailing
		// spaces removed
		std::string name(uint8_t _kit) const;
	};

	// A Library asked for from any thread and read on the Device's rendering thread between two blocks, with the
	// firmware paused. The rendering thread never waits: while a reader copies the last reading, it reads again
	// at the next block.
	class LibraryControl
	{
	public:
		// Asks for a reading; returns its id, which getReadId() reaches once it is read
		uint32_t request() { return m_requested.fetch_add(1, std::memory_order_acq_rel) + 1; }
		uint32_t getRequested() const { return m_requested.load(std::memory_order_acquire); }

		// The rendering thread: false, and nothing kept, while a reader copies the last reading
		bool publish(uint32_t _id, const Library& _library);

		uint32_t getReadId() const { return m_readId.load(std::memory_order_acquire); }
		// The last reading, and its id in _id (0 before the first)
		Library read(uint32_t& _id) const;

	private:
		mutable std::mutex m_mutex;
		Library m_library;		// under m_mutex
		std::atomic<uint32_t> m_requested{0};
		std::atomic<uint32_t> m_readId{0};
	};
}
