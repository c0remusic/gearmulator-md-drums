#pragma once

#include "mdLib/mdautomation.h"

#include <array>
#include <cstdint>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The SORTIE column of MIX (Machinedrum): for each track a segment per output,
	// mdEdOut<track>_<output> (0 to 5 for A to F, 6 for MAIN). The chosen output
	// shows in orange (class mdEdSelected), the whole row is greyed while the
	// routing is unknown, and a click sends Controller::setTrackOutput.
	class TrackRoutingView
	{
	public:
		TrackRoutingView(Controller& _controller, Rml::Element& _document);

		// Shows the controller's routing when it changed. Returns true when it
		// changed the DOM.
		bool update();

	private:
		static constexpr uint8_t OutputCount = 7;

		Controller& m_controller;
		std::array<std::array<Rml::Element*, OutputCount>, md::automation::machinedrum::TrackCount> m_segments{};
		uint64_t m_shownRevision = ~uint64_t{0};
	};
}
