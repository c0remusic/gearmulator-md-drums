#include "mdStepGrid.h"

#include "mdController.h"

#include "juceRmlUi/rmlElemValue.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace mdJucePlugin
{
	namespace
	{
		constexpr double g_retryMilliseconds = 3000.0;
		// A wheel turn has no end: its lock is sent once it pauses this long.
		constexpr double g_sendDelayMilliseconds = 300.0;

		// Pattern slot 0..127 as the Machinedrum shows it: banks A to H of 16.
		std::string patternName(const uint8_t _slot)
		{
			const auto number = _slot % 16 + 1;
			return std::string(1, static_cast<char>('A' + _slot / 16)) + (number < 10 ? "0" : "") + std::to_string(number);
		}

		std::string attribute(Rml::Element& _element, const char* _name)
		{
			const auto* value = _element.GetAttribute(_name);
			return value ? value->Get<Rml::String>(_element.GetCoreInstance()) : std::string();
		}

		// Text and display are written only when they change: each write lays the whole document out
		void setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
		{
			if(!_element || _text == _shown)
				return;
			_shown = _text;
			_element->SetInnerRML(_text);
		}

		void setDisplayed(Rml::Element* _element, bool& _shown, const bool _displayed)
		{
			if(!_element || _displayed == _shown)
				return;
			_shown = _displayed;
			if(_displayed)
				_element->RemoveProperty(Rml::PropertyId::Display);
			else
				_element->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		}
	}

	StepGrid::StepGrid(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
	{
		// A column is the step of the page shown
		for(uint8_t column = 0; column < m_steps.size(); ++column)
		{
			m_steps[column] = _document.GetElementById("mdEdStep" + std::to_string(column));
			if(!m_steps[column])
				continue;
			juceRmlUi::EventListener::Add(m_steps[column], Rml::EventId::Click, [this, column](Rml::Event&)
			{
				toggleFocus(static_cast<uint8_t>(m_stepPage * StepCount + column));
			});
			// The two clicks before it have toggled the focus off and on again.
			juceRmlUi::EventListener::Add(m_steps[column], Rml::EventId::Dblclick, [this, column](Rml::Event&)
			{
				toggleTrig(static_cast<uint8_t>(m_stepPage * StepCount + column));
			});
		}
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			m_stepPages[page] = _document.GetElementById("mdEdStepPage" + std::to_string(page));
			if(!m_stepPages[page])
				continue;
			juceRmlUi::EventListener::Add(m_stepPages[page], Rml::EventId::Click, [this, page](Rml::Event&)
			{
				setStepPage(page);
			});
		}
		m_root = m_steps[0] ? m_steps[0]->GetParentNode() : nullptr;
		m_info = _document.GetElementById("mdEdStepsInfo");
		if(auto* button = _document.GetElementById("mdEdStepsRefresh"))
		{
			juceRmlUi::EventListener::Add(button, Rml::EventId::Click, [this](Rml::Event&)
			{
				refresh();
			});
		}

		// Machinedrum track parameters 0..23 are the synthesis, effects and routing
		// pages, 8 each, in the order the pattern's lock rows use.
		auto* document = _document.GetOwnerDocument();
		for(const auto& description : m_controller.getParameterDescriptions().getDescriptions())
		{
			if(description.page > md::automation::machinedrum::Routing || description.index >= 8)
				continue;
			const auto parameter = static_cast<uint8_t>(description.page * 8 + description.index);
			auto& control = m_controls[parameter];
			control.name = description.name;
			control.control = _document.GetElementById("mdEdCtl_" + description.name);
			control.value = _document.GetElementById("mdEdVal_" + description.name);
			if(!control.value || !document)
				continue;
			// The locked value sits exactly over the bound value label, which stays
			// bound and is only hidden while a locked step has the focus.
			auto lock = document->CreateElement("div");
			lock->SetClass("jucePos", true);
			lock->SetClass("juceLabel", true);
			lock->SetClass("mdEdValue", true);
			lock->SetClass("mdEdLockValue", true);
			lock->SetClass("mdEdHidden", true);
			lock->SetAttribute("style", attribute(*control.value, "style"));
			lock->SetId("mdEdLock_" + description.name);
			control.lock = control.value->GetParentNode()->AppendChild(std::move(lock));
			juceRmlUi::EventListener::Add(control.lock, Rml::EventId::Dblclick, [this, parameter](Rml::Event&)
			{
				if(isLocking())
				{
					editLock(parameter, std::nullopt);
					send();
				}
			});

			if(!control.control)
				continue;
			// A knob without sprite over the control: while a step with a trig has the
			// focus it takes the pointer, so the bound control and the Kit stay as they are.
			const bool fader = control.control->GetTagName() == "input";
			auto knob = document->CreateElement("knob");
			knob->SetClass("jucePos", true);
			knob->SetClass(fader ? "mdEdLockFader" : "mdEdLockKnob", true);
			knob->SetAttribute("style", attribute(*control.control, "style"));
			knob->SetAttribute("min", 0);
			knob->SetAttribute("max", 127);
			knob->SetAttribute("value", 0);
			// Out of range: the knob's own double click (back to default) does nothing.
			knob->SetAttribute("default", -1);
			knob->SetId("mdEdLockKnob_" + description.name);
			knob->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
			control.lockKnob = control.control->GetParentNode()->AppendChild(std::move(knob));
			juceRmlUi::EventListener::Add(control.lockKnob, Rml::EventId::Change, [this, parameter](Rml::Event& _event)
			{
				const auto value = std::lround(_event.GetParameter<float>("value", 0.0f));
				editLock(parameter, static_cast<uint8_t>(std::clamp<long>(value, 0, 127)));
			});
			juceRmlUi::EventListener::Add(control.lockKnob, Rml::EventId::Dragstart, [this](Rml::Event&)
			{
				m_dragging = true;
			});
			juceRmlUi::EventListener::Add(control.lockKnob, Rml::EventId::Dragend, [this](Rml::Event&)
			{
				m_dragging = false;
				send();
			});
			juceRmlUi::EventListener::Add(control.lockKnob, Rml::EventId::Dblclick, [this, parameter](Rml::Event&)
			{
				editLock(parameter, std::nullopt);
				send();
			});
		}
	}

	bool StepGrid::update(const double _nowMilliseconds)
	{
		m_now = _nowMilliseconds;
		if(m_controller.getPatternRevision() == 0 && _nowMilliseconds - m_lastRequest >= g_retryMilliseconds)
		{
			m_lastRequest = _nowMilliseconds;
			const auto reading = m_controller.requestPattern();
			m_dirty |= reading != m_reading;
			m_reading = reading;
		}
		if(m_sendPending && !m_dragging && _nowMilliseconds - m_lastEdit >= g_sendDelayMilliseconds)
			send();

		const auto part = m_controller.getCurrentPart();
		const auto revision = m_controller.getPatternRevision();
		const auto write = static_cast<uint8_t>(m_controller.getPatternWrite());
		if(part != m_shownPart || revision != m_shownRevision || write != m_shownWrite)
		{
			if(revision != m_shownRevision)
				m_reading = false;
			// Pending edits belong to the track they were made on.
			if(part != m_shownPart && m_shownPart != 0xff)
				send();
			m_shownPart = part;
			m_shownRevision = revision;
			m_shownWrite = write;
			m_dirty = true;
		}
		// Hidden (another page than SON): drawn once shown
		if(m_root && !m_root->IsVisible(true))
			return showPlayStep(-1);
		// Drawn first: the page shown is settled there
		const bool rendered = m_dirty;
		if(m_dirty)
			render();
		const auto playing = m_controller.getPlayingStep();
		const bool playChanged = showPlayStep(playing && *playing / StepCount == m_stepPage ? *playing % StepCount : -1);
		return rendered || playChanged;
	}

	bool StepGrid::showPlayStep(const int _column)
	{
		if(_column == m_shownPlayStep)
			return false;
		if(m_shownPlayStep >= 0 && m_steps[static_cast<size_t>(m_shownPlayStep)])
			m_steps[static_cast<size_t>(m_shownPlayStep)]->SetClass("mdEdStepNow", false);
		if(_column >= 0 && m_steps[static_cast<size_t>(_column)])
			m_steps[static_cast<size_t>(_column)]->SetClass("mdEdStepNow", true);
		m_shownPlayStep = _column;
		return true;
	}

	void StepGrid::setStepPage(const uint8_t _page)
	{
		if(_page >= m_stepPages.size() || _page == m_stepPage)
			return;
		// The focus is a step shown
		send();
		m_stepPage = _page;
		m_focus = -1;
		m_dirty = true;
		render();
	}

	void StepGrid::refresh()
	{
		m_reading = m_controller.requestPattern();
		m_dirty = true;
		render();
	}

	void StepGrid::toggleFocus(const uint8_t _step)
	{
		send();
		m_focus = m_focus == _step ? -1 : _step;
		m_dirty = true;
		render();
	}

	void StepGrid::toggleTrig(const uint8_t _step)
	{
		send();
		const auto pattern = m_controller.getPattern();
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		if(pattern && m_controller.setPatternTrig(part, _step, !pattern->hasTrig(part, _step)))
			m_controller.sendPatternSoon();
		m_focus = _step;
		m_dirty = true;
		render();
	}

	bool StepGrid::isLocking() const
	{
		if(m_focus < 0)
			return false;
		const auto pattern = m_controller.getPattern();
		return pattern && pattern->hasTrig(static_cast<uint8_t>(m_controller.getCurrentPart()), static_cast<uint8_t>(m_focus));
	}

	void StepGrid::editLock(const uint8_t _parameter, const std::optional<uint8_t> _value)
	{
		if(!isLocking())
			return;
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto step = static_cast<uint8_t>(m_focus);
		const auto pattern = m_controller.getPattern();
		if(pattern && pattern->lock(part, _parameter, step) == _value)
			return;
		if(!m_controller.setPatternLock(part, _parameter, step, _value))
			return;
		m_sendPending = true;
		m_lastEdit = m_now;
		render();
	}

	void StepGrid::send()
	{
		if(!m_sendPending)
			return;
		m_sendPending = false;
		m_controller.sendPattern();
	}

	uint8_t StepGrid::kitValue(const uint8_t _parameter) const
	{
		const auto* parameter = m_controller.getParameter(m_controls[_parameter].name, m_controller.getCurrentPart());
		return parameter ? static_cast<uint8_t>(std::clamp(static_cast<int>(parameter->getUnnormalizedValue()), 0, 127)) : 0;
	}

	void StepGrid::render()
	{
		m_dirty = false;
		const auto pattern = m_controller.getPattern();
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto length = pattern ? pattern->length : uint8_t{0};
		// Steps 33 to 64 only for a pattern over 32 steps; the focus only on a step shown
		m_stepPage = pattern && length > StepCount ? m_stepPage : 0;
		if(m_focus >= 0 && m_focus / StepCount != m_stepPage)
			m_focus = -1;
		const auto first = static_cast<uint8_t>(m_stepPage * StepCount);
		const bool locking = isLocking();

		// The trigs within the length, on every step
		size_t trigs = 0;
		for(uint8_t step = 0; pattern && step < length; ++step)
			trigs += pattern->hasTrig(part, step) ? 1 : 0;
		for(uint8_t column = 0; column < m_steps.size(); ++column)
		{
			auto* cell = m_steps[column];
			if(!cell)
				continue;
			const auto step = static_cast<uint8_t>(first + column);
			// A text written lays the document out: only when the page changes
			if(m_shownNumbers != m_stepPage)
				cell->SetInnerRML(std::to_string(step + 1));
			cell->SetClass("mdEdStepTrig", pattern && step < length && pattern->hasTrig(part, step));
			cell->SetClass("mdEdStepOut", pattern && step >= length);
			cell->SetClass("mdEdStepFocus", step == m_focus);
		}
		m_shownNumbers = m_stepPage;
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			if(!m_stepPages[page])
				continue;
			m_stepPages[page]->SetClass("mdEdSelected", page == m_stepPage);
			m_stepPages[page]->SetClass("mdEdOff", page > 0 && length <= StepCount);
		}

		size_t locks = 0;
		for(uint8_t parameter = 0; parameter < m_controls.size(); ++parameter)
		{
			auto& control = m_controls[parameter];
			if(!control.value)
				continue;
			const auto lock = pattern && m_focus >= 0
				? pattern->lock(part, parameter, static_cast<uint8_t>(m_focus)) : std::nullopt;
			locks += lock ? 1 : 0;
			const bool dim = m_focus >= 0 && !lock;
			if(control.lock)
			{
				setText(control.lock, control.shownLock, lock ? std::to_string(*lock) : std::string());
				control.lock->SetClass("mdEdHidden", !lock);
			}
			control.value->SetClass("mdEdHidden", lock.has_value());
			control.value->SetClass("mdEdDim", dim);
			if(control.control)
			{
				control.control->SetClass("mdEdDim", dim);
				control.control->SetClass("mdEdLocked", lock.has_value());
			}
			if(control.lockKnob)
			{
				setDisplayed(control.lockKnob, control.lockKnobShown, locking);
				// Starts from the lock, or from the Kit value for a parameter without one.
				if(locking && !m_dragging)
					juceRmlUi::ElemValue::setValue(control.lockKnob, static_cast<float>(lock ? *lock : kitValue(parameter)), false);
			}
		}

		if(!m_info)
			return;
		if(!pattern)
		{
			setText(m_info, m_shownInfo, m_reading ? "lecture du pattern…" : "pattern : en attente du firmware");
			return;
		}
		// The track is the tab above; the line keeps to what the grid cannot show.
		auto text = "pattern " + patternName(pattern->slot) + " · " + std::to_string(length) + " pas"
			+ " · " + std::to_string(trigs) + (trigs == 1 ? " trig" : " trigs");
		if(m_focus >= 0)
		{
			text += " · pas " + std::to_string(m_focus + 1);
			if(locking)
				text += " : " + std::to_string(locks) + (locks == 1 ? " lock" : " locks");
			else
				text += " sans trig : double-clic";
		}
		switch(m_controller.getPatternWrite())
		{
		case Controller::PatternWrite::Pending: text += " · envoi…"; break;
		case Controller::PatternWrite::Refused: text += " · refusé par le firmware"; break;
		default: break;
		}
		if(m_reading)
			text += " · relecture…";
		setText(m_info, m_shownInfo, text);
	}
}
