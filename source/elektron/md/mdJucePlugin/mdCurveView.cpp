#include "mdCurveView.h"

#include "mdController.h"
#include "mdViewLayout.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace mdJucePlugin
{
	namespace
	{
		// Top bar and track strip above the page; the front panel too when stacked.
		constexpr float g_aboveThePage = 36.0f + 32.0f;
		constexpr float g_frontPanelHeight = 570.0f;

		// Frequency axis: 20 Hz to 20 kHz, a parameter 0..127 spread along it.
		constexpr float g_minDb = -36.0f;
		constexpr float g_maxDb = 18.0f;

		float frequency(const float _x)
		{
			return 20.0f * std::pow(1000.0f, _x);
		}

		float cutoff(const int _value)
		{
			return frequency(static_cast<float>(std::clamp(_value, 0, 127)) / 127.0f);
		}

		float resonance(const int _value)
		{
			return 0.5f + static_cast<float>(std::clamp(_value, 0, 127)) / 127.0f * 7.5f;
		}

		// Two-pole low-pass and high-pass magnitudes at r = f / fc.
		float lowPass(const float _r, const float _q)
		{
			return 1.0f / std::sqrt((1.0f - _r * _r) * (1.0f - _r * _r) + (_r / _q) * (_r / _q));
		}

		float highPass(const float _r, const float _q)
		{
			return _r * _r * lowPass(_r, _q);
		}

		float toDb(const float _magnitude)
		{
			return 20.0f * std::log10(std::max(_magnitude, 1.0e-4f));
		}

		const juce::Colour g_line(0xfff4f1ea);
		const juce::Colour g_grid(0xff2e3034);
	}

	CurveView::CurveView(Controller& _controller, const md::MachineModel _model, Rml::Element& _document)
		: m_controller(_controller)
		, m_model(_model)
		, m_document(_document)
	{
		m_row = _document.GetElementById("mdEdCurves");
		m_trackView = _document.GetElementById("mdEdTrackView");
		if(m_trackView)
		{
			if(const auto* height = m_trackView->GetLocalProperty(Rml::PropertyId::Height))
				m_baseHeight = height->Get<float>(m_trackView->GetCoreInstance());
			if(const auto* roomy = m_trackView->GetAttribute("roomyheight"))
				m_roomyHeight = roomy->Get<float>(m_trackView->GetCoreInstance(), 0.0f);
		}

		for(const auto& [id, curve] : {std::pair{"mdEdCurveAmp", Curve::Amp}, {"mdEdCurveFilter", Curve::Filter}, {"mdEdCurveEq", Curve::Eq}})
		{
			auto* area = _document.GetElementById(id);
			if(!area)
				continue;
			auto* canvas = juceRmlUi::ElemCanvas::create(area);
			canvas->setRepaintGraphicsCallback([this, curve = curve](juce::Image& _image, juce::Graphics& _g)
			{
				paint(curve, _image, _g);
			});
			m_canvases.push_back({curve, canvas});
		}

		juceRmlUi::EventListener::Add(&_document, Rml::EventId::Resize, [this](Rml::Event&)
		{
			layout();
		});
		layout();
	}

	float CurveView::pageHeight() const
	{
		const auto* context = m_document.GetContext();
		if(!context || context->GetDensityIndependentPixelRatio() <= 0.0f)
			return 0.0f;
		const auto window = static_cast<float>(context->GetDimensions().y) / context->GetDensityIndependentPixelRatio();
		return window - g_aboveThePage - (window >= ViewLayout::g_stackedHeight ? g_frontPanelHeight : 0.0f);
	}

	void CurveView::layout()
	{
		if(!m_row || !m_trackView || m_roomyHeight <= m_baseHeight)
			return;
		const bool shown = pageHeight() >= m_roomyHeight;
		if(shown == m_shown)
			return;
		m_shown = shown;
		if(m_shown)
			m_row->RemoveProperty(Rml::PropertyId::Display);
		else
			m_row->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		m_trackView->SetProperty(Rml::PropertyId::Height, Rml::Property(m_shown ? m_roomyHeight : m_baseHeight, Rml::Unit::DP));
		m_shownValues.clear();
	}

	std::vector<int> CurveView::values(const Curve _curve) const
	{
		const bool mm = m_model == md::MachineModel::Monomachine;
		std::vector<const char*> names;
		switch(_curve)
		{
		case Curve::Amp: names = {"AmpAttack", "AmpHold", "AmpDecay"}; break;
		case Curve::Filter:
			names = mm ? std::vector<const char*>{"FilterBase", "FilterWidth", "FilterHighpassQ", "FilterLowpassQ"}
				: std::vector<const char*>{"FilterBase", "FilterWidth", "FilterQ", "FilterQ"};
			break;
		case Curve::Eq:
			names = mm ? std::vector<const char*>{"EffectsEqFrequency", "EffectsEqGain"}
				: std::vector<const char*>{"EQFrequency", "EQGain"};
			break;
		}
		std::vector<int> result;
		const auto part = m_controller.getCurrentPart();
		for(const auto* name : names)
		{
			const auto* parameter = m_controller.getParameter(name, part);
			result.push_back(parameter ? static_cast<int>(parameter->getUnnormalizedValue()) : 0);
		}
		return result;
	}

	bool CurveView::update()
	{
		if(!m_shown)
			return false;
		std::vector<int> all{static_cast<int>(m_controller.getCurrentPart())};
		for(const auto& canvas : m_canvases)
		{
			const auto v = values(canvas.curve);
			all.insert(all.end(), v.begin(), v.end());
		}
		if(all == m_shownValues)
			return false;
		m_shownValues = std::move(all);
		for(const auto& canvas : m_canvases)
			canvas.canvas->repaint();
		return true;
	}

	float CurveView::level(const Curve _curve, const std::vector<int>& _values, const float _x)
	{
		switch(_curve)
		{
		case Curve::Amp:
		{
			// Attack, hold and decay share the width in proportion to their values.
			const auto a = static_cast<float>(_values.at(0)) + 1.0f;
			const auto h = static_cast<float>(_values.at(1)) + 1.0f;
			const auto d = static_cast<float>(_values.at(2)) + 1.0f;
			const auto t = _x * (a + h + d);
			if(t < a)
				return t / a;
			if(t < a + h)
				return 1.0f;
			return std::max(0.0f, 1.0f - (t - a - h) / d);
		}
		case Curve::Filter:
		{
			// The pass band runs from BASE to BASE + WIDTH.
			const auto f = frequency(_x);
			const auto high = cutoff(_values.at(0));
			const auto low = cutoff(_values.at(0) + _values.at(1));
			return toDb(highPass(f / high, resonance(_values.at(2))) * lowPass(f / low, resonance(_values.at(3))));
		}
		case Curve::Eq:
		{
			// A one-octave bell; 64 is flat, 0 and 127 about -18 and +18 dB.
			const auto octaves = std::log2(frequency(_x) / cutoff(_values.at(0)));
			const auto gain = (static_cast<float>(_values.at(1)) - 64.0f) / 64.0f * 18.0f;
			return gain * std::exp(-octaves * octaves * 2.0f);
		}
		}
		return 0.0f;
	}

	void CurveView::paint(const Curve _curve, juce::Image& _image, juce::Graphics& _g) const
	{
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		_image.clear(_image.getBounds());
		if(w < 4.0f || h < 4.0f)
			return;

		const bool envelope = _curve == Curve::Amp;
		const auto toY = [&](const float _level)
		{
			const auto n = envelope ? _level : (std::clamp(_level, g_minDb, g_maxDb) - g_minDb) / (g_maxDb - g_minDb);
			return h - 4.0f - n * (h - 8.0f);
		};

		// Quarters of the axis, and 0 dB (or silence for the envelope).
		_g.setColour(g_grid);
		for(int i = 1; i < 4; ++i)
			_g.drawVerticalLine(static_cast<int>(w * static_cast<float>(i) / 4.0f), 0.0f, h);
		_g.drawHorizontalLine(static_cast<int>(toY(0.0f)), 0.0f, w);

		const auto values = this->values(_curve);
		juce::Path path;
		const int steps = std::max(2, static_cast<int>(w / 2.0f));
		for(int i = 0; i <= steps; ++i)
		{
			const auto x = static_cast<float>(i) / static_cast<float>(steps);
			const auto y = toY(level(_curve, values, x));
			if(i == 0)
				path.startNewSubPath(0.0f, y);
			else
				path.lineTo(x * w, y);
		}
		auto area = path;
		area.lineTo(w, h);
		area.lineTo(0.0f, h);
		area.closeSubPath();
		_g.setColour(g_line.withAlpha(0.12f));
		_g.fillPath(area);
		_g.setColour(g_line);
		_g.strokePath(path, juce::PathStrokeType(std::max(1.5f, h / 60.0f)));
	}
}
