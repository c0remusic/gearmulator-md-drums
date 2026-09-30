#pragma once

#include "mdLib/mdtypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace juce
{
	class Graphics;
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The COURBES row under the SON view: the filter response and the EQ of the
	// edited track, and on the Monomachine its amp envelope. The curves show the
	// shape the parameters give, not measured frequencies or times: the plug-in
	// does not know the firmware's scales.
	// The row takes room only when the page has it (see isShown), so the default
	// window keeps every control without scrolling.
	class CurveView
	{
	public:
		enum class Curve : uint8_t
		{
			Amp,     // attack, hold, decay (Monomachine)
			Filter,  // high-pass and low-pass edges, their resonance
			Eq       // one bell
		};

		CurveView(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Repaints the curves when the edited track or one of their parameters
		// changed. Returns true when it repainted.
		bool update();

		bool isShown() const { return m_shown; }

		// Level in dB of a curve at a position 0..1 of its frequency axis (Filter,
		// Eq), or level 0..1 at a position 0..1 of its time axis (Amp).
		static float level(Curve _curve, const std::vector<int>& _values, float _x);

	private:
		struct Canvas
		{
			Curve curve;
			juceRmlUi::ElemCanvas* canvas = nullptr;
		};

		void layout();
		float pageHeight() const;
		std::vector<int> values(Curve _curve) const;
		void paint(Curve _curve, juce::Image& _image, juce::Graphics& _g) const;

		Controller& m_controller;
		const md::MachineModel m_model;
		Rml::Element& m_document;
		Rml::Element* m_row = nullptr;
		Rml::Element* m_trackView = nullptr;
		float m_baseHeight = 0.0f;
		float m_roomyHeight = 0.0f;
		std::vector<Canvas> m_canvases;
		std::vector<int> m_shownValues;
		bool m_shown = false;
	};
}
