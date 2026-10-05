#include "mdChainView.h"

#include "mdController.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <utility>

namespace mdJucePlugin
{
	namespace
	{
		bool samePlaying(const std::optional<md::ChainPlayer::Playing>& _a, const std::optional<md::ChainPlayer::Playing>& _b)
		{
			if(_a.has_value() != _b.has_value())
				return false;
			return !_a || (_a->entry == _b->entry && _a->pass == _b->pass);
		}
	}

	ChainView::ChainView(ChainControl& _chain, Controller& _controller, Rml::Element& _document)
		: m_chain(_chain)
		, m_controller(_controller)
		, m_document(&_document)
	{
		m_root = _document.GetElementById("mdPlayChainPage");
		m_state = _document.GetElementById("mdChainState");
		m_enable = _document.GetElementById("mdChainEnable");
		m_add = _document.GetElementById("mdChainAdd");
		for(size_t slot = 0; slot < SlotCount; ++slot)
		{
			m_slots[slot] = _document.GetElementById("mdChainSlot" + std::to_string(slot));
			if(!m_slots[slot])
				continue;
			juceRmlUi::EventListener::Add(m_slots[slot], Rml::EventId::Click, [this, slot](Rml::Event&)
			{
				if(slot >= m_chain.getEntries().size())
					return;
				m_chosen = slot;
				m_dirty = true;
			});
		}

		bindClick("mdChainEnable", [this] { m_chain.setEnabled(!m_chain.isEnabled()); m_dirty = true; });
		// After the chosen entry, or at the end
		bindClick("mdChainAdd", [this]
		{
			const auto pattern = m_controller.getCurrentPattern();
			if(pattern >= 128)
				return;
			edit([pattern](std::vector<Entry>& _entries, size_t& _chosen)
			{
				if(_entries.size() >= SlotCount)
					return false;
				const auto at = _entries.empty() ? size_t{0} : std::min(_chosen + 1, _entries.size());
				_entries.insert(_entries.begin() + static_cast<std::ptrdiff_t>(at), Entry{pattern, 1});
				_chosen = at;
				return true;
			});
		});
		const auto change = [this](const char* _id, std::function<bool(Entry&)> _change)
		{
			bindClick(_id, [this, _change]
			{
				edit([&_change](std::vector<Entry>& _entries, size_t& _chosen)
				{
					return _chosen < _entries.size() && _change(_entries[_chosen]);
				});
			});
		};
		// The chosen entry's pattern: its bank (A to H) keeping its number, or its number keeping its bank
		for(uint8_t bank = 0; bank < BankCount; ++bank)
		{
			m_banks[bank] = _document.GetElementById("mdChainBank" + std::to_string(bank));
			change(("mdChainBank" + std::to_string(bank)).c_str(), [bank](Entry& _entry)
			{
				const auto pattern = static_cast<uint8_t>(bank * NumberCount + _entry.pattern % NumberCount);
				if(pattern == _entry.pattern)
					return false;
				_entry.pattern = pattern;
				return true;
			});
		}
		for(uint8_t number = 0; number < NumberCount; ++number)
		{
			m_numbers[number] = _document.GetElementById("mdChainNumber" + std::to_string(number));
			change(("mdChainNumber" + std::to_string(number)).c_str(), [number](Entry& _entry)
			{
				const auto pattern = static_cast<uint8_t>(_entry.pattern / NumberCount * NumberCount + number);
				if(pattern == _entry.pattern)
					return false;
				_entry.pattern = pattern;
				return true;
			});
		}
		change("mdChainPassesDown", [](Entry& _entry)
		{
			if(_entry.passes <= 1)
				return false;
			--_entry.passes;
			return true;
		});
		change("mdChainPassesUp", [](Entry& _entry)
		{
			if(_entry.passes >= md::PatternChain::MaxPasses)
				return false;
			++_entry.passes;
			return true;
		});
		bindClick("mdChainMoveLeft", [this]
		{
			edit([](std::vector<Entry>& _entries, size_t& _chosen)
			{
				if(_chosen == 0 || _chosen >= _entries.size())
					return false;
				std::swap(_entries[_chosen], _entries[_chosen - 1]);
				--_chosen;
				return true;
			});
		});
		bindClick("mdChainMoveRight", [this]
		{
			edit([](std::vector<Entry>& _entries, size_t& _chosen)
			{
				if(_chosen + 1 >= _entries.size())
					return false;
				std::swap(_entries[_chosen], _entries[_chosen + 1]);
				++_chosen;
				return true;
			});
		});
		bindClick("mdChainRemove", [this]
		{
			edit([](std::vector<Entry>& _entries, size_t& _chosen)
			{
				if(_chosen >= _entries.size())
					return false;
				_entries.erase(_entries.begin() + static_cast<std::ptrdiff_t>(_chosen));
				if(_chosen > 0 && _chosen >= _entries.size())
					--_chosen;
				return true;
			});
		});
		bindClick("mdChainClear", [this]
		{
			edit([](std::vector<Entry>& _entries, size_t& _chosen)
			{
				if(_entries.empty())
					return false;
				_entries.clear();
				_chosen = 0;
				return true;
			});
		});
	}

