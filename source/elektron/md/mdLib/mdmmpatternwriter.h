#pragma once

#include "mdhostsync.h"
#include "mdsysexautomation.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace md
{
	// Writes dumps to a Monomachine, which takes a pattern dump only on its GLOBAL > FILE > SYSEX RECV page:
	// the menu is driven there in ORIG mode (monomachineReceiveMacro), where a dump goes to the slot it
	// names, the dumps are sent and given the MIDI line's time, the menus are left
	// (monomachineLeaveMenusMacro), then the messages asked for afterwards are sent (a request for the
	// pattern written, to read it back). The sequencer plays on throughout. The owner services it on its
	// rendering thread with the emulated frame count, and forwards what it asks to send, as for HostSync.
	// Nothing starts in a machine's first ten seconds up: the Monomachine ignores its panel behind the
	// boot logo.
	class MmPatternWriter
	{
	public:
		struct Write
		{
			uint32_t id = 0;
			std::vector<automation::sysex::Message> dumps;	// received on the SYSEX RECEIVE page
			std::vector<automation::sysex::Message> after;	// sent once the menus are left
		};

		struct Actions
		{
			std::function<void(const automation::sysex::Message&)> sendSysex;
			// False when the panel input queue is full; the step is retried.
			std::function<bool(const PanelPacket&)> sendPanel;
		};

		// Takes _write, once idle
		bool start(Write _write);
		bool isBusy() const { return m_phase != Phase::Idle; }
		// The id of the last write done, 0 before any
		uint32_t getDone() const { return m_done; }

		// _frames counts emulated frames from the machine's power on; _epoch changes when the machine is
		// replaced (a state restore), which abandons a write half done.
		void service(uint64_t _frames, uint64_t _epoch, const Actions& _actions);

		// The time a dump of _bytes takes on the MIDI line, in frames
		static uint64_t lineFrames(size_t _bytes);

	private:
		enum class Phase : uint8_t
		{
			Idle,
			Entering,	// the receive macro plays
			Receiving,	// the dumps sent, the MIDI line's time given
			Leaving		// the leave macro plays
		};

		// Plays the macro's steps that are due; true once all are played and the last wait is over
		bool playMacro(uint64_t _frames, const Actions& _actions);

		Phase m_phase = Phase::Idle;
		Write m_write;
		uint32_t m_done = 0;
		std::vector<PanelMacroStep> m_macro;
		size_t m_macroStep = 0;
		uint64_t m_nextFrames = 0;
		std::optional<uint64_t> m_epoch;
		uint64_t m_lastFrames = 0;
	};

	// Lock-free for the rendering thread: the plug-in's controller queues writes, the Device takes them
	// one at a time (never waiting for the lock) and publishes the last one done.
	class MmPatternWriteControl
	{
	public:
		// Queues a write; returns its id, which getDone() reaches once the write is done
		uint32_t request(std::vector<automation::sysex::Message> _dumps, std::vector<automation::sysex::Message> _after);
		// The oldest write waiting, unless the lock is taken (the next call tries again)
		std::optional<MmPatternWriter::Write> take();

		void publishDone(const uint32_t _id) { m_done.store(_id, std::memory_order_release); }
		uint32_t getDone() const { return m_done.load(std::memory_order_acquire); }

	private:
		std::mutex m_mutex;
		std::deque<MmPatternWriter::Write> m_waiting;	// under m_mutex
		uint32_t m_lastId = 0;							// under m_mutex
		std::atomic<uint32_t> m_done{0};
	};
}
