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
		AddEventListener(Rml::EventId::Mouseover, this);
		AddEventListener(Rml::EventId::Mouseout, this);
		SetAttribute("speedScaleShift", ShiftScale);
	}

	Knob::~Knob()
	{
		RemoveEventListener(Rml::EventId::Keydown, this);
		RemoveEventListener(Rml::EventId::Mouseover, this);
		RemoveEventListener(Rml::EventId::Mouseout, this);
	}

	void Knob::setTarget(std::function<void()> _onPick)
	{
		m_onPick = std::move(_onPick);
		if(!m_ring && m_onPick)
		{
			// The ring lies outside the knob's box: a canvas of its own, 2 px larger each way
			m_ring = juceRmlUi::ElemCanvas::create(this);
			m_ring->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_ring->SetProperty(Rml::PropertyId::Left, Rml::Property(-2.0f, Rml::Unit::DP));
			m_ring->SetProperty(Rml::PropertyId::Top, Rml::Property(-2.0f, Rml::Unit::DP));
			m_ring->SetProperty(Rml::PropertyId::Width, Rml::Property(68.0f, Rml::Unit::DP));
			m_ring->SetProperty(Rml::PropertyId::Height, Rml::Property(68.0f, Rml::Unit::DP));
			m_ring->setPixelAligned(true);
			m_ring->setClearEveryFrame(true);
			m_ring->setRepaintGraphicsCallback([this](const juce::Image&, juce::Graphics& _g) { paintRing(_g); });
		}
		if(!m_onPick)
			m_hover = false;
		if(m_ring)
		{
			m_ring->SetProperty(Rml::PropertyId::Display, m_onPick ? Rml::Style::Display::Block : Rml::Style::Display::None);
			m_ring->repaint();
		}
	}

	void Knob::paintRing(juce::Graphics& _g) const
	{
		// HANDOFF.md, "LFO": the mockup's box-shadow, 0 0 0 1px (2px hovered) around the knob's circle
		const auto* context = GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
		const auto centre = 34.0f * dp;
		const auto width = (m_hover ? 2.0f : 1.0f) * dp;
		const auto radius = 32.0f * dp + width * 0.5f;
		_g.setColour(juce::Colour(0xff6fd1c4));
		_g.drawEllipse(centre - radius, centre - radius, radius * 2.0f, radius * 2.0f, width);
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
		const auto id = _event.GetId();
		if(m_onPick)
		{
			switch(id)
			{
			case Rml::EventId::Mousedown:
				{
					// The pick may make every knob a knob again, this one included
					const auto pick = m_onPick;
					m_swallowDrag = true;
					_event.StopPropagation();
					pick();
					return;
				}
			case Rml::EventId::Mouseover:
			case Rml::EventId::Mouseout:
				m_hover = id == Rml::EventId::Mouseover;
				if(m_ring)
					m_ring->repaint();
				return;
			case Rml::EventId::Drag:
			case Rml::EventId::Dblclick:
			case Rml::EventId::Mousescroll:
			case Rml::EventId::Keydown:
				_event.StopPropagation();
				return;
			default:
				break;
			}
		}
		if(id == Rml::EventId::Mousedown)
			m_swallowDrag = false;
		else if(m_swallowDrag && (id == Rml::EventId::Drag || id == Rml::EventId::Dblclick))
			return;

		switch(id)
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
		if(_key == "bipolar")
			m_bipolar = getProperty<int>("bipolar", 0) != 0;
		else if(_key == "fader")
			m_fader = getProperty<int>("fader", 0) != 0;
		else
			return;
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
		// ElemKnob's speed is the drag for a full sweep; a fader's is its height, set in the skin
		if(!m_fader)
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
		if(m_fader)
		{
			paintFader(_g, dp);
			return;
		}

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

	void Knob::paintFader(juce::Graphics& _g, const float _dp) const
	{
		// HANDOFF.md, Mix: the rail a 2 px --track stroke at x + 39 of the strip's column, the cap 48 x 8 ink at x + 16,
		// its foot at the value's height less 4 px (the mockup's bottom: calc(level / 127 - 4px))
		const auto height = static_cast<float>(m_canvas->getPaintSize().y);
		const auto range = getRange();
		const auto value = getValue() == UninitializedValue ? getDefaultValue() : getValue();
		const auto fraction = range > 0.0f ? std::clamp((value - getMinValue()) / range, 0.0f, 1.0f) : 0.0f;
		_g.setColour(g_track);
		_g.fillRect(39.0f * _dp, 0.0f, 2.0f * _dp, height);
		const auto foot = height - fraction * height + 4.0f * _dp;
		_g.setColour(g_ink);
		_g.fillRect(16.0f * _dp, foot - 8.0f * _dp, 48.0f * _dp, 8.0f * _dp);
	}
}
