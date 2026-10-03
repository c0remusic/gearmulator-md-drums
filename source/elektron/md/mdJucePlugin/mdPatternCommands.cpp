#include "mdPatternCommands.h"

#include "mdController.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlMenu.h"

#include "RmlUi/Core/Element.h"

#include <juce_core/juce_core.h>

namespace mdJucePlugin
{
	namespace
	{
		constexpr uint8_t g_maxSteps = 64;

		std::string patternName(const uint8_t _slot)
		{
			const auto number = _slot % 16 + 1;
			return std::string(1, static_cast<char>('A' + _slot / 16)) + (number < 10 ? "0" : "") + std::to_string(number);
		}

		void setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
		{
			if(!_element || _text == _shown)
				return;
			_shown = _text;
			_element->SetInnerRML(_text);
		}

		uint64_t copyKey(const Controller::PatternCopyState& _copy)
		{
			return uint64_t{static_cast<uint8_t>(_copy.state)} | uint64_t{_copy.from} << 8 | uint64_t{_copy.to} << 16
				| uint64_t{_copy.serial} << 24;
		}
	}

	PatternCommands::PatternCommands(Controller& _controller, Rml::Element& _document, const std::string& _prefix,
		std::function<void()> _changed)
		: m_controller(_controller)
		, m_changed(std::move(_changed))
	{
		m_info = _document.GetElementById(_prefix + "Info");
		m_length = _document.GetElementById(_prefix + "Length");
		if(m_length)
		{
			juceRmlUi::EventListener::Add(m_length, Rml::EventId::Click, [this](const Rml::Event& _event)
			{
				openLengthMenu(_event);
			});
		}
		// COPIER VERS: the menu of the slots, or the copy REMPLACER asked to confirm
		m_copy = _document.GetElementById(_prefix + "Copy");
		if(m_copy)
		{
			juceRmlUi::EventListener::Add(m_copy, Rml::EventId::Click, [this](const Rml::Event& _event)
			{
				if(m_copyArmedTo < 128 && juce::Time::getMillisecondCounterHiRes() - m_copyArmedAt <= ConfirmMilliseconds)
				{
					const auto from = m_copyArmedFrom;
					const auto to = m_copyArmedTo;
					disarmCopy();
					m_controller.copyPattern(from, to);
					renderCopy();
					return;
				}
				disarmCopy();
				openCopyMenu(_event);
			});
		}
		// TOUT EFFACER: a first click arms it (CONFIRMER, 3 s), a second one clears the pattern and writes it
		m_clear = _document.GetElementById(_prefix + "Clear");
		if(m_clear)
		{
			juceRmlUi::EventListener::Add(m_clear, Rml::EventId::Click, [this](Rml::Event&)
			{
				clear(juce::Time::getMillisecondCounterHiRes());
			});
		}
	}

	void PatternCommands::show(const std::optional<uint8_t> _slot, const uint8_t _length, const std::string& _line)
	{
		m_slot = _slot;
		m_patternLength = _length;
		m_line = _line;
		setText(m_length, m_shownLength, _slot ? "LONGUEUR " + std::to_string(_length) : std::string("LONGUEUR —"));
		renderCopy();
	}

	bool PatternCommands::update(const double _nowMilliseconds)
	{
		bool changed = false;
		if(m_clearArmedAt >= 0.0 && _nowMilliseconds - m_clearArmedAt > ConfirmMilliseconds)
		{
			disarmClear();
			changed = true;
		}
		if(m_copyArmedTo < 128 && _nowMilliseconds - m_copyArmedAt > ConfirmMilliseconds)
		{
			disarmCopy();
			changed = true;
		}
		const auto copy = copyKey(m_controller.getPatternCopy());
		if(copy != m_shownCopy)
		{
			m_shownCopy = copy;
			renderCopy();
			changed = true;
		}
		return changed;
	}

	void PatternCommands::openLengthMenu(const Rml::Event& _event)
	{
		if(!m_slot)
			return;
		// Two columns of 32; the pattern is written once the choice is made, as a click on a step does
		juceRmlUi::Menu menu;
		for(uint8_t length = 1; length <= g_maxSteps; ++length)
		{
			menu.addEntry(std::to_string(length) + " pas", length == m_patternLength, [this, length]
			{
				if(m_controller.setPatternLength(length))
					m_controller.sendPatternSoon();
				if(m_changed)
					m_changed();
			});
		}
		menu.runModal(_event, 32);
	}

