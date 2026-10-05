#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "mdsysexautomation.h"

namespace md
{
	// A byte written into the machine's RAM between two of the firmware's instructions, where it holds
	// _expected (any value without)
	struct RamWrite
	{
		uint32_t address = 0;
		uint8_t value = 0;
		std::optional<uint8_t> expected;
	};

	// Where the Machinedrum stores its 128 patterns, each laid out as a pattern dump's ($67) unpacked payload in
	// three blocks, A01's first and every next pattern's right after it (mdPlayheadProbe --pattern-ram,
	// --pattern-ext, --library-ram). The machine plays the selected pattern from there: what is written plays
	// from the next step, stays when another pattern is selected, and is what a pattern request answers with. A
	// grid edit, without the reload of the pattern and its Kit a dump costs.
	struct LivePatternLayout
	{
		static constexpr uint32_t MainSize = 2198;		// trigs, lock masks, accent/slide/swing, plain bytes, lock rows
		static constexpr uint32_t TailSize = 204;		// EDIT ALL words, the tracks' own accent, slide and swing
		static constexpr uint32_t ExtensionSize = 2316;	// steps 33 to 64 of all of the above
		static constexpr uint8_t PatternCount = 128;

		// A01's blocks
		uint32_t main = 0;
		uint32_t tail = 0;
		uint32_t extension = 0;

		// Pattern _slot's blocks, 0 (A01) to 127 (H16)
		uint32_t mainOf(const uint8_t _slot) const { return main + _slot * MainSize; }
		uint32_t tailOf(const uint8_t _slot) const { return tail + _slot * TailSize; }
		uint32_t extensionOf(const uint8_t _slot) const { return extension + _slot * ExtensionSize; }
	};

	// The payload of a Machinedrum pattern dump, unpacked: MainSize bytes, TailSize, then ExtensionSize for the
	// 64-step form; nullopt for anything else
	std::optional<std::vector<uint8_t>> unpackMdPatternPayload(automation::sysex::MessageView _dump);

	// The bytes that make the stored pattern _to where it holds _from, two dumps of the same pattern: only those
	// that differ, at that pattern's blocks, each expected to hold _from's value (an edit made on the machine
	// meanwhile elsewhere in the pattern stays). A _from without steps 33 to 64 has them all written, whatever
	// they hold. Nullopt when either is not a Machinedrum pattern dump, or they name different patterns.
	std::optional<std::vector<RamWrite>> livePatternWrites(const LivePatternLayout& _layout,
		automation::sysex::MessageView _from, automation::sysex::MessageView _to);

	// Lock-free for the rendering thread, as MmPatternWriteControl: the plug-in's controller queues edits of
	// the pattern the machine plays, the Device takes them (never waiting for the lock) and publishes the last
	// one taken, and the last one refused: its bytes no longer held _from's values (the machine's pattern
	// changed meanwhile), so none was written.
	class LivePatternControl
	{
	public:
		struct Edit
		{
			uint32_t id = 0;
			automation::sysex::Message from;
			automation::sysex::Message to;
		};

		// Queues an edit; returns its id, which getDone() reaches once the RAM is written
		uint32_t request(automation::sysex::Message _from, automation::sysex::Message _to);
		// The oldest edit waiting, unless the lock is taken (the next call tries again)
		std::optional<Edit> take();

		// A refused edit is published refused before done
		void publishDone(const uint32_t _id) { m_done.store(_id, std::memory_order_release); }
		void publishRefused(const uint32_t _id) { m_refused.store(_id, std::memory_order_release); }
		uint32_t getDone() const { return m_done.load(std::memory_order_acquire); }
		uint32_t getRefused() const { return m_refused.load(std::memory_order_acquire); }

	private:
		std::mutex m_mutex;
		std::deque<Edit> m_waiting;		// under m_mutex
		uint32_t m_lastId = 0;			// under m_mutex
		std::atomic<uint32_t> m_done{0};
		std::atomic<uint32_t> m_refused{0};
	};
}
