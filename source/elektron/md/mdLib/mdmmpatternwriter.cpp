#include "mdmmpatternwriter.h"

#include <utility>

namespace md
{
	namespace
	{
		// As HostSync: the Monomachine shows its boot logo, ignoring the panel, for about nine seconds
		constexpr uint64_t g_bootSettleFrames = g_samplerate * 10;
		// After the dumps' own time on the line, for the firmware to store them
		constexpr uint64_t g_storeFrames = g_samplerate / 2;
	}

	bool MmPatternWriter::start(Write _write)
	{
		if(isBusy())
			return false;
		m_write = std::move(_write);
		m_macro = monomachineReceiveMacro();
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
		{
			if(!playMacro(_frames, _actions))
				return;
			size_t bytes = 0;
			for(const auto& dump : m_write.dumps)
			{
				if(_actions.sendSysex)
					_actions.sendSysex(dump);
				bytes += dump.size();
			}
			m_nextFrames = _frames + lineFrames(bytes) + g_storeFrames;
			m_phase = Phase::Receiving;
			return;
		}
		case Phase::Receiving:
			if(_frames < m_nextFrames)
				return;
			m_macro = monomachineLeaveMenusMacro();
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
