#pragma once

#include "mdpanel.h"
#include "mdsysexautomation.h"
#include "mdtypes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace md
{
	// One front-panel packet of a macro, and the emulated time to let pass before
	// the next one.
	struct PanelMacroStep
	{
		PanelPacket packet;
		uint32_t waitFrames = 0;
	};

	// How long a macro holds a key, then lets the screen redraw, in emulated frames:
	// by default a quick tap and the time a menu takes to redraw.
	struct PanelMacroTiming
	{
		uint32_t holdFrames = 1024;
		uint32_t settleFrames = 2048;
	};

	// Front-panel input that sets the Monomachine's GLOBAL > CONTROL > CONTROL IN
	// page (TEMPO SYNC, TRANSPORT) to _sync. The Monomachine takes Global dumps only
	// in its SysEx receive mode, and a received Global does not replace the active
	// settings, so its own menu is the way in. Every cursor is first walked to a
	// known end, and values stop at the ends of their lists, so the result does not
	// depend on where the menus were left.
	std::vector<PanelMacroStep> monomachineSyncMacro(automation::sysex::GlobalSync _sync);

	// Front-panel input from any screen to the Monomachine's GLOBAL > FILE > SYSEX RECV page, its MODE
	// on ORIG (a dump goes to the slot it names), waiting for dumps: the only way it takes a pattern dump.
	// The page shows RECV n MSG. n ERR. as they come; the sequencer plays on (mmPatternWriteFirmwareTest).
	std::vector<PanelMacroStep> monomachineReceiveMacro(PanelMacroTiming _timing = {});
	// Out of the menus, back to the screen the machine plays on
	std::vector<PanelMacroStep> monomachineLeaveMenusMacro(PanelMacroTiming _timing = {});

	// Keeps a machine's MIDI sync receive settings on what the plug-in's "Follow
	// host tempo" option asks for. The owner feeds it every Global dump of the
	// active slot, services it regularly with the emulated frame count, and
	// forwards what it asks to send. A Machinedrum gets its Global rewritten and
	// reloaded; a Monomachine gets its menu driven (monomachineSyncMacro). Nothing
	// is changed in a machine's first ten seconds up (the Monomachine ignores its
	// panel behind the boot logo), every change is verified on a fresh Global
	// dump, and it is given up after MaxAttempts.
	class HostSync
	{
	public:
		enum class Target : uint8_t
		{
			Leave,		// the machine's setting stays as it is
			Follow,		// clock and transport from the host, kept that way
			Release,	// once: back to the factory setting, then Leave
		};

		enum class State : uint8_t
		{
			Unknown,		// no Global dump seen yet
			Following,		// clock and transport from the host
			NotFollowing,
			Applying,
			Failed,			// the machine did not take the setting
		};

		struct Actions
		{
			std::function<void(const automation::sysex::Message&)> sendSysex;
			// False when the panel input queue is full; the step is retried.
			std::function<bool(const PanelPacket&)> sendPanel;
			std::function<void()> requestGlobal;
		};

		static constexpr uint32_t MaxAttempts = 3;

		explicit HostSync(MachineModel _model);

		void setTarget(Target _target);
		Target getTarget() const { return m_target; }
		State getState() const { return m_state; }
		// Its macro plays into the front panel: nothing else may drive the panel meanwhile
		bool isDrivingPanel() const { return m_phase == Phase::RunningMacro; }

		// A Global dump of the active slot, as the firmware sent it. Copied into
		// storage reserved up front: no allocation on a rendering thread.
		void onGlobalDump(automation::sysex::MessageView _dump);

		// _frames counts emulated frames; _epoch changes when the machine is replaced
		// (a state restore), which drops what was learned about the old one.
		void service(uint64_t _frames, uint64_t _epoch, const Actions& _actions);

		// The setting a target asks for, none for Leave.
		static std::optional<automation::sysex::GlobalSync> wantedSync(MachineModel _model, Target _target);

	private:
		enum class Phase : uint8_t
		{
			Idle,
			RunningMacro,
			Verifying,
		};

		void reset();
		void evaluate(uint64_t _frames, const Actions& _actions);
		void startVerifying(uint64_t _frames);

		const MachineModel m_model;
		Target m_target = Target::Leave;
		State m_state = State::Unknown;
		Phase m_phase = Phase::Idle;

		automation::sysex::Message m_dump;	// empty until a Global was seen
		uint64_t m_dumpCount = 0;
		uint64_t m_nextRequestFrames = 0;
		uint32_t m_attempts = 0;

		std::vector<PanelMacroStep> m_macro;
		size_t m_macroStep = 0;
		uint64_t m_nextStepFrames = 0;

		uint64_t m_verifyAfterDump = 0;
		uint64_t m_verifyRequestFrames = 0;
		uint32_t m_verifyRequests = 0;

		std::optional<uint64_t> m_epoch;
		uint64_t m_lastFrames = 0;
		std::optional<uint64_t> m_upSince;	// frames at the first service of this machine
	};

	// Lock-free link between a plug-in's UI side, which asks for a target, and
	// the Device, which services the HostSync on its rendering thread and
	// publishes the state it reached. A request carries a generation, so asking
	// for Release twice (a one-shot) still reaches the Device twice.
	class HostSyncControl
	{
	public:
		void request(const HostSync::Target _target)
		{
			auto current = m_request.load(std::memory_order_relaxed);
			uint32_t next;
			do
			{
				next = (((current >> 8) + 1) << 8) | static_cast<uint8_t>(_target);
			}
			while(!m_request.compare_exchange_weak(current, next, std::memory_order_acq_rel));
		}

		// Generation in the upper bits, the target in the low byte; 0 before any request.
		uint32_t pendingRequest() const { return m_request.load(std::memory_order_acquire); }
		static HostSync::Target targetOf(const uint32_t _request)
		{
			return static_cast<HostSync::Target>(_request & 0xff);
		}

		void publish(const HostSync::State _state) { m_state.store(_state, std::memory_order_release); }
		HostSync::State getState() const { return m_state.load(std::memory_order_acquire); }

	private:
		std::atomic<uint32_t> m_request{0};
		std::atomic<HostSync::State> m_state{HostSync::State::Unknown};
	};
}
