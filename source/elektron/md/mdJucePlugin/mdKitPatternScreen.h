#pragma once

#include <cstdint>
#include <string>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The top bar's screen: the selected Kit and pattern on its first line, the
	// Kit's name on the second, from the controller's status and Kit dump replies.
	// Element ids from the skin: mdEdScreenMain, mdEdScreenName. Does nothing when
	// the skin has no screen.
	class KitPatternScreen
	{
	public:
		KitPatternScreen(Controller& _controller, Rml::Element& _document);

		// Refreshes the screen when the Kit, its name or the pattern changed.
		// Returns true when it changed the DOM.
		bool update();

		// "KIT 03 · PATTERN B03": Kits from 1, patterns A01 to H16, "—" for what
		// is not known yet (0xff)
		static std::string mainLine(uint8_t _kit, uint8_t _pattern);

	private:
		Controller& m_controller;
		Rml::Element* m_main = nullptr;
		Rml::Element* m_name = nullptr;
		uint64_t m_shownRevision = ~uint64_t{0};
	};
}