	void ChainView::bindClick(const char* const _id, std::function<void()> _action)
	{
		auto* element = m_document->GetElementById(_id);
		if(!element)
			return;
		juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [_action = std::move(_action)](Rml::Event&) { _action(); });
	}

	void ChainView::edit(const std::function<bool(std::vector<Entry>&, size_t&)>& _change)
	{
		auto entries = m_chain.getEntries();
		// Without a chosen entry, the last one
		size_t chosen = m_chosen && *m_chosen < entries.size() ? *m_chosen : (entries.empty() ? 0 : entries.size() - 1);
		if(!_change(entries, chosen) || !m_chain.setEntries(entries))
			return;
		m_chosen = entries.empty() ? std::nullopt : std::optional<size_t>(std::min(chosen, entries.size() - 1));
		m_dirty = true;
	}

	std::string ChainView::patternName(const uint8_t _pattern)
	{
		if(_pattern >= 128)
			return "—";
		std::string name(1, static_cast<char>('A' + _pattern / 16));
		const auto number = _pattern % 16 + 1;
		name += static_cast<char>('0' + number / 10);
		name += static_cast<char>('0' + number % 10);
		return name;
	}

	std::string ChainView::slotText(const Entry& _entry, const std::optional<uint8_t> _length)
	{
		return patternName(_entry.pattern) + " ×" + std::to_string(_entry.passes) + "<br/>"
			+ (_length ? std::to_string(*_length) + " pas" : std::string("…"));
	}

	std::string ChainView::stateLine(const ChainControl::State _state, const std::vector<Entry>& _entries,
		const std::optional<md::ChainPlayer::Playing>& _playing)
	{
		using State = ChainControl::State;
		switch(_state)
		{
		case State::Off:
			return "chaîne inactive";
		case State::Empty:
			return "chaîne vide : AJOUTER prend le pattern de la machine";
		case State::ReadingLengths:
			return "lecture de la longueur des patterns…";
		case State::NotFollowing:
			return "en attente : la machine ne suit pas l'hôte (SYSTÈME, SUIVRE L'HÔTE)";
		case State::Playing:
			break;
		}
		if(!_playing || _playing->entry >= _entries.size())
			return "prête : joue avec le transport de l'hôte";
		const auto& entry = _entries[_playing->entry];
		std::string text = "joue " + patternName(entry.pattern);
		if(entry.passes > 1)
			text += " (passage " + std::to_string(_playing->pass + 1) + "/" + std::to_string(entry.passes) + ")";
		// The next pass: the same entry again, or the next one, looping
		const bool again = _playing->pass + 1 < entry.passes;
		const auto& next = again ? entry : _entries[(_playing->entry + 1) % _entries.size()];
		return text + " · ensuite " + patternName(next.pattern);
	}

	bool ChainView::setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
	{
		if(!_element || _text == _shown)
			return false;
		_shown = _text;
		_element->SetInnerRML(Rml::StringUtilities::EncodeRml(_text));
		return true;
	}

	bool ChainView::update()
	{
		// Only while the tab is shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_dirty = true;
			return false;
		}
		const auto current = m_controller.getCurrentPattern();
		bool changed = setText(m_add, m_shownAdd, current < 128 ? "AJOUTER " + patternName(current) : std::string("AJOUTER"));

		const auto revision = m_chain.getRevision();
		const auto playing = m_chain.getPlaying();
		const auto enabled = m_chain.isEnabled();
		if(!m_dirty && revision == m_shownRevision && samePlaying(playing, m_shownPlaying) && m_shownChosen == m_chosen
			&& m_shownEnabled == static_cast<int>(enabled))
			return changed;
		m_dirty = false;
		m_shownRevision = revision;
		m_shownPlaying = playing;
		m_shownChosen = m_chosen;
		m_shownEnabled = enabled;

		const auto entries = m_chain.getEntries();
		if(m_chosen && *m_chosen >= entries.size())
			m_chosen.reset();
		for(size_t slot = 0; slot < SlotCount; ++slot)
		{
			auto* element = m_slots[slot];
			if(!element)
				continue;
			const bool used = slot < entries.size();
			const auto text = used ? slotText(entries[slot], m_chain.getLength(entries[slot].pattern)) : std::string("—");
			if(text != m_shownSlots[slot])
			{
				m_shownSlots[slot] = text;
				element->SetInnerRML(text);
			}
			element->SetClass("mdEdUnread", !used);
			element->SetClass("mdEdSelected", used && m_chosen == slot);
			element->SetClass("mdChainPlaying", used && playing && playing->entry == slot);
		}
		// The pattern of the entry the selector changes: the chosen one, or the last
		const auto target = m_chosen ? m_chosen : (entries.empty() ? std::nullopt : std::optional<size_t>(entries.size() - 1));
		const auto pattern = target ? std::optional<uint8_t>(entries[*target].pattern) : std::nullopt;
		for(uint8_t bank = 0; bank < BankCount; ++bank)
		{
			if(!m_banks[bank])
				continue;
			m_banks[bank]->SetClass("mdEdSelected", pattern && *pattern / NumberCount == bank);
			m_banks[bank]->SetClass("mdEdUnread", !pattern);
		}
		for(uint8_t number = 0; number < NumberCount; ++number)
		{
			if(!m_numbers[number])
				continue;
			m_numbers[number]->SetClass("mdEdSelected", pattern && *pattern % NumberCount == number);
			m_numbers[number]->SetClass("mdEdUnread", !pattern);
		}
		if(m_enable)
			juceRmlUi::ElemButton::setChecked(m_enable, enabled);
		setText(m_state, m_shownState, stateLine(m_chain.getState(), entries, playing));
		return true;
	}
}
