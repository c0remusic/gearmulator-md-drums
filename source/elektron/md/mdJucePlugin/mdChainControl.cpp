#include "mdChainControl.h"

#include "baseLib/binarystream.h"

namespace mdJucePlugin
{
	ChainControl::ChainControl(const md::MachineModel _model)
		: m_model(_model)
		, m_player(std::make_shared<md::ChainPlayer>(_model))
	{
	}

	bool ChainControl::isEnabled() const
	{
		const std::lock_guard lock(m_mutex);
		return m_enabled;
	}

	void ChainControl::setEnabled(const bool _enabled)
	{
		const std::lock_guard lock(m_mutex);
		if(_enabled == m_enabled)
			return;
		m_enabled = _enabled;
		++m_revision;
		updateLocked(m_following);
	}

	std::vector<ChainControl::Entry> ChainControl::getEntries() const
	{
		const std::lock_guard lock(m_mutex);
		return m_entries;
	}

	bool ChainControl::setEntries(std::vector<Entry> _entries)
	{
		md::PatternChain check;
		if(!check.setEntries(_entries))
			return false;
		const std::lock_guard lock(m_mutex);
		if(_entries == m_entries)
			return true;
		m_entries = std::move(_entries);
		++m_revision;
		updateLocked(m_following);
		return true;
	}

	void ChainControl::setLength(const uint8_t _pattern, const uint8_t _steps)
	{
		if(_pattern >= m_lengths.size() || _steps > 64)
			return;
		if(m_lengths[_pattern].exchange(_steps, std::memory_order_acq_rel) != _steps)
			m_lengthsChanged.fetch_add(1, std::memory_order_acq_rel);
	}

	std::optional<uint8_t> ChainControl::getLength(const uint8_t _pattern) const
	{
		if(_pattern >= m_lengths.size())
			return std::nullopt;
		const auto steps = m_lengths[_pattern].load(std::memory_order_acquire);
		if(!steps)
			return std::nullopt;
		return steps;
	}

	std::optional<uint8_t> ChainControl::getMissingLength() const
	{
		const std::lock_guard lock(m_mutex);
		for(const auto& entry : m_entries)
		{
			if(!getLength(entry.pattern))
				return entry.pattern;
		}
		return std::nullopt;
	}

	ChainControl::State ChainControl::getState() const
	{
		const std::lock_guard lock(m_mutex);
		return m_state;
	}

	uint32_t ChainControl::getRevision() const
	{
		const std::lock_guard lock(m_mutex);
		return m_revision;
	}

	md::PatternChain ChainControl::makeChain() const
	{
		md::PatternChain chain(m_model);
		(void)chain.setEntries(m_entries);
		for(const auto& entry : m_entries)
			chain.setLength(entry.pattern, m_lengths[entry.pattern].load(std::memory_order_acquire));
		return chain;
	}

	ChainControl::State ChainControl::update(const bool _following)
	{
		const std::lock_guard lock(m_mutex);
		return updateLocked(_following);
	}

	ChainControl::State ChainControl::updateLocked(const bool _following)
	{
		m_following = _following;
		const auto lengthsChanged = m_lengthsChanged.load(std::memory_order_acquire);
		if(lengthsChanged != m_lengthsSeen)
		{
			m_lengthsSeen = lengthsChanged;
			++m_revision;
		}

		auto chain = makeChain();
		State state = State::Playing;
		if(!m_enabled)
			state = State::Off;
		else if(m_entries.empty())
			state = State::Empty;
		else if(!chain.isPlayable())
			state = State::ReadingLengths;
		else if(!_following)
			state = State::NotFollowing;

		if(state == State::Playing)
		{
			if(!m_handed || *m_handed != chain)
			{
				m_player->setChain(std::make_shared<const md::PatternChain>(chain));
				m_handed = std::move(chain);
			}
		}
		else if(m_handed)
		{
			m_player->setChain(nullptr);
			m_handed.reset();
		}

		if(state != m_state)
		{
			m_state = state;
			++m_revision;
		}
		return state;
	}

	void ChainControl::save(baseLib::BinaryStream& _stream) const
	{
		const std::lock_guard lock(m_mutex);
		_stream.write(static_cast<uint8_t>(m_enabled ? 1 : 0));
		_stream.write(static_cast<uint8_t>(m_entries.size()));
		for(const auto& entry : m_entries)
		{
			_stream.write(entry.pattern);
			_stream.write(entry.passes);
			_stream.write(m_lengths[entry.pattern].load(std::memory_order_acquire));
		}
	}

	bool ChainControl::load(baseLib::BinaryStream& _stream)
	{
		const auto enabled = _stream.read<uint8_t>();
		const auto count = _stream.read<uint8_t>();
		std::vector<Entry> entries;
		std::vector<std::pair<uint8_t, uint8_t>> lengths;
		for(uint8_t index = 0; index < count; ++index)
		{
			Entry entry;
			entry.pattern = _stream.read<uint8_t>();
			entry.passes = _stream.read<uint8_t>();
			const auto steps = _stream.read<uint8_t>();
			entries.push_back(entry);
			lengths.emplace_back(entry.pattern, steps);
		}
		md::PatternChain check;
		if(enabled > 1 || !check.setEntries(entries))
			return false;
		// The project's patterns come back with the project: their lengths too, until a dump says otherwise
		for(const auto& [pattern, steps] : lengths)
		{
			if(steps)
				setLength(pattern, steps);
		}
		const std::lock_guard lock(m_mutex);
		m_entries = std::move(entries);
		m_enabled = enabled != 0;
		++m_revision;
		updateLocked(m_following);
		return true;
	}
}
