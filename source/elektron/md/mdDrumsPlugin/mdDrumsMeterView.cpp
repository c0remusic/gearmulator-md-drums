#include "mdDrumsMeterView.h"

#include "mdDrumsCanvas.h"
#include "mdDrumsColours.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFrame.h"

#include "juceRmlUi/rmlElemCanvas.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>

namespace mdDrums
{
	MeterView::MeterView(Editor& _editor, Telemetry& _telemetry) : m_editor(_editor), m_telemetry(_telemetry)
	{
		m_db.fill(FloorDb - 1.0f);
		for(int t = 0; t < Telemetry::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			m_numbers[static_cast<size_t>(t)] = {m_editor.element("row" + n + "_num"),
				m_editor.element("strip" + n + "_num"), m_editor.element("send" + n + "_num")};
		}

		m_main = attach("main_meter", [this](juce::Graphics& _g, const float _w, const float _h, float)
		{
			// Two bars of 2 px, 4 px apart, centred in the meter's 8 (y 1 and 5)
			const auto unit = _h / 8.0f;
			_g.setColour(g_accent);
			_g.fillRect(0.0f, unit, level(0) * _w, 2.0f * unit);
			_g.fillRect(0.0f, 5.0f * unit, level(1) * _w, 2.0f * unit);
		});
		for(int t = 0; t < Telemetry::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			m_strips[static_cast<size_t>(t)] = attach("strip" + n + "_meter",
				[this, t](juce::Graphics& _g, const float _w, const float _h, float)
				{
					const auto height = level(2 + t) * _h;
					_g.setColour(g_accent);
					_g.fillRect(0.0f, _h - height, _w, height);
				});
			m_scopes[static_cast<size_t>(t)] = attach("row" + n + "_scope",
				[this, t](juce::Graphics& _g, const float _w, const float _h, const float _dp)
				{
					paintScope(t, _g, _w, _h, _dp);
				});
		}
	}

	MeterView::~MeterView() = default;

	MeterView::Canvas MeterView::attach(const std::string& _id, const Paint& _paint)
	{
		Canvas result;
		result.element = m_editor.element(_id);
		if(!result.element)
			return result;
		result.canvas = createCanvas(result.element, _paint);
		return result;
	}

	float MeterView::level(const int _output) const
	{
		return std::clamp((m_db[static_cast<size_t>(_output)] - FloorDb) / -FloorDb, 0.0f, 1.0f);
	}

