#pragma once

#include "juceRmlUi/rmlElemCanvas.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementUtilities.h"

#include <functional>
#include <utility>

namespace mdDrums
{
	// A canvas over _parent's box, on which the views and the custom elements draw: the mouse goes through it, pixel
	// aligned, cleared every frame. _paint draws it at its size in pixels, with its dp ratio.
	inline juceRmlUi::ElemCanvas* createCanvas(Rml::Element* _parent,
		std::function<void(juce::Graphics& _g, float _w, float _h, float _dp)> _paint)
	{
		auto* canvas = juceRmlUi::ElemCanvas::create(_parent);
		canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		canvas->setPixelAligned(true);
		canvas->setClearEveryFrame(true);
		canvas->setRepaintGraphicsCallback([canvas, paint = std::move(_paint)](const juce::Image&, juce::Graphics& _g)
		{
			const auto size = canvas->getPaintSize();
			paint(_g, static_cast<float>(size.x), static_cast<float>(size.y),
				Rml::ElementUtilities::GetDensityIndependentPixelRatio(canvas));
		});
		return canvas;
	}
}
