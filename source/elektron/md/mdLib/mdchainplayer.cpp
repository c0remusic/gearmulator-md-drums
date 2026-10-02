#include "mdchainplayer.h"

#include "mdmidiprotocol.h"
#include "mdsysexautomation.h"

namespace md
{
	void ChainPlayer::setChain(std::shared_ptr<const PatternChain> _chain)
	{
		std::atomic_store(&m_chain, std::move(_chain));
	}

	std::shared_ptr<const PatternChain> ChainPlayer::getChain() const
	{
		return std::atomic_load(&m_chain);
	}

	std::optional<ChainPlayer::Playing> ChainPlayer::getPlaying() const
	{
		const auto published = m_published.load(std::memory_order_relaxed);
		if(!published)
			return std::nullopt;
		return Playing{(published >> 8) - 1, static_cast<uint8_t>(published & 0xff)};
	}

	void ChainPlayer::publish(const std::optional<PatternChain::Pass>& _pass)
	{
		m_published.store(_pass ? static_cast<uint32_t>((_pass->entry + 1) << 8 | _pass->pass) : 0, std::memory_order_relaxed);
	}

	void ChainPlayer::select(const uint8_t _pattern, const synthLib::SMidiEvent& _at, std::vector<synthLib::SMidiEvent>& _out)
	{
		const auto body = midiProtocol::selectPattern(m_model, _pattern);
		synthLib::SMidiEvent event(_at.source);
		event.offset = _at.offset;
		event.sysex.push_back(0xf0);
		event.sysex.insert(event.sysex.end(), body.begin(), body.end());
		event.sysex.push_back(0xf7);
		_out.push_back(event);
	}

	std::optional<PatternChain::Pass> ChainPlayer::prepareStart(const PatternChain* _chain, const uint64_t _tick,
		const synthLib::SMidiEvent& _at, std::vector<synthLib::SMidiEvent>& _out)
	{
		const auto pass = _chain ? _chain->passAt(_tick) : std::nullopt;
		if(pass && m_startPattern != pass->pattern)
		{
			select(pass->pattern, _at, _out);
			m_startPattern = pass->pattern;
			m_startFromQueue = false;
		}
		return pass;
	}

	void ChainPlayer::started(const uint64_t _tick)
	{
		m_playing = true;
		m_nextTick = _tick;
		m_current = m_startPattern;
		m_queued.reset();
		m_startFromQueue = false;
	}

	void ChainPlayer::selectedElsewhere(const std::optional<uint8_t> _pattern)
	{
		if(!m_playing)
		{
			m_startPattern = _pattern;
			m_startFromQueue = false;
			return;
		}
		// Queued for a boundary of the machine's own: unknown until the next start
		m_current.reset();
		m_queued.reset();
	}

	void ChainPlayer::process(const synthLib::SMidiEvent& _event, std::vector<synthLib::SMidiEvent>& _out)
	{
		const auto chainShared = std::atomic_load(&m_chain);
		const auto* chain = chainShared && chainShared->isPlayable() ? chainShared.get() : nullptr;

		if(!_event.sysex.empty())
		{
			const auto status = automation::sysex::parseSetStatus(m_model, _event.sysex);
			if(status && status->parameter == automation::sysex::StatusParameter::Pattern)
				selectedElsewhere(status->value);
			_out.push_back(_event);
			return;
		}
		if((_event.a & 0xf0) == synthLib::M_PROGRAMCHANGE)
		{
			selectedElsewhere(std::nullopt);
			_out.push_back(_event);
			return;
		}
		switch(_event.a)
		{
		case synthLib::M_SONGPOSITION:
		{
			m_songPosition = static_cast<uint64_t>(_event.b | _event.c << 7) * PatternChain::TicksPerStep;
			const auto pass = prepareStart(chain, m_songPosition, _event, _out);
			if(!pass)
				break;
			// Into the pattern: the machine takes the position modulo its own length
			const auto steps = (m_songPosition - pass->start) / PatternChain::TicksPerStep;
			auto moved = _event;
			moved.b = static_cast<uint8_t>(steps & 0x7f);
			moved.c = static_cast<uint8_t>(steps >> 7 & 0x7f);
			_out.push_back(moved);
			return;
		}
		case synthLib::M_START:
			m_songPosition = 0;
			prepareStart(chain, 0, _event, _out);
			_out.push_back(_event);
			started(0);
			return;
		case synthLib::M_CONTINUE:
			_out.push_back(_event);
			started(m_songPosition);
			return;
		case synthLib::M_STOP:
			_out.push_back(_event);
			if(m_playing)
			{
				m_playing = false;
				m_startFromQueue = m_queued.has_value();
				m_startPattern = m_queued ? m_queued : m_current;
				m_queued.reset();
				publish(std::nullopt);
			}
			return;
		case synthLib::M_TIMINGCLOCK:
			_out.push_back(_event);
			if(m_playing)
				clock(chain, _event, _out);
			return;
		default:
			break;
		}
		_out.push_back(_event);
	}

	void ChainPlayer::clock(const PatternChain* _chain, const synthLib::SMidiEvent& _event, std::vector<synthLib::SMidiEvent>& _out)
	{
		const auto tick = m_nextTick++;
		if(m_queued && tick >= m_queuedFrom)
		{
			m_current = m_queued;
			m_queued.reset();
		}
		const auto pass = _chain ? _chain->passAt(tick) : std::nullopt;
		publish(pass);
		if(!pass)
			return;
		// What the machine plays after this pass unless asked for another
		const auto next = _chain->next(*pass);
		const auto after = m_queued ? m_queued : m_current;
		if(after == next.pattern || tick > _chain->lastRequestTick(*pass))
			return;
		select(next.pattern, _event, _out);
		m_queued = next.pattern;
		m_queuedFrom = pass->end();
	}

	void ChainPlayer::observe(const synthLib::SMidiEvent& _event)
	{
		if(m_playing || m_startFromQueue || _event.sysex.empty())
			return;
		const auto status = automation::sysex::parseStatusResponse(m_model, _event.sysex);
		if(status && status->parameter == automation::sysex::StatusParameter::Pattern)
			m_startPattern = status->value;
	}
}
