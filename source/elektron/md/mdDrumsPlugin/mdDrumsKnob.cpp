#include "mdDrumsKnob.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlHelper.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Context.h"

#include <cmath>

namespace mdDrums
{
	namespace
	{
		// HANDOFF.md, "Colour"
		const juce::Colour g_track(0xff6e7177);
		const juce::Colour g_ink(0xfff2f2f2);
		const juce::Colour g_inkDim(0xffa7aaaf);

		// The track runs 270 degrees, from the lower left to the lower right through the top (JUCE's angles run
		// clockwise from 12 o'clock)
		constexpr float g_start = juce::MathConstants<float>::pi * 1.25f;
		constexpr float g_sweep = juce::MathConstants<float>::pi * 1.5f;
	}

	Knob::Knob(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemKnob(_coreInstance, _tag)
	{
		AddEventListener(Rml::EventId::Keydown, this);
		SetAttribute("speedScaleShift", ShiftScale);
	}

	Knob::~Knob()
	{
		RemoveEventListener(Rml::EventId::Keydown, this);
	}

	void Knob::OnUpdate()
	{
		if(!m_canvas)
		{
			m_canvas = juceRmlUi::ElemCanvas::create(this);
			m_canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_canvas->setPixelAligned(true);
			m_canvas->setClearEveryFrame(true);
			m_canvas->setRepaintGraphicsCallback([this](const juce::Image& _image, juce::Graphics& _g) { paint(_image, _g); });
		}
		ElemKnob::OnUpdate();
	}

	void Knob::ProcessEvent(Rml::Event& _event)
	{
		switch(_event.GetId())
		{
		case Rml::EventId::Mousescroll:
			{
				// Shift turns some wheels sideways
				const auto wheel = juceRmlUi::helper::getMouseWheelDelta(_event);
				const auto delta = wheel.y != 0.0f ? wheel.y : wheel.x;
				if(delta != 0.0f)
				{
					const auto steps = juceRmlUi::helper::getKeyModShift(_event) ? 1.0f : 2.0f;
					step(delta > 0.0f ? -steps : steps);
				}
				_event.StopPropagation();
				return;
			}
		case Rml::EventId::Keydown:
			{
				const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
				if(key != Rml::Input::KI_UP && key != Rml::Input::KI_DOWN)
					break;
				const auto steps = juceRmlUi::helper::getKeyModShift(_event) ? 10.0f : 1.0f;
				step(key == Rml::Input::KI_UP ? steps : -steps);
				_event.StopPropagation();
				return;
			}
		default:
			break;
		}
		ElemKnob::ProcessEvent(_event);
	}

	void Knob::onChangeValue()
	{
		ElemKnob::onChangeValue();
		repaint();
	}

	void Knob::onChangeMinValue()
	{
		ElemKnob::onChangeMinValue();
		updateSpeed();
		repaint();
	}

	void Knob::onChangeMaxValue()
	{
		ElemKnob::onChangeMaxValue();
		updateSpeed();
		repaint();
	}

	void Knob::onChangeDefaultValue()
	{
		ElemKnob::onChangeDefaultValue();
		repaint();
	}

	void Knob::onPropertyChanged(const std::string& _key)
	{
		ElemKnob::onPropertyChanged(_key);
		if(_key != "bipolar")
			return;
		m_bipolar = getProperty<int>("bipolar", 0) != 0;
		repaint();
	}

	bool Knob::isEdited() const
	{
		if(getValue() == UninitializedValue)
			return false;
		return std::lround(getValue()) != std::lround(getDefaultValue());
	}

	void Knob::step(const float _steps)
	{
		const auto from = getValue() == UninitializedValue ? getDefaultValue() : std::round(getValue());
		setValue(from + _steps);
	}

	void Knob::updateSpeed()
	{
		// ElemKnob's speed is the drag for a full sweep
		SetAttribute("speed", std::max(1.0f, getRange()) * PixelsPerStep);
	}

	void Knob::repaint() const
	{
		if(m_canvas)
			m_canvas->repaint();
	}

	void Knob::paint(const juce::Image&, juce::Graphics& _g) const
	{
		const auto paintSize = m_canvas->getPaintSize();
		const auto size = static_cast<float>(std::min(paintSize.x, paintSize.y));
		if(size <= 0.0f)
			return;
		const auto* context = GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;

		// The mockup's geometry (v22-ui.js, knobSvg): radius size/2 - 3, the pointer from 35 % of it to 5 px short of it
		const auto centre = size * 0.5f;
		const auto radius = centre - 3.0f * dp;
		const auto range = getRange();
		const auto fraction = [&](const float _value)
		{
			return range > 0.0f ? std::clamp((_value - getMinValue()) / range, 0.0f, 1.0f) : 0.0f;
		};
		const auto angleOf = [&](const float _value) { return g_start + g_sweep * fraction(_value); };

		juce::Path track;
		track.addCentredArc(centre, centre, radius, radius, 0.0f, g_start, g_start + g_sweep, true);
		_g.setColour(g_track);
		_g.strokePath(track, juce::PathStrokeType(2.0f * dp));

		const auto value = getValue() == UninitializedValue ? getDefaultValue() : getValue();
		const auto edited = isEdited();
		const auto angle = angleOf(value);
		const auto from = m_bipolar ? angleOf(getMinValue() + std::ceil(range * 0.5f)) : g_start;

		if(edited && std::abs(angle - from) > 0.0001f)
		{
			juce::Path arc;
			arc.addCentredArc(centre, centre, radius, radius, 0.0f, std::min(from, angle), std::max(from, angle), true);
			_g.setColour(g_ink);
			_g.strokePath(arc, juce::PathStrokeType(3.0f * dp));
		}

		const auto inner = radius * 0.35f, outer = radius - 5.0f * dp;
		const auto sin = std::sin(angle), cos = std::cos(angle);
		_g.setColour(edited ? g_ink : g_inkDim);
		_g.drawLine(centre + inner * sin, centre - inner * cos, centre + outer * sin, centre - outer * cos, 2.0f * dp);
	}
}
