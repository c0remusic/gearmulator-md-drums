#include "mdStepGrid.h"

#include "mdController.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <string>

namespace mdJucePlugin
{
	namespace
	{
		constexpr double g_retryMilliseconds = 3000.0;

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
	}

	StepGrid::StepGrid(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
	{
		for(uint8_t step = 0; step < m_steps.size(); ++step)
		{
			m_steps[step] = _document.GetElementById("mdEdStep" + std::to_string(step));
			if(m_steps[step])
			{
				juceRmlUi::EventListener::Add(m_steps[step], Rml::EventId::Click, [this, step](Rml::Event&)
				{
					toggleFocus(step);
				});
			}
		}
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
			auto& control = m_controls[description.page * 8 + description.index];
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
		}
	}

	bool StepGrid::update(const double _nowMilliseconds)
	{
		if(m_controller.getPatternRevision() == 0 && _nowMilliseconds - m_lastRequest >= g_retryMilliseconds)
		{
			m_lastRequest = _nowMilliseconds;
			const auto reading = m_controller.requestPattern();
			m_dirty |= reading != m_reading;
			m_reading = reading;
		}
		const auto part = m_controller.getCurrentPart();
		const auto revision = m_controller.getPatternRevision();
		if(part != m_shownPart || revision != m_shownRevision)
		{
			if(revision != m_shownRevision)
				m_reading = false;
			m_shownPart = part;
			m_shownRevision = revision;
			m_dirty = true;
		}
		if(!m_dirty)
			return false;
		render();
		return true;
	}

	void StepGrid::refresh()
	{
		m_reading = m_controller.requestPattern();
		m_dirty = true;
		render();
	}

	void StepGrid::toggleFocus(const uint8_t _step)
	{
		m_focus = m_focus == _step ? -1 : _step;
		m_dirty = true;
		render();
	}

	void StepGrid::render()
	{
		m_dirty = false;
		const auto pattern = m_controller.getPattern();
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto length = pattern ? pattern->length : uint8_t{0};

		size_t trigs = 0;
		for(uint8_t step = 0; step < m_steps.size(); ++step)
		{
			const bool trig = pattern && pattern->hasTrig(part, step) && step < length;
			trigs += trig ? 1 : 0;
			if(!m_steps[step])
				continue;
			m_steps[step]->SetClass("mdEdStepTrig", trig);
			m_steps[step]->SetClass("mdEdStepOut", pattern && step >= length);
			m_steps[step]->SetClass("mdEdStepFocus", step == m_focus);
		}

		size_t locks = 0;
		for(uint8_t parameter = 0; parameter < m_controls.size(); ++parameter)
		{
			const auto& control = m_controls[parameter];
			if(!control.value)
				continue;
			const auto lock = pattern && m_focus >= 0
				? pattern->lock(part, parameter, static_cast<uint8_t>(m_focus)) : std::nullopt;
			locks += lock ? 1 : 0;
			const bool dim = m_focus >= 0 && !lock;
			if(control.lock)
			{
				control.lock->SetInnerRML(lock ? std::to_string(*lock) : std::string());
				control.lock->SetClass("mdEdHidden", !lock);
			}
			control.value->SetClass("mdEdHidden", lock.has_value());
			control.value->SetClass("mdEdDim", dim);
			if(control.control)
			{
				control.control->SetClass("mdEdDim", dim);
				control.control->SetClass("mdEdLocked", lock.has_value());
			}
		}

		if(!m_info)
			return;
		if(!pattern)
		{
			m_info->SetInnerRML(m_reading ? "lecture du pattern…" : "pattern : en attente du firmware");
			return;
		}
		auto text = "pattern " + patternName(pattern->slot) + " · " + std::to_string(length) + " pas · piste "
			+ std::to_string(part + 1) + " : " + std::to_string(trigs) + (trigs == 1 ? " trig" : " trigs");
		if(length > 32)
			text += " · 32 premiers pas affichés";
		if(m_focus >= 0)
			text += " · pas " + std::to_string(m_focus + 1) + " : " + std::to_string(locks)
				+ (locks == 1 ? " valeur verrouillée" : " valeurs verrouillées");
		if(m_reading)
			text += " · relecture…";
		m_info->SetInnerRML(text);
	}
}
