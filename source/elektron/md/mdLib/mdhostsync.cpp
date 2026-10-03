#include "mdhostsync.h"

#include <utility>

namespace md
{
	namespace
	{
		using automation::sysex::GlobalSync;

		// Nothing is changed before the machine has been up this long. The
		// Monomachine answers SysEx a second after power-on but shows its boot
		// logo, ignoring the front panel, for about eight more seconds (measured
		// on OS 1.32b); this leaves two seconds of margin.
		constexpr uint64_t g_bootSettleFrames = g_samplerate * 10;

		// A verification request waits this long after a change, so that answers to
		// requests sent before the change have come back first.
		constexpr uint64_t g_verifyDelayFrames = g_samplerate / 2;
		constexpr uint64_t g_verifyRetryFrames = g_samplerate * 2;
		constexpr uint32_t g_maxVerifyRequests = 3;

		class MacroBuilder
		{
		public:
			explicit MacroBuilder(const MachineModel _model, const PanelMacroTiming _timing = {})
				: m_model(_model), m_timing(_timing)
			{
			}

			MacroBuilder& tap(const PanelControl _control, const uint32_t _count = 1)
			{
				const auto packet = panelPacket(m_model, _control);
				for(uint32_t i = 0; packet && i < _count; ++i)
				{
					m_steps.push_back({m_rows.press(*packet), m_timing.holdFrames});
					m_steps.push_back({m_rows.release(*packet), m_timing.settleFrames});
				}
				return *this;
			}

			MacroBuilder& chord(const PanelControl _held, const PanelControl _control)
			{
				const auto held = panelPacket(m_model, _held);
				const auto packet = panelPacket(m_model, _control);
				if(!held || !packet)
					return *this;
				m_steps.push_back({m_rows.press(*held), m_timing.holdFrames});
				m_steps.push_back({m_rows.press(*packet), m_timing.holdFrames});
				m_steps.push_back({m_rows.release(*packet), m_timing.holdFrames});
				m_steps.push_back({m_rows.release(*held), m_timing.settleFrames});
				return *this;
			}

			std::vector<PanelMacroStep> take() { return std::move(m_steps); }

		private:
			const MachineModel m_model;
			const PanelMacroTiming m_timing;
			PanelRowState m_rows;
			std::vector<PanelMacroStep> m_steps;
		};
	}

	std::vector<PanelMacroStep> monomachineSyncMacro(const GlobalSync _sync)
	{
		using C = PanelControl;
		MacroBuilder macro(MachineModel::Monomachine);
		macro.tap(C::Exit, 4)							// out of any menu
			.chord(C::Function, C::Kit)					// GLOBAL, on the active slot
			.tap(C::Enter)								// GLOBAL n EDIT
			.tap(C::Left).tap(C::Up, 4)					// first category: AUDIO
			.tap(C::Down)								// CONTROL
			.tap(C::Right).tap(C::Up, 6)				// its first item: MIDI CHANLS
			.tap(C::Down, 3)							// CONTROL IN
			.tap(C::Enter)
			.tap(C::Up, 3)								// TEMPO SYNC
			.tap(_sync.clockIn ? C::Right : C::Left, 2)		// EXT MIDI CLK or INTERNAL
			.tap(C::Down)								// TRANSPORT
			.tap(_sync.transportIn ? C::Right : C::Left, 2)	// ACCEPT or IGNORE
			.tap(C::Exit, 4);
		return macro.take();
	}

	std::vector<PanelMacroStep> monomachineReceiveMacro(const PanelMacroTiming _timing)
	{
		using C = PanelControl;
		MacroBuilder macro(MachineModel::Monomachine, _timing);
		macro.tap(C::Exit, 4)							// out of any menu
			.chord(C::Function, C::Kit)					// GLOBAL, on the active slot
			.tap(C::Enter)								// GLOBAL n EDIT
			.tap(C::Left).tap(C::Up, 4)					// first category: AUDIO
			.tap(C::Down, 2)							// FILE
			.tap(C::Right).tap(C::Up, 3)				// its first item: SYSEX SEND
			.tap(C::Down)								// SYSEX RECV
			.tap(C::Enter)								// the SYSEX RECEIVE page
			.tap(C::Up, 3)								// MODE: ORIG, the first of ORIG, SPEC, VERF
			.tap(C::Right).tap(C::Enter);				// WAITING...
		return macro.take();
	}

	std::vector<PanelMacroStep> monomachineLeaveMenusMacro(const PanelMacroTiming _timing)
	{
		MacroBuilder macro(MachineModel::Monomachine, _timing);
		macro.tap(PanelControl::Exit, 4);
		return macro.take();
	}

	HostSync::HostSync(const MachineModel _model) : m_model(_model)
	{
		// A Machinedrum Global is 197 bytes, a Monomachine one about 110.
		m_dump.reserve(512);
	}

