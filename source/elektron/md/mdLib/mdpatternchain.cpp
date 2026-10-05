#include "mdpatternchain.h"

#include <algorithm>

namespace md
{
	bool PatternChain::setEntries(std::vector<Entry> _entries)
	{
		const bool valid = std::all_of(_entries.begin(), _entries.end(), [](const Entry& _entry)
		{
			return _entry.pattern < 128 && _entry.passes >= 1 && _entry.passes <= MaxPasses;
		});
		if(!valid)
			return false;
		m_entries = std::move(_entries);
		return true;
	}

	void PatternChain::setLength(const uint8_t _pattern, const uint8_t _steps)
	{
		if(_pattern < 128 && _steps <= 64)
			m_lengths[_pattern] = _steps;
	}

	std::optional<uint8_t> PatternChain::getLength(const uint8_t _pattern) const
	{
		if(_pattern >= 128 || !m_lengths[_pattern])
			return std::nullopt;
		return m_lengths[_pattern];
	}

	bool PatternChain::isPlayable() const
	{
		return !m_entries.empty() && std::all_of(m_entries.begin(), m_entries.end(), [this](const Entry& _entry)
		{
			return m_lengths[_entry.pattern] != 0;
		});
	}

	uint64_t PatternChain::roundTicks() const
	{
		if(!isPlayable())
			return 0;
		uint64_t ticks = 0;
		for(const auto& entry : m_entries)
			ticks += static_cast<uint64_t>(passTicks(entry.pattern)) * entry.passes;
		return ticks;
	}

	std::optional<PatternChain::Pass> PatternChain::passAt(const uint64_t _tick) const
	{
		const auto round = roundTicks();
		if(!round)
			return std::nullopt;
		const auto roundStart = _tick - _tick % round;
		auto entryStart = roundStart;
		for(size_t index = 0; index < m_entries.size(); ++index)
		{
			const auto& entry = m_entries[index];
			const auto ticks = passTicks(entry.pattern);
			const auto entryTicks = static_cast<uint64_t>(ticks) * entry.passes;
			if(_tick < entryStart + entryTicks)
			{
				const auto pass = static_cast<uint8_t>((_tick - entryStart) / ticks);
				return Pass{index, pass, entryStart + static_cast<uint64_t>(pass) * ticks, ticks, entry.pattern};
			}
			entryStart += entryTicks;
		}
		return std::nullopt;
	}

	PatternChain::Pass PatternChain::next(const Pass& _pass) const
	{
		const auto& entry = m_entries[_pass.entry];
		if(_pass.pass + 1 < entry.passes)
			return Pass{_pass.entry, static_cast<uint8_t>(_pass.pass + 1), _pass.end(), _pass.ticks, _pass.pattern};
		const auto index = (_pass.entry + 1) % m_entries.size();
		const auto pattern = m_entries[index].pattern;
		return Pass{index, 0, _pass.end(), passTicks(pattern), pattern};
	}
}
