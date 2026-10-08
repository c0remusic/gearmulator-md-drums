#pragma once

#include "juceRmlUi/rmlElemKnob.h"

#include <functional>

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
	// The v22 knob (Design/HANDOFF.md, "Controls"), the <vknob> element: a 270 degree track, and once the value leaves
	// its default an arc from the minimum (from 64 with the "bipolar" attribute) and an ink pointer. It draws on a
	// canvas of its own, sharp at every scale, and repaints only when its value, range, default or size change. It keeps
	// ElemKnob's binding (param=), drag and double-click to the default, at the HANDOFF's pace: 1.5 px a step, Shift
	// four times finer; the wheel moves 2 steps (1 with Shift), the arrow keys 1 (10 with Shift) on the focused knob.
	// With the "fader" attribute it is the Mix's level fader instead: a 2 px rail and a 48 x 8 cap at the value, dragged
	// by the height of its box for the whole range (its "speed" attribute).
	class Knob final : public juceRmlUi::ElemKnob
	{
	public:
		static constexpr float PixelsPerStep = 1.5f;
		static constexpr float ShiftScale = 0.25f;

		Knob(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~Knob() override;

		void OnUpdate() override;
		void ProcessEvent(Rml::Event& _event) override;

		void onChangeValue() override;
		void onChangeMinValue() override;
		void onChangeMaxValue() override;
		void onChangeDefaultValue() override;
		void onPropertyChanged(const std::string& _key) override;

		bool isEdited() const;

		// ASSIGN (HANDOFF.md, "LFO"): a target wears a 1 px accent ring outside its box, 2 px under the mouse, and a click
		// on it calls _onPick instead of moving it (nor does the drag that follows). An empty function makes it a knob again.
		void setTarget(std::function<void()> _onPick);
		bool isTarget() const { return static_cast<bool>(m_onPick); }

	private:
		void step(float _steps);
		void updateSpeed();
		void repaint() const;
		void paint(const juce::Image& _image, juce::Graphics& _g) const;
		void paintFader(juce::Graphics& _g, float _dp) const;
		void paintRing(juce::Graphics& _g) const;

		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		juceRmlUi::ElemCanvas* m_ring = nullptr;
		bool m_bipolar = false;
		bool m_fader = false;
		std::function<void()> m_onPick;
		bool m_hover = false;
		bool m_swallowDrag = false;
	};
}
