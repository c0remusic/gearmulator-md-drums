#include "mdDrumsFilterView.h"

#include "mdDrumsCanvas.h"
#include "mdDrumsColours.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"
#include "mdDrumsProcessor.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>

namespace mdDrums
{
	namespace
	{
		// The plot, in the screen's canvas (which starts after the screen's 1 px left rule): 16 px from the window's screen
		// edges, from 48 px under the screen's top to 36 px over its foot; 0 dB 45 % down, +24 dB at the top (the mockup's)
		constexpr float g_left = 15.0f, g_width = 368.0f, g_top = 48.0f, g_bottom = 216.0f - 36.0f;
		constexpr float g_zero = g_top + (g_bottom - g_top) * 0.45f;
		constexpr float g_perDb = (g_zero - g_top) / FilterView::DbAbove;

		constexpr std::array<const char*, 6> g_parameters{"EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR"};
		constexpr std::array<double, 3> g_ticks{100.0, 1000.0, 10000.0};
	}

	FilterView::FilterView(Editor& _editor) : m_editor(_editor)
	{
		if(auto* screen = find("screen_filter"))
		{
			m_canvas = createCanvas(screen, [this](juce::Graphics& _g, float, float, const float _dp)
			{
				paint(_g, _dp);
			});
			m_canvas->repaint();
		}

		// The graduations' words, 4 px after their lines, on the axis' baseline; "0 dB" 4 px over its line
		for(size_t i = 0; i < g_ticks.size(); ++i)
			if(auto* tick = find("filter_tick" + std::to_string(i + 1)))
				tick->SetProperty("left", dp(xOf(g_ticks[i]) + 4.0f));

		auto& controller = m_editor.getProcessor().getController();
		for(uint8_t t = 0; t < m_parameters.size(); ++t)
			for(size_t i = 0; i < g_parameters.size(); ++i)
				m_parameters[t][i] = controller.getParameter(g_parameters[i], t);
	}

	FilterView::~FilterView() = default;

	float FilterView::xOf(const double _hz)
	{
		return g_left + static_cast<float>(std::log10(_hz / LowHz) / Decades) * g_width;
	}

	float FilterView::yOf(const double _db)
	{
		return std::clamp(g_zero - static_cast<float>(_db) * g_perDb, g_top, g_bottom);
	}

	Rml::Element* FilterView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	bool FilterView::step(const Frame& _frame)
	{
		// Hidden too (another tab, an overlay over it): RmlUi knows it shows only after the update that follows the
		// frame, and a hidden canvas paints nothing until then
		const auto& parameters = m_parameters[_frame.shown];
		const auto value = [&](const size_t _index)
		{
			const auto* parameter = parameters[_index];
			return parameter ? parameter->getUnnormalizedValue() : 0;
		};
		bool wrote = false;
		if(const auto srr = value(SrrIndex); srr != m_srr)
		{
			m_srr = srr;
			if(auto* note = find("filter_srr"))
			{
				note->SetInnerRML(srr > 0 ? "SRR " + std::to_string(srr) : std::string());
				setDisplay(note, srr > 0, Rml::Style::Display::Inline);
			}
			wrote = true;
		}
		if(!m_response)
		{
			auto tables = m_editor.processor().getFxTables();
			if(!tables)
				return wrote;
			m_response = std::make_unique<FilterResponse>(std::move(tables));
		}
		const FilterResponse::Settings settings{value(0), value(1), value(2), value(3), value(4)};
		if(m_curve && settings == m_response->settings())
			return wrote;
		m_response->set(settings);
		m_curve = true;
		if(m_canvas)
			m_canvas->repaint();
		return true;
	}

	void FilterView::paint(juce::Graphics& _g, const float _dp) const
	{
		_g.setColour(g_tick);
		for(const auto hz : g_ticks)
			_g.fillRect(std::round(xOf(hz) * _dp), g_top * _dp, _dp, (g_bottom - g_top) * _dp);
		_g.fillRect(g_left * _dp, std::round(g_zero * _dp), g_width * _dp, _dp);
		if(!m_response)
			return;

		// A point a pixel, at the frequency of the pixel's left edge
		juce::Path curve;
		const auto pixels = static_cast<int>(std::round(g_width * _dp));
		for(int x = 0; x <= pixels; ++x)
		{
			const auto hz = LowHz * std::pow(10.0, Decades * x / pixels);
			const auto px = g_left * _dp + static_cast<float>(x), py = yOf(m_response->gain(hz)) * _dp;
			if(x)
				curve.lineTo(px, py);
			else
				curve.startNewSubPath(px, py);
		}
		_g.setColour(g_accent);
		_g.strokePath(curve, juce::PathStrokeType(2.0f * _dp));

		// The EQ's point, on the curve
		const auto centre = std::clamp(m_response->eqCentre(), LowHz, LowHz * std::pow(10.0, Decades));
		const auto ex = std::round((xOf(centre) - 3.0f) * _dp);
		const auto ey = std::round((yOf(m_response->gain(centre)) - 3.0f) * _dp);
		_g.setColour(g_ink);
		_g.fillRect(ex, ey, 6.0f * _dp, 6.0f * _dp);
	}
}
