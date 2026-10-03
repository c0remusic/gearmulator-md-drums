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
	// names, and the dumps are sent once WAITING... shows. Once their time on the MIDI line is over, the
	// page's last line changing (RECV n MSG.) tells the firmware has taken them; a firmware that has not
	// within resendFrames gets them again: just after its sequencer starts, the Monomachine shows WAITING...
	// and drops what comes (mmPatternWriteFirmwareTest). Then the menus are left (monomachineLeaveMenusMacro)
	// and the messages asked for afterwards are sent (a request for the pattern written, to read it back).
	// The sequencer plays on throughout. The owner services it on its rendering thread with the emulated
	// frame count, and forwards what it asks to send, as for HostSync. Nothing starts in a machine's first
	// ten seconds up: the Monomachine ignores its panel behind the boot logo.
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
			// A digest of the LCD's last lines, where the SYSEX RECEIVE page tells WAITING..., then RECV n
			// MSG. once a dump is taken (Hardware::lcdPagesDigest). None: the store time only.
			std::function<uint64_t()> screenDigest;
		};

		// The Monomachine takes its keys in order however short they are: OS 1.32b wrote patterns right with
		// taps of 16 frames, 32 more to redraw, stopped and playing (mmPatternWriteFirmwareTest,
		// MM_WRITE_TIMING_SWEEP). Eight times that: about half a second a write, most of it the Device's
		// blocks, as a step is played a block at most. A full pattern takes the firmware some 50 ms to store.
		struct Timing
		{
			PanelMacroTiming panel{128, 256};
			// From the last key to the dumps sent: WAITING... drawn
			uint64_t readyFrames = g_samplerate / 20;
			// Once the screen tells the dumps were taken, before the menus are left; without a screen, after
			// the dumps' time on the line
			uint64_t storeFrames = g_samplerate / 20;
			// From the dumps' time on the line over to the dumps sent again, the screen unchanged
			uint64_t resendFrames = g_samplerate * 3 / 10;
			// The dumps sent this often at most; then the menus are left all the same
			uint32_t sends = 3;
		};

		explicit MmPatternWriter(const Timing _timing = {}) : m_timing(_timing) {}

		// Takes _write, once idle
		bool start(Write _write);
		bool isBusy() const { return m_phase != Phase::Idle; }
		// The id of the last write done, 0 before any
		uint32_t getDone() const { return m_done; }
		// How often the last write's dumps were sent: more than once when the firmware dropped them
		uint32_t getSends() const { return m_sends; }

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
			Readying,	// readyFrames for WAITING... to show
			Sending,	// the dumps sent, the screen watched for them taken
			Storing,	// the store time
			Leaving		// the leave macro plays
		};

		// Plays the macro's steps that are due; true once all are played and the last wait is over
		bool playMacro(uint64_t _frames, const Actions& _actions);
		void sendDumps(uint64_t _frames, const Actions& _actions);

		const Timing m_timing;
		Phase m_phase = Phase::Idle;
		Write m_write;
		uint32_t m_done = 0;
		std::vector<PanelMacroStep> m_macro;
		size_t m_macroStep = 0;
		uint64_t m_nextFrames = 0;
		uint64_t m_lineEnd = 0;			// the dumps sent last off the MIDI line
		uint32_t m_sends = 0;			// how often the dumps were sent
		uint64_t m_screen = 0;			// the last lines when the dumps were first sent (WAITING...)
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
