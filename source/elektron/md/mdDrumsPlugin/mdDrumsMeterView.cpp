#include "mdDrumsMeterView.h"

#include "mdDrumsEditor.h"

#include "mdDrumsTelemetry.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>

namespace mdDrums
{
	namespace
	{
		// HANDOFF.md, "Colour"
		const juce::Colour g_accent(0xff6fd1c4);
		const juce::Colour g_ink(0xfff2f2f2);
		const juce::Colour g_wave(0xffa7aaaf);

		constexpr int g_stripHeight = 492;	// a strip's meter, in its own pixels at 100 %
		constexpr int g_mainWidth = 64;
	}

	MeterView::MeterView(Editor& _editor, Telemetry& _telemetry, const int _refreshHz)
		: m_editor(_editor), m_telemetry(_telemetry)
	{
		m_db.fill(FloorDb - 1.0f);
		for(int t = 0; t < TrackCount; ++t)
			m_hits[static_cast<size_t>(t)] = m_telemetry.hits(t);

		m_main = attach("main_meter", [this](juce::Graphics& _g, const float _w, const float _h)
		{
			// Two bars of 2 px, 4 px apart, centred in the meter's 8 (y 1 and 5)
			const auto unit = _h / 8.0f;
			_g.setColour(g_accent);
			_g.fillRect(0.0f, unit, level(0) * _w, 2.0f * unit);
			_g.fillRect(0.0f, 5.0f * unit, level(1) * _w, 2.0f * unit);
		});
		for(int t = 0; t < TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			m_strips[static_cast<size_t>(t)] = attach("strip" + n + "_meter", [this, t](juce::Graphics& _g, const float _w, const float _h)
			{
				const auto height = level(2 + t) * _h;
				_g.setColour(g_accent);
				_g.fillRect(0.0f, _h - height, _w, height);
			});
			m_scopes[static_cast<size_t>(t)] = attach("row" + n + "_scope", [this, t](juce::Graphics& _g, const float _w, const float _h)
			{
				paintScope(t, _g, _w, _h);
			});
		}
		startTimerHz(std::clamp(_refreshHz, 30, 300));
	}

	MeterView::~MeterView()
	{
		stopTimer();
	}

	MeterView::Canvas MeterView::attach(const std::string& _id, const std::function<void(juce::Graphics&, float, float)>& _paint)
	{
		Canvas result;
		result.element = m_editor.findChild(_id, false);
		if(!result.element)
			return result;
		auto* canvas = juceRmlUi::ElemCanvas::create(result.element);
		canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		canvas->setPixelAligned(true);
		canvas->setClearEveryFrame(true);
		canvas->setRepaintGraphicsCallback([canvas, _paint](const juce::Image&, juce::Graphics& _g)
		{
			const auto size = canvas->getPaintSize();
			_paint(_g, static_cast<float>(size.x), static_cast<float>(size.y));
		});
		result.canvas = canvas;
		return result;
	}

	float MeterView::level(const int _output) const
	{
		return std::clamp((m_db[static_cast<size_t>(_output)] - FloorDb) / -FloorDb, 0.0f, 1.0f);
	}

	void MeterView::timerCallback()
	{
		auto* component = m_editor.getRmlComponent();
		if(!component)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		update(juce::Time::getMillisecondCounterHiRes());
	}

