#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// Greys the editor's elements that show a value the firmware set without telling
	// (Controller::isValueKnown, after a machine assignment), class mdEdUnread:
	// - every control with a param attribute under mdEditor, with the SON view's
	//   value line under it (mdEdVal_<param>), whose number reads "—";
	// - every element with a data-unread attribute, a space separated list of the
	//   parameters it shows (summary lines, curves), greyed while one is unknown.
	class UnreadValues
	{
	public:
		UnreadValues(Controller& _controller, Rml::Element& _document);

		// Applies the controller's state when it or the edited track changed.
		// Returns true when it changed the DOM.
		bool update();

	private:
		struct Control
		{
			Rml::Element* element = nullptr;
			std::vector<std::pair<uint8_t, uint8_t>> values;    // page, index
			uint8_t part = 0;          // CurrentPart: the edited track
			bool unread = false;
		};

		static constexpr uint8_t CurrentPart = 0xff;

		void collect(Rml::Element& _element);
		bool addValue(Control& _control, const std::string& _parameter) const;

		Controller& m_controller;
		std::vector<Control> m_controls;
		uint64_t m_shownRevision = ~uint64_t{0};
		uint8_t m_shownPart = 0xff;
	};
}
