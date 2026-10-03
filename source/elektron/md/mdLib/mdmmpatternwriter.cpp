#include "mdmmpatternwriter.h"

#include <utility>

namespace md
{
	namespace
	{
		// As HostSync: the Monomachine shows its boot logo, ignoring the panel, for about nine seconds
		constexpr uint64_t g_bootSettleFrames = g_samplerate * 10;
	}

	bool MmPatternWriter::start(Write _write)
	{
		if(isBusy())
			return false;
		m_write = std::move(_write);
		m_macro = monomachineReceiveMacro(m_timing.panel);
		m_macroStep = 0;
		m_nextFrames = 0;
		m_phase = Phase::Entering;
		return true;
	}

	uint64_t MmPatternWriter::lineFrames(const size_t _bytes)
	{
		// 31250 bauds, ten bits a byte
		return static_cast<uint64_t>(_bytes) * 10 * g_samplerate / 31250;
	}

	bool MmPatternWriter::playMacro(const uint64_t _frames, const Actions& _actions)
	{
		while(m_macroStep < m_macro.size() && _frames >= m_nextFrames)
		{
			const auto& step = m_macro[m_macroStep];
			if(!_actions.sendPanel || !_actions.sendPanel(step.packet))
				return false;
			m_nextFrames = _frames + step.waitFrames;
			++m_macroStep;
		}
		return m_macroStep >= m_macro.size() && _frames >= m_nextFrames;
	}

	void MmPatternWriter::sendDumps(const uint64_t _frames, const Actions& _actions)
	{
		size_t bytes = 0;
		for(const auto& dump : m_write.dumps)
		{
			if(_actions.sendSysex)
				_actions.sendSysex(dump);
			bytes += dump.size();
		}
		++m_sends;
		m_lineEnd = _frames + lineFrames(bytes);
		m_nextFrames = m_lineEnd + m_timing.resendFrames;
	}

	void MmPatternWriter::service(const uint64_t _frames, const uint64_t _epoch, const Actions& _actions)
	{
		// Another machine: a write half done goes with the old one
		if(m_epoch && (*m_epoch != _epoch || _frames < m_lastFrames))
		{
			if(isBusy())
				m_done = m_write.id;
			m_phase = Phase::Idle;
		}
		m_epoch = _epoch;
		m_lastFrames = _frames;
		// The frames count from the machine's power on
		if(_frames < g_bootSettleFrames)
			return;

		switch(m_phase)
		{
		case Phase::Idle:
			return;
		case Phase::Entering:
			if(!playMacro(_frames, _actions))
				return;
			m_nextFrames = _frames + m_timing.readyFrames;
			m_phase = Phase::Readying;
			[[fallthrough]];
		case Phase::Readying:
			if(_frames < m_nextFrames)
				return;
			// WAITING... on the screen: what changes it now is RECV n MSG.
			m_screen = _actions.screenDigest ? _actions.screenDigest() : 0;
			m_sends = 0;
			sendDumps(_frames, _actions);
			m_phase = Phase::Sending;
			return;
		case Phase::Sending:
			if(_frames < m_lineEnd)
				return;
			if(!_actions.screenDigest)
			{
				m_nextFrames = m_lineEnd + m_timing.storeFrames;
				m_phase = Phase::Storing;
				return;
			}
			if(_actions.screenDigest() == m_screen)
			{
				// Not taken yet: given resendFrames, then sent again, or the menus left all the same
				if(_frames < m_nextFrames)
					return;
				if(m_sends < m_timing.sends)
				{
					sendDumps(_frames, _actions);
					return;
				}
			}
			m_nextFrames = _frames + m_timing.storeFrames;
			m_phase = Phase::Storing;
			return;
		case Phase::Storing:
			if(_frames < m_nextFrames)
				return;
			m_macro = monomachineLeaveMenusMacro(m_timing.panel);
			m_macroStep = 0;
			m_phase = Phase::Leaving;
			[[fallthrough]];
		case Phase::Leaving:
			if(!playMacro(_frames, _actions))
				return;
			for(const auto& message : m_write.after)
			{
				if(_actions.sendSysex)
					_actions.sendSysex(message);
			}
			m_done = m_write.id;
			m_write = {};
			m_macro.clear();
			m_phase = Phase::Idle;
			return;
		}
	}

	uint32_t MmPatternWriteControl::request(std::vector<automation::sysex::Message> _dumps,
		std::vector<automation::sysex::Message> _after)
	{
		const std::lock_guard lock(m_mutex);
		m_waiting.push_back({++m_lastId, std::move(_dumps), std::move(_after)});
		return m_lastId;
	}

	std::optional<MmPatternWriter::Write> MmPatternWriteControl::take()
	{
		const std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock() || m_waiting.empty())
			return std::nullopt;
		auto write = std::move(m_waiting.front());
		m_waiting.pop_front();
		return write;
	}
}