	void MeterView::update(const double _nowMs)
	{
		const auto elapsed = m_lastMs < 0.0 ? 0.0 : std::max(0.0, _nowMs - m_lastMs);
		m_lastMs = _nowMs;
		const auto fall = static_cast<float>(FallDbPerSecond * elapsed / 1000.0);

		// Rise at once to the peak since the last read, else fall
		for(int o = 0; o < OutputCount; ++o)
		{
			const auto peak = m_telemetry.takePeak(o);
			const auto db = peak > 0.0f ? 20.0f * std::log10(peak) : FloorDb - 1.0f;
			auto& shown = m_db[static_cast<size_t>(o)];
			shown = std::max(FloorDb - 1.0f, std::max(db, shown - fall));
		}

		repaint(m_main, juce::roundToInt(level(0) * g_mainWidth) * 1000 + juce::roundToInt(level(1) * g_mainWidth));
		for(int t = 0; t < TrackCount; ++t)
			repaint(m_strips[static_cast<size_t>(t)], juce::roundToInt(level(2 + t) * g_stripHeight));

		// The scopes: a column every 1/32 s, the highest level of its time; after a pause, start afresh
		if(_nowMs - m_columnStartMs > 1000.0)
			m_columnStartMs = _nowMs;
		for(int t = 0; t < TrackCount; ++t)
			m_columnPeak[static_cast<size_t>(t)] = std::max(m_columnPeak[static_cast<size_t>(t)], level(2 + t));
		while(_nowMs - m_columnStartMs >= ScopeColumnMs)
		{
			for(int t = 0; t < TrackCount; ++t)
			{
				auto& history = m_history[static_cast<size_t>(t)];
				std::rotate(history.begin(), history.begin() + 1, history.end());
				history.back() = m_columnPeak[static_cast<size_t>(t)];
				m_columnPeak[static_cast<size_t>(t)] = 0.0f;
			}
			m_columnStartMs += ScopeColumnMs;
			++m_columns;
		}
		const auto shown = static_cast<int>(m_editor.getProcessor().getController().getCurrentPart());
		for(int t = 0; t < TrackCount; ++t)
		{
			const auto& history = m_history[static_cast<size_t>(t)];
			const bool silent = std::all_of(history.begin(), history.end(), [](const float _v) { return _v <= 0.0f; });
			// Silent: drawn once; else at each new column, and when the shown Track changes its colour
			repaint(m_scopes[static_cast<size_t>(t)], silent ? -2 : (m_columns % 1000000) * 2 + (t == shown ? 1 : 0));
		}

		// A struck Track's number, in the list, in Mix and among the sends, lit for 140 ms
		for(int t = 0; t < TrackCount; ++t)
		{
			const auto hits = m_telemetry.hits(t);
			if(hits != m_hits[static_cast<size_t>(t)])
			{
				m_hits[static_cast<size_t>(t)] = hits;
				m_litUntil[static_cast<size_t>(t)] = _nowMs + LightMs;
			}
			const bool lit = _nowMs < m_litUntil[static_cast<size_t>(t)];
			if(lit == m_lit[static_cast<size_t>(t)])
				continue;
			m_lit[static_cast<size_t>(t)] = lit;
			const auto n = std::to_string(t + 1);
			for(const auto& id : {"row" + n + "_num", "strip" + n + "_num", "send" + n + "_num"})
				if(auto* e = m_editor.findChild(id, false))
					e->SetClass("trig", lit);
		}
	}

	void MeterView::repaint(Canvas& _canvas, const int _shows) const
	{
		if(!_canvas.canvas || _shows == _canvas.drawn || !_canvas.element->IsVisible(true))
			return;
		_canvas.drawn = _shows;
		_canvas.canvas->repaint();
	}

	void MeterView::paintScope(const int _track, juce::Graphics& _g, const float _w, const float _h) const
	{
		// The mockup's scope: a line per pixel column, its height the level, centred; the shown Track's in ink
		const auto* context = m_scopes[static_cast<size_t>(_track)].element->GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
		const auto& history = m_history[static_cast<size_t>(_track)];
		const bool shown = static_cast<int>(m_editor.getProcessor().getController().getCurrentPart()) == _track;
		_g.setColour(shown ? g_ink : g_wave);
		const auto centre = _h * 0.5f;
		const auto columns = static_cast<int>(_w);
		for(int x = 0; x < columns; ++x)
		{
			const auto value = history[static_cast<size_t>(x * ScopeColumns / std::max(1, columns))];
			const auto half = value * (centre - 2.0f * dp);
			if(half < 0.5f)
				continue;
			_g.fillRect(static_cast<float>(x), centre - half, 1.0f, 2.0f * half);
		}
	}
}
