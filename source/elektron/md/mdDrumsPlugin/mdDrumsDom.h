#pragma once

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

	// Shows _element with the display _shown ("block", "inline") or hides it; nothing without an element
	inline void setDisplay(Rml::Element* _element, const bool _visible, const char* _shown = "block")
	{
		if(_element)
			_element->SetProperty("display", _visible ? _shown : "none");
	}
}
