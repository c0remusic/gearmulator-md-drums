#pragma once

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

#include <cstdio>
#include <string>

namespace mdDrums
{
	// A length in dp, as the views give it to SetProperty
	inline std::string dp(const int _value)
	{
		return std::to_string(_value) + "dp";
	}

	inline std::string dp(const double _value)
	{
		return std::to_string(_value) + "dp";
	}

	// A Track's, an Out's or a Slot's number as the skin shows it: 1 is "01"
	inline std::string twoDigits(const int _value)
	{
		char text[8];
		(void)std::snprintf(text, sizeof(text), "%02d", _value);
		return text;
	}

	// Shows _element with the display _shown or hides it; nothing without an element. Only a change is written:
	// setting display, even to its value, lays the whole document out again. True when it wrote
	inline bool setDisplay(Rml::Element* _element, const bool _visible,
		const Rml::Style::Display _shown = Rml::Style::Display::Block)
	{
		return _element && juceRmlUi::helper::changeProperty(_element, Rml::PropertyId::Display,
			Rml::Property(_visible ? _shown : Rml::Style::Display::None));
	}

	// A length in dp, written only when it differs. True when it wrote
	inline bool setDp(Rml::Element* _element, const Rml::PropertyId _property, const float _value)
	{
		return _element && juceRmlUi::helper::changeProperty(_element, _property, Rml::Property(_value, Rml::Unit::DP));
	}

	// A wheel step's direction, -1, 0 or 1: the vertical delta, else the horizontal one (Shift turns some wheels)
	inline int wheelSign(const Rml::Event& _event)
	{
		const auto wheel = juceRmlUi::helper::getMouseWheelDelta(_event);
		const auto delta = wheel.y != 0.0f ? wheel.y : wheel.x;
		return (delta > 0.0f) - (delta < 0.0f);
	}

	// _element's RML, written only when it differs: SetInnerRML builds the children again. True when it wrote
	inline bool setText(Rml::Element* _element, const std::string& _rml)
	{
		if(!_element || _element->GetInnerRML() == _rml)
			return false;
		_element->SetInnerRML(_rml);
		return true;
	}
}
