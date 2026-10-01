#include "mdMasterEffectsView.h"

#include "mdController.h"

#include "juceRmlUi/rmlElemValue.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace mdJucePlugin
{
	MasterEffectsView::MasterEffectsView(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
	{
		for(uint8_t effect = 0; effect < m_controls.size(); ++effect)
		{
			for(uint8_t parameter = 0; parameter < m_controls[effect].size(); ++parameter)
			{
				const auto id = std::to_string(effect) + "_" + std::to_string(parameter);
				auto& control = m_controls[effect][parameter];
				control.knob = _document.GetElementById("mdEdFx" + id);
				control.value = _document.GetElementById("mdEdFxVal" + id);
				if(!control.knob)
					continue;
				juceRmlUi::EventListener::Add(control.knob, Rml::EventId::Change, [this, effect, parameter](Rml::Event& _event)
				{
					onTurn(effect, parameter, _event.GetParameter<float>("value", 0.0f));
				});
			}
		}
	}

	bool MasterEffectsView::update()
	{
		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getMasterEffectRevision();
		if(revision == m_shownRevision)
			return false;
		m_shownRevision = revision;
		bool changed = false;
		for(uint8_t effect = 0; effect < m_controls.size(); ++effect)
		{
			for(uint8_t parameter = 0; parameter < m_controls[effect].size(); ++parameter)
			{
				auto& control = m_controls[effect][parameter];
				const auto value = m_controller.getMasterEffect(static_cast<md::automation::sysex::MasterEffect>(effect), parameter);
				const auto shown = value ? static_cast<int>(*value) : -1;
				if(shown == control.shown)
					continue;
				show(control, shown);
				changed = true;
			}
		}
		return changed;
	}

	void MasterEffectsView::onTurn(const uint8_t _effect, const uint8_t _parameter, const float _value)
	{
		auto& control = m_controls[_effect][_parameter];
		const auto value = static_cast<int>(std::clamp<long>(std::lround(_value), 0, 127));
		if(value == control.shown)
			return;
		if(!m_controller.setMasterEffect(static_cast<md::automation::sysex::MasterEffect>(_effect), _parameter,
			static_cast<uint8_t>(value)))
		{
			// Not sent (firmware not ready): back to what the firmware holds
			show(control, control.shown);
			return;
		}
		// The line follows at once; the next update sees the same value
		show(control, value);
	}

	void MasterEffectsView::show(Control& _control, const int _value)
	{
		_control.shown = _value;
		const bool unknown = _value < 0;
		if(_control.knob)
		{
			_control.knob->SetClass("mdEdUnread", unknown);
			if(!unknown)
				juceRmlUi::ElemValue::setValue(_control.knob, static_cast<float>(_value), false);
		}
		if(_control.value)
		{
			_control.value->SetClass("mdEdUnread", unknown);
			_control.value->SetInnerRML(unknown ? "—" : std::to_string(_value));
		}
	}
}