	bool MeterView::step(const Frame& _frame)
	{
		const auto nowMs = _frame.now;
		m_shown = _frame.shown;
		const auto elapsed = m_lastMs < 0.0 ? 0.0 : std::max(0.0, nowMs - m_lastMs);
		m_lastMs = nowMs;
		const auto fall = static_cast<float>(FallDbPerSecond * elapsed / 1000.0);

		// After a pause, what piled up meanwhile is no news: the peaks and the Hits are taken without being shown, and
		// the scopes start afresh
		if(_frame.resumed)
		{
			for(int o = 0; o < Telemetry::OutputCount; ++o)
				(void)m_telemetry.takePeak(o);
			m_db.fill(FloorDb - 1.0f);
			for(int t = 0; t < Telemetry::TrackCount; ++t)
				m_hits[static_cast<size_t>(t)] = m_telemetry.hits(t);
			for(auto& history : m_history)
				history.fill(0.0f);
			m_columnPeak.fill(0.0f);
			m_columnStartMs = nowMs;
		}

		// Rise at once to the peak since the last read, else fall
		for(int o = 0; o < Telemetry::OutputCount; ++o)
		{
			const auto peak = m_telemetry.takePeak(o);
			const auto db = peak > 0.0f ? 20.0f * std::log10(peak) : FloorDb - 1.0f;
			auto& shown = m_db[static_cast<size_t>(o)];
			shown = std::max(FloorDb - 1.0f, std::max(db, shown - fall));
		}

		// In the canvases' own pixels: a finer key would repaint them for nothing at a smaller Size
		const auto mainWidth = pixels(m_main, false);
		bool wrote = repaint(m_main, juce::roundToInt(level(0) * mainWidth) * 100000 + juce::roundToInt(level(1) * mainWidth));
		for(int t = 0; t < Telemetry::TrackCount; ++t)
		{
			auto& strip = m_strips[static_cast<size_t>(t)];
			wrote |= repaint(strip, juce::roundToInt(level(2 + t) * pixels(strip, true)));
		}

		// The scopes: a column every 1/32 s, the highest level of its time (after a pause they start afresh, above)
		for(int t = 0; t < Telemetry::TrackCount; ++t)
			m_columnPeak[static_cast<size_t>(t)] = std::max(m_columnPeak[static_cast<size_t>(t)], level(2 + t));
		while(nowMs - m_columnStartMs >= ScopeColumnMs)
		{
			for(int t = 0; t < Telemetry::TrackCount; ++t)
			{
				auto& history = m_history[static_cast<size_t>(t)];
				std::rotate(history.begin(), history.begin() + 1, history.end());
				history.back() = m_columnPeak[static_cast<size_t>(t)];
				m_columnPeak[static_cast<size_t>(t)] = 0.0f;
			}
			m_columnStartMs += ScopeColumnMs;
			++m_columns;
		}
		for(int t = 0; t < Telemetry::TrackCount; ++t)
		{
			const auto& history = m_history[static_cast<size_t>(t)];
			const bool silent = std::all_of(history.begin(), history.end(), [](const float _v) { return _v <= 0.0f; });
			// Silent: drawn once; else at each new column, and when the shown Track changes its colour
			wrote |= repaint(m_scopes[static_cast<size_t>(t)], silent ? -2 : (m_columns % 1000000) * 2 + (t == m_shown ? 1 : 0));
		}

		// A struck Track's number, in the list, in Mix and among the sends, lit for 140 ms
		for(int t = 0; t < Telemetry::TrackCount; ++t)
		{
			const auto hits = m_telemetry.hits(t);
			if(hits != m_hits[static_cast<size_t>(t)])
			{
				m_hits[static_cast<size_t>(t)] = hits;
				m_litUntil[static_cast<size_t>(t)] = nowMs + LightMs;
			}
			const bool lit = nowMs < m_litUntil[static_cast<size_t>(t)];
			if(lit == m_lit[static_cast<size_t>(t)])
				continue;
			m_lit[static_cast<size_t>(t)] = lit;
			for(auto* e : m_numbers[static_cast<size_t>(t)])
				if(e)
					e->SetClass("trig", lit);
			wrote = true;
		}
		return wrote;
	}

	int MeterView::pixels(const Canvas& _canvas, const bool _height)
	{
		if(!_canvas.canvas)
			return 1;
		const auto size = _canvas.canvas->getPaintSize();
		return std::max(1, _height ? size.y : size.x);
	}

	bool MeterView::repaint(Canvas& _canvas, const int _shows) const
	{
		if(!_canvas.canvas || _shows == _canvas.drawn || !_canvas.element->IsVisible(true))
			return false;
		_canvas.drawn = _shows;
		_canvas.canvas->repaint();
		return true;
	}

	void MeterView::paintScope(const int _track, juce::Graphics& _g, const float _w, const float _h,
		const float _dp) const
	{
		// The mockup's scope: a line per pixel column, its height the level, centred; the shown Track's in ink
		const auto& history = m_history[static_cast<size_t>(_track)];
		const bool shown = m_shown == _track;
		_g.setColour(shown ? g_ink : g_inkDim);
		const auto centre = _h * 0.5f;
		const auto columns = static_cast<int>(_w);
		for(int x = 0; x < columns; ++x)
		{
			const auto value = history[static_cast<size_t>(x * ScopeColumns / std::max(1, columns))];
			const auto half = value * (centre - 2.0f * _dp);
			if(half < 0.5f)
				continue;
			_g.fillRect(static_cast<float>(x), centre - half, 1.0f, 2.0f * half);
		}
	}
}
