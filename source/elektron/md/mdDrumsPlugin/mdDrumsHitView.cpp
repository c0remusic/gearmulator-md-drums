#include "mdDrumsHitView.h"

#include "mdDrumsEditor.h"

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
		const juce::Colour g_ink(0xfff2f2f2);
		const juce::Colour g_track(0xff6e7177);
		const juce::Colour g_accent(0xff6fd1c4);
		const juce::Colour g_tick(0xff3c3e43);

		constexpr double g_columnSeconds = Telemetry::ColumnSamples / HitView::SampleRate;
		constexpr double g_release = 0.03;	// the envelope's, in seconds (the mockup's)
		constexpr float g_fullScale = 0.5f;	// at the plot's edges

		// The screen's plot, in its canvas (which starts after the screen's 1 px left rule): 16 px from the window's
		// screen edges, from 48 px under the screen's top to 36 px over its foot
		constexpr float g_plotLeft = 15.0f, g_plotRight = 16.0f, g_plotTop = 48.0f, g_plotFoot = 36.0f;
		constexpr float g_screenWidth = 399.0f;	// the canvas, inside the rule

		double tickMs(const double _window)
		{
			return _window <= 0.1 ? 25.0 : _window <= 0.2 ? 50.0 : _window <= 0.4 ? 100.0 : 200.0;
		}

		float amplitude(const Telemetry::Column& _column)
		{
			return static_cast<float>(std::max(std::abs(static_cast<int>(_column[0])), std::abs(static_cast<int>(_column[1])))) / 32767.0f;
		}

		std::string dp(const double _value)
		{
			return std::to_string(_value) + "dp";
		}
	}

	HitView::HitView(Editor& _editor, Telemetry& _telemetry, const int _refreshHz)
		: m_editor(_editor), m_telemetry(_telemetry)
	{
		m_screen = find("screen_hit");
		if(m_screen)
		{
			m_canvas = juceRmlUi::ElemCanvas::create(m_screen);
			m_canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_canvas->setPixelAligned(true);
			m_canvas->setClearEveryFrame(true);
			m_canvas->setRepaintGraphicsCallback([this](const juce::Image&, juce::Graphics& _g)
			{
				const auto size = m_canvas->getPaintSize();
				paint(_g, static_cast<float>(size.x), static_cast<float>(size.y));
			});
		}
		updateTicks();
		startTimerHz(std::clamp(_refreshHz, 30, 300));
	}

	HitView::~HitView()
	{
		stopTimer();
	}

	Rml::Element* HitView::find(const std::string& _id) const
	{
		return m_editor.findChild(_id, false);
	}

	void HitView::timerCallback()
	{
		auto* component = m_editor.getRmlComponent();
		if(!component)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		update(juce::Time::getMillisecondCounterHiRes());
	}

	double HitView::fit(const Telemetry::Capture& _capture)
	{
		if(!_capture.id || !_capture.count)
			return Windows.front();
		float peak = 0.0f;
		for(int c = 0; c < _capture.count; ++c)
			peak = std::max(peak, amplitude(_capture.columns[static_cast<size_t>(c)]));
		if(peak <= 0.0f)
			return Windows.front();
		// What is audible: the envelope (an instant attack, a 30 ms release) over 3 % of the peak, as the mockup measures
		const auto release = static_cast<float>(std::exp(-g_columnSeconds / g_release));
		float envelope = 0.0f;
		int last = 0;
		for(int c = 0; c < _capture.count; ++c)
		{
			envelope = std::max(amplitude(_capture.columns[static_cast<size_t>(c)]), envelope * release);
			if(envelope > 0.03f * peak)
				last = c;
		}
		const auto need = 1.1 * (last + 1) * g_columnSeconds;
		for(const auto window : Windows)
			if(window >= need)
				return window;
		return Windows.back();
	}

	void HitView::update(const double _nowMs)
	{
		const auto track = static_cast<int>(m_editor.getProcessor().getController().getCurrentPart());
		if(track != m_track)
		{
			m_track = track;
			m_current = {};
			m_previous = {};
		}
		const auto id = m_current.id;
		const auto count = m_current.count;
		m_telemetry.readCapture(track, false, m_current);
		m_telemetry.readCapture(track, true, m_previous);
		if(m_current.id != id || m_current.count != count)
			m_lastGrowthMs = _nowMs;
		// Playing while the capture grows: until it is full, or 50 ms after it stopped (the Track struck again)
		m_playing = m_current.id && m_current.count < Telemetry::CaptureColumns && _nowMs - m_lastGrowthMs < 50.0;

		// While the Hit plays, the window keeps the Hit before it if that was longer: it does not grow under the playhead
		const auto window = m_playing && m_previous.id ? std::max(fit(m_current), fit(m_previous)) : fit(m_current);
		if(window != m_window)
		{
			m_window = window;
			updateTicks();
		}

		if(auto* note = find("hit_note"))
		{
			const auto text = m_current.id ? "Last hit, velocity " + std::to_string(m_current.velocity) : std::string("Not played yet");
			if(note->GetInnerRML() != text)
				note->SetInnerRML(text);
		}

		const bool changed = m_current.id != m_drawnId || m_current.count != m_drawnCount || m_previous.id != m_drawnPrevious
			|| m_window != m_drawnWindow || track != m_drawnTrack || m_playing != m_drawnPlaying;
		if(!m_canvas || !changed || !m_screen->IsVisible(true))
			return;
		m_drawnId = m_current.id;
		m_drawnCount = m_current.count;
		m_drawnPrevious = m_previous.id;
		m_drawnWindow = m_window;
		m_drawnTrack = track;
		m_drawnPlaying = m_playing;
		m_canvas->repaint();
	}

	void HitView::updateTicks() const
	{
		// The graduations' words, 4 px after each line, on a baseline 16 px over the screen's foot
		const auto step = tickMs(m_window);
		const auto plotWidth = g_screenWidth - g_plotLeft - g_plotRight;
		for(int i = 0; i < TickCount; ++i)
		{
			auto* tick = find("hit_tick" + std::to_string(i + 1));
			if(!tick)
				continue;
			const auto ms = i * step;
			if(ms >= m_window * 1000.0 - 1.0)
			{
				tick->SetProperty("display", "none");
				continue;
			}
			tick->SetInnerRML(i ? std::to_string(static_cast<int>(ms)) + " ms" : std::string("0"));
			tick->SetProperty("left", dp(g_plotLeft + ms / 1000.0 / m_window * plotWidth + 4.0));
			tick->SetProperty("display", "block");
		}
	}

	void HitView::paint(juce::Graphics& _g, const float _w, const float _h) const
	{
		const auto* context = m_screen->GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
		const auto left = g_plotLeft * dp, right = _w - g_plotRight * dp, top = g_plotTop * dp, bottom = _h - g_plotFoot * dp;
		const auto width = right - left, mid = std::round((top + bottom) * 0.5f), half = (bottom - top) * 0.5f;
		if(width <= 0.0f || half <= 0.0f)
			return;

		_g.setColour(g_tick);
		const auto step = tickMs(m_window);
		for(double ms = 0.0; ms < m_window * 1000.0 - 1.0; ms += step)
			_g.fillRect(std::round(left + static_cast<float>(ms / 1000.0 / m_window) * width), top, dp, bottom - top);
		_g.fillRect(left, mid, width, dp);

		// A pixel column: the columns of its time, the current Hit's where it has them, else the previous Hit's
		const auto columnsPerPixel = m_window / g_columnSeconds / static_cast<double>(width);
		const auto release = static_cast<float>(std::exp(-(m_window / width) / g_release));
		juce::Path envelopePath;
		float envelope = 0.0f;
		bool started = false;
		const auto pixels = static_cast<int>(width);
		for(int x = 0; x < pixels; ++x)
		{
			const auto c0 = static_cast<int>(std::floor(x * columnsPerPixel));
			const auto c1 = std::max(c0 + 1, static_cast<int>(std::floor((x + 1) * columnsPerPixel)));
			const Telemetry::Capture* capture = nullptr;
			if(m_current.id && c0 < m_current.count)
				capture = &m_current;
			else if(m_previous.id && c0 < m_previous.count)
				capture = &m_previous;
			if(!capture)
				continue;
			int lo = 32767, hi = -32767;
			for(int c = c0; c < std::min(c1, capture->count); ++c)
			{
				lo = std::min(lo, static_cast<int>(capture->columns[static_cast<size_t>(c)][0]));
				hi = std::max(hi, static_cast<int>(capture->columns[static_cast<size_t>(c)][1]));
			}
			const auto low = static_cast<float>(lo) / 32767.0f, high = static_cast<float>(hi) / 32767.0f;
			const auto y0 = mid - std::clamp(high / g_fullScale, -1.0f, 1.0f) * half;
			const auto y1 = mid - std::clamp(low / g_fullScale, -1.0f, 1.0f) * half;
			_g.setColour(capture == &m_current ? g_ink : g_track);
			_g.fillRect(left + static_cast<float>(x), y0, 1.0f, std::max(1.0f, y1 - y0));

			envelope = std::max(std::max(std::abs(low), std::abs(high)), envelope * release);
			const auto ey = mid - std::min(1.0f, envelope / g_fullScale) * half;
			if(!started)
				envelopePath.startNewSubPath(left + static_cast<float>(x), ey);
			else
				envelopePath.lineTo(left + static_cast<float>(x), ey);
			started = true;
		}
		if(started)
		{
			_g.setColour(g_accent);
			_g.strokePath(envelopePath, juce::PathStrokeType(2.0f * dp));
		}

		if(m_playing)
		{
			const auto x = left + static_cast<float>(m_current.count * g_columnSeconds / m_window) * width;
			if(x < right)
			{
				_g.setColour(g_accent);
				_g.fillRect(std::round(x), top, dp, bottom - top);
			}
		}
	}
}