	void PatternCommands::openCopyMenu(const Rml::Event& _event)
	{
		if(!m_slot)
			return;
		// A column per bank, A to H, what the library knows of each slot; the pattern's own off
		juceRmlUi::Menu menu;
		for(uint8_t slot = 0; slot < Controller::PatternLibrarySize; ++slot)
		{
			auto label = patternName(slot);
			const auto stored = m_controller.getLibraryPattern(slot);
			if(stored && stored->read && stored->trigs)
				label += *stored->trigs ? " · " + std::to_string(*stored->trigs) + (*stored->trigs == 1 ? " trig" : " trigs") : " · vide";
			menu.addEntry(label, slot != *m_slot, slot == *m_slot, [this, slot] { copyTo(slot); });
		}
		menu.runModal(_event, 16);
	}

	void PatternCommands::copyTo(const uint8_t _slot)
	{
		if(!m_slot || _slot >= Controller::PatternLibrarySize || _slot == *m_slot)
			return;
		const auto stored = m_controller.getLibraryPattern(_slot);
		if(stored && stored->read && stored->trigs == uint16_t{0})
		{
			disarmCopy();
			m_controller.copyPattern(*m_slot, _slot);
			renderCopy();
			return;
		}
		m_copyArmedFrom = *m_slot;
		m_copyArmedTo = _slot;
		m_copyArmedAt = juce::Time::getMillisecondCounterHiRes();
		renderCopy();
	}

	void PatternCommands::clear(const double _nowMilliseconds)
	{
		if(m_clearArmedAt < 0.0 || _nowMilliseconds - m_clearArmedAt > ConfirmMilliseconds)
		{
			m_clearArmedAt = _nowMilliseconds;
			if(m_clear)
			{
				m_clear->SetInnerRML("CONFIRMER ?");
				m_clear->SetClass("mdPlayArmed", true);
			}
			return;
		}
		disarmClear();
		if(m_controller.clearPattern())
			m_controller.sendPattern();
		if(m_changed)
			m_changed();
	}

	void PatternCommands::disarmClear()
	{
		m_clearArmedAt = -1.0;
		if(m_clear)
		{
			m_clear->SetInnerRML("TOUT EFFACER");
			m_clear->SetClass("mdPlayArmed", false);
		}
	}

	void PatternCommands::disarmCopy()
	{
		if(m_copyArmedTo >= 128)
			return;
		m_copyArmedTo = 0xff;
		renderCopy();
	}

	void PatternCommands::renderCopy()
	{
		const auto copy = m_controller.getPatternCopy();
		const bool copying = copy.state == Controller::PatternCopy::Reading || copy.state == Controller::PatternCopy::Writing;
		if(m_copy)
		{
			setText(m_copy, m_shownCopyLabel, m_copyArmedTo < 128 ? "REMPLACER " + patternName(m_copyArmedTo) + " ?"
				: copying ? std::string("COPIE…") : std::string("COPIER VERS…"));
			m_copy->SetClass("mdPlayArmed", m_copyArmedTo < 128);
			m_copy->SetClass("mdEdOff", !m_slot || copying);
		}
		// The line tells how the copy of the pattern shown goes
		auto text = m_line;
		if(m_slot && copy.state != Controller::PatternCopy::None && copy.from == *m_slot)
		{
			const auto to = patternName(copy.to);
			switch(copy.state)
			{
			case Controller::PatternCopy::Reading:
			case Controller::PatternCopy::Writing: text += " · copie vers " + to + "…"; break;
			case Controller::PatternCopy::Copied: text += " · copié sur " + to; break;
			case Controller::PatternCopy::Refused: text += " · copie sur " + to + " refusée"; break;
			case Controller::PatternCopy::Failed: text += " · copie sur " + to + " sans réponse"; break;
			default: break;
			}
		}
		setText(m_info, m_shownInfo, text);
	}
}
