#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "mdLib/mdlivekit.h"

namespace Rml
{
	class Element;
	class Event;
}

namespace juce
{
	class Graphics;
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace mdJucePlugin
{
	class Controller;

	// The MODULATION block of the Machinedrum's SON page: where the edited track's LFO goes, how a trig
	// restarts it and its two shapes, as the Kit holds them (Controller::getTrackLfo), and the wave the
	// shapes make with the track's SHMIX. A click on the destination or on a shape opens a menu, a click
	// on an update sets it: SET LFO PARAM ($62, Controller::setTrackLfo). Element ids from the skin
	// (mdEdLfo*); does nothing when the skin has no such block.
	class LfoView
	{
	public:
		LfoView(Controller& _controller, Rml::Element& _document);

		// Redraws when the edited track, its LFO, the tracks' machines (they name the destination) or the
		// track's SHMIX changed, once SON shows the block. Returns true when it changed the DOM.
		bool update();

		// As the view names them
		static const char* shapeName(uint8_t _shape);
		// A shape's level, 0 to 1, at _phase through a period (0 to 1). The one-shot shapes (ramp,
		// exponential) fall once over the period; random holds the level _period picks.
		static float shapeLevel(uint8_t _shape, float _phase, uint32_t _period);

	private:
		void openDestinationMenu(const Rml::Event& _event);
		void openShapeMenu(const Rml::Event& _event, uint8_t _field);
		// _periods of the wave; an icon is drawn smaller, in white, without its axis
		void paintWave(juce::Image& _image, juce::Graphics& _g, uint8_t _shape1, uint8_t _shape2, float _mix,
			uint32_t _periods, bool _icon) const;

		Controller& m_controller;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_destination = nullptr;
		std::array<Rml::Element*, 3> m_updates{};
		std::array<Rml::Element*, 2> m_shapeNames{};
		std::array<juceRmlUi::ElemCanvas*, 2> m_icons{};
		juceRmlUi::ElemCanvas* m_wave = nullptr;
		Rml::Element* m_info = nullptr;

		// What the DOM and the canvases show
		std::optional<md::LfoSettings> m_shown;
		uint8_t m_shownPart = 0xff;
		uint64_t m_shownMachines = ~uint64_t{0};
		int m_shownMix = -1;
	};
}
