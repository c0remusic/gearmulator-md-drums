#include "mdDrumsGlyph.h"

#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/PropertyIdSet.h"

namespace mdDrums
{
	Glyph::Glyph(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag)
	{
	}

	void Glyph::OnUpdate()
	{
		if(!m_canvas)
		{
			m_canvas = juceRmlUi::ElemCanvas::create(this);
			m_canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_canvas->setPixelAligned(true);
			m_canvas->setClearEveryFrame(true);
			m_canvas->setRepaintGraphicsCallback([this](const juce::Image&, juce::Graphics& _g) { paint(_g); });
		}
		Element::OnUpdate();
	}

	void Glyph::OnPropertyChange(const Rml::PropertyIdSet& _changedProperties)
	{
		Element::OnPropertyChange(_changedProperties);
		// A hover or a state class changes the colour
		if(m_canvas && _changedProperties.Contains(Rml::PropertyId::Color))
			m_canvas->repaint();
	}

	void Glyph::onPropertyChanged(const std::string& _key)
	{
		if(_key != "shape")
			return;
		m_shape = getProperty<std::string>("shape", "");
		if(m_canvas)
			m_canvas->repaint();
	}

	void Glyph::paint(juce::Graphics& _g) const
	{
		const auto size = m_canvas->getPaintSize();
		const auto w = static_cast<float>(size.x), h = static_cast<float>(size.y);
		if(w <= 0.0f || h <= 0.0f)
			return;
		const auto* context = GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
		const auto colour = GetComputedValues().color();
		_g.setColour(juce::Colour(colour.red, colour.green, colour.blue, colour.alpha));

		juce::Path path;
		if(m_shape == "play")
		{
			// M2 1 L13 8 L2 15 Z in a 14 x 16 box, drawn to the element's size
			path.addTriangle(2.0f / 14.0f * w, 1.0f / 16.0f * h, 13.0f / 14.0f * w, 8.0f / 16.0f * h, 2.0f / 14.0f * w, 15.0f / 16.0f * h);
			_g.fillPath(path);
		}
		else if(m_shape == "chevron")
		{
			// Arms of 8 px at 45 degrees, centred
			const auto arm = 8.0f * dp * 0.70710678f;
			const auto cx = w * 0.5f, cy = h * 0.5f;
			path.startNewSubPath(cx - arm, cy - arm * 0.5f);
			path.lineTo(cx, cy + arm * 0.5f);
			path.lineTo(cx + arm, cy - arm * 0.5f);
			_g.strokePath(path, juce::PathStrokeType(2.0f * dp, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
		}
		else if(m_shape == "down")
		{
			path.addTriangle(0.0f, h * 0.25f, w, h * 0.25f, w * 0.5f, h * 0.75f);
			_g.fillPath(path);
		}
	}
}