	std::optional<GlobalSync> HostSync::wantedSync(const MachineModel _model, const Target _target)
	{
		switch(_target)
		{
		case Target::Follow:
			return GlobalSync{true, true};
		case Target::Release:
			// Factory settings: the Machinedrum ships with CTRL IN on, the
			// Monomachine with TRANSPORT set to IGNORE.
			return GlobalSync{false, _model == MachineModel::Machinedrum};
		case Target::Leave:
			break;
		}
		return std::nullopt;
	}

	void HostSync::setTarget(const Target _target)
	{
		if(_target == m_target)
			return;
		m_target = _target;
		m_attempts = 0;
		if(m_state == State::Failed)
			m_state = m_dump.empty() ? State::Unknown : State::NotFollowing;
	}

	void HostSync::onGlobalDump(const automation::sysex::MessageView _dump)
	{
		if(!automation::sysex::parseGlobalSync(m_model, _dump))
			return;
		m_dump.assign(_dump.begin(), _dump.end());
		++m_dumpCount;
	}

	void HostSync::reset()
	{
		m_state = State::Unknown;
		m_phase = Phase::Idle;
		m_dump.clear();
		m_attempts = 0;
		m_macro.clear();
		m_macroStep = 0;
		m_nextRequestFrames = 0;
		m_upSince.reset();
	}

	void HostSync::startVerifying(const uint64_t _frames)
	{
		m_phase = Phase::Verifying;
		m_verifyAfterDump = ~uint64_t{0};
		m_verifyRequestFrames = _frames + g_verifyDelayFrames;
		m_verifyRequests = 0;
	}

	void HostSync::service(const uint64_t _frames, const uint64_t _epoch, const Actions& _actions)
	{
		// A replaced machine (state restore) must be read again before anything is
		// changed on it; a macro half played into the old one is abandoned.
		if(m_epoch && (*m_epoch != _epoch || _frames < m_lastFrames))
			reset();
		m_epoch = _epoch;
		m_lastFrames = _frames;
		if(!m_upSince)
			m_upSince = _frames;

		switch(m_phase)
		{
		case Phase::RunningMacro:
			while(m_macroStep < m_macro.size() && _frames >= m_nextStepFrames)
			{
				const auto& step = m_macro[m_macroStep];
				if(!_actions.sendPanel || !_actions.sendPanel(step.packet))
					return;
				m_nextStepFrames = _frames + step.waitFrames;
				++m_macroStep;
			}
			if(m_macroStep < m_macro.size() || _frames < m_nextStepFrames)
				return;
			m_macro.clear();
			startVerifying(_frames);
			return;
		case Phase::Verifying:
			if(m_dumpCount > m_verifyAfterDump)
			{
				m_phase = Phase::Idle;
				break;
			}
			if(_frames < m_verifyRequestFrames)
				return;
			if(m_verifyRequests >= g_maxVerifyRequests)
			{
				// No answer: count the attempt as failed and let evaluate() decide.
				m_phase = Phase::Idle;
				break;
			}
			// Only dumps that arrive after this request can show the change.
			m_verifyAfterDump = m_dumpCount;
			if(_actions.requestGlobal)
				_actions.requestGlobal();
			++m_verifyRequests;
			m_verifyRequestFrames = _frames + g_verifyRetryFrames;
			return;
		case Phase::Idle:
			break;
		}
		evaluate(_frames, _actions);
	}

	void HostSync::evaluate(const uint64_t _frames, const Actions& _actions)
	{
		const auto wanted = wantedSync(m_model, m_target);
		if(m_dump.empty())
		{
			m_state = State::Unknown;
			if(wanted && _actions.requestGlobal && _frames >= m_nextRequestFrames)
			{
				_actions.requestGlobal();
				m_nextRequestFrames = _frames + g_verifyRetryFrames;
			}
			return;
		}
		const auto current = automation::sysex::parseGlobalSync(m_model, m_dump);
		if(!wanted || *current == *wanted)
		{
			m_state = *current == GlobalSync{true, true} ? State::Following : State::NotFollowing;
			m_attempts = 0;
			if(m_target == Target::Release)
				m_target = Target::Leave;
			return;
		}
		if(m_attempts >= MaxAttempts)
		{
			m_state = State::Failed;
			return;
		}
		m_state = State::Applying;
		if(_frames < *m_upSince + g_bootSettleFrames)
			return;
		++m_attempts;

		if(m_model == MachineModel::Machinedrum)
		{
			const auto patched = automation::sysex::withGlobalSync(m_model, m_dump, *wanted);
			if(!patched || !_actions.sendSysex)
			{
				m_state = State::Failed;
				return;
			}
			_actions.sendSysex(*patched);
			_actions.sendSysex(automation::sysex::globalReload(m_model, m_dump[9]));
			startVerifying(_frames);
			return;
		}

		m_macro = monomachineSyncMacro(*wanted);
		m_macroStep = 0;
		m_nextStepFrames = _frames;
		m_phase = Phase::RunningMacro;
	}
}
