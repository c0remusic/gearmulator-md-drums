#pragma once

#include "juceRmlUi/rmlElement.h"

namespace juce
{
	class Graphics;
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace mdDrums
{
	// The v22 glyphs Inter lacks or the software renderer cannot turn (Design/HANDOFF.md, "Asset manifest"), the
	// <vglyph shape="..."> element, drawn in its RCSS colour on a canvas of its own: "play" (the play key's triangle),
	// "chevron" (the machine name's: an 8 x 8 L of 2 px turned 45 degrees) and "down" (the small triangle after the
	// kit's name and the LFO's target, which the mockup takes from a fallback font).
	class Glyph final : public juceRmlUi::Element
	{
	public:
		Glyph(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void OnUpdate() override;
		void OnPropertyChange(const Rml::PropertyIdSet& _changedProperties) override;
		void onPropertyChanged(const std::string& _key) override;

	private:
		void paint(juce::Graphics& _g) const;

		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		std::string m_shape;
	};
}
