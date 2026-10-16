#pragma once

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

#include <cstdio>
#include <cstring>
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

	// Shows _element with the display _shown ("block", "inline", "inline-block") or hides it; nothing without an
	// element. Only a change is written: setting display, even to its value, lays the whole document out again.
	// True when it wrote
	inline bool setDisplay(Rml::Element* _element, const bool _visible, const char* _shown = "block")
	{
		if(!_element)
			return false;
		using Display = Rml::Style::Display;
		const auto shown = std::strcmp(_shown, "inline") == 0 ? Display::Inline
			: std::strcmp(_shown, "inline-block") == 0 ? Display::InlineBlock : Display::Block;
		return juceRmlUi::helper::changeProperty(_element, Rml::PropertyId::Display,
			Rml::Property(_visible ? shown : Display::None));
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
