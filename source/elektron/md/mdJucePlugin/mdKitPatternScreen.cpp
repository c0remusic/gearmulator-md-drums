#include "mdKitPatternScreen.h"

#include "mdController.h"

#include "RmlUi/Core/Element.h"

namespace mdJucePlugin
{
	namespace
	{
		// A Kit name comes from the firmware: keep it out of the RML markup
		std::string escape(const std::string& _text)
		{
			std::string result;
			for(const auto c : _text)
			{
				switch(c)
				{
				case '&': result += "&amp;"; break;
				case '<': result += "&lt;"; break;
				case '>': result += "&gt;"; break;
				default: result += c; break;
				}
			}
			return result;
		}
	}

	KitPatternScreen::KitPatternScreen(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
		, m_main(_document.GetElementById("mdEdScreenMain"))
		, m_name(_document.GetElementById("mdEdScreenName"))
	{
	}

	std::string KitPatternScreen::mainLine(const uint8_t _kit, const uint8_t _pattern)
	{
		std::string kit = "—";
		if(_kit != 0xff)
		{
			kit = std::to_string(_kit + 1);
			if(kit.size() < 2)
				kit.insert(0, "0");
		}
		std::string pattern = "—";
		if(_pattern < 128)
		{
			const auto number = _pattern % 16 + 1;
			pattern = std::string(1, static_cast<char>('A' + _pattern / 16)) + (number < 10 ? "0" : "")
				+ std::to_string(number);
		}
		return "KIT " + kit + " · PATTERN " + pattern;
	}

	bool KitPatternScreen::update()
	{
		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getSelectionRevision();
		if(!m_main || revision == m_shownRevision)
			return false;
		m_shownRevision = revision;
		m_main->SetInnerRML(mainLine(m_controller.getCurrentKit(), m_controller.getCurrentPattern()));
		if(m_name)
			m_name->SetInnerRML(escape(m_controller.getKitName()));
		return true;
	}
}
