#include "mdDrumsHitView.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

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

		// A Track's sound (HitView::SoundCount): Machine, SYN1-8 from index 1, the other 16 parameters, the LFO, Level
		constexpr std::array<const char*, HitView::SoundCount> g_sound{
			"Machine",
			"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
			"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM",
			"LfoTrack", "LfoParam", "LfoShape1", "LfoShape2", "LfoMode",
			"Level"};
		constexpr size_t g_syn = 1;

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

	HitView::HitView(Editor& _editor, Telemetry& _telemetry, const int _refreshHz, std::vector<std::filesystem::path> _romFolders)
		: m_editor(_editor), m_telemetry(_telemetry), m_client(std::move(_romFolders))
	{
		createScreen(m_screens[0], "screen_hit", "hit_tick");
		createScreen(m_screens[1], "screen_preview", "preview_tick");
		startTimerHz(std::clamp(_refreshHz, 30, 300));
	}

	HitView::~HitView()
	{
		stopTimer();
	}

	void HitView::createScreen(Screen& _screen, const std::string& _id, const std::string& _ticks)
	{
		_screen.ticks = _ticks;
		_screen.element = find(_id);
		if(_screen.element)
		{
			auto* canvas = juceRmlUi::ElemCanvas::create(_screen.element);
			_screen.canvas = canvas;
			canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			canvas->setPixelAligned(true);
			canvas->setClearEveryFrame(true);
			canvas->setRepaintGraphicsCallback([this, &_screen](const juce::Image&, juce::Graphics& _g)
			{
				const auto size = _screen.canvas->getPaintSize();
				paint(_g, static_cast<float>(size.x), static_cast<float>(size.y), _screen);
			});
		}
		updateTicks(_screen);
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

	void HitView::bindSound()
	{
		auto& controller = m_editor.getProcessor().getController();
		const auto part = static_cast<uint8_t>(m_track);
		for(size_t i = 0; i < SoundCount; ++i)
		{
			m_soundParameters[i] = controller.getParameter(g_sound[i], part);
			m_raw[i] = m_soundParameters[i] ? m_soundParameters[i]->getUnnormalizedValue() : 0;
		}
		m_sound = m_raw;
		m_chosenMachine = -1;
		m_soundChanged = false;
	}

	std::array<int, HitView::SoundCount> HitView::sound(const double _nowMs)
	{
		std::array<int, SoundCount> raw{};
		for(size_t i = 0; i < SoundCount; ++i)
			raw[i] = m_soundParameters[i] ? m_soundParameters[i]->getUnnormalizedValue() : 0;
		const auto syn = [](const std::array<int, SoundCount>& _values)
		{
			std::array<int, 8> values{};
			std::copy_n(_values.begin() + g_syn, values.size(), values.begin());
			return values;
		};

		// A machine chosen alone: the Device gives it its own SYN1-8 and reports them a block later. Until they change
		// (a Kit's machine comes with its own at once), the sound is the machine's defaults.
		if(raw[0] != m_raw[0])
		{
			m_chosenMachine = syn(raw) == syn(m_raw) ? raw[0] : -1;
			m_chosenAtMs = _nowMs;
			m_synBefore = syn(m_raw);
		}
		m_raw = raw;
		if(m_chosenMachine != raw[0] || _nowMs - m_chosenAtMs >= MachineReportMs || syn(raw) != m_synBefore)
		{
			m_chosenMachine = -1;
			return raw;
		}
		auto& processor = static_cast<Processor&>(m_editor.getProcessor());
		if(const auto machines = processor.getMachines())
		{
			for(const auto& machine : *machines)
			{
				if(machine.id != raw[0])
					continue;
				for(size_t p = 0; p < 8; ++p)
					raw[g_syn + p] = machine.defaults[p];
				break;
			}
		}
		return raw;
	}

	void HitView::requestPreview()
	{
		auto& controller = static_cast<Controller&>(m_editor.getProcessor().getController());
		const auto track = static_cast<uint8_t>(m_track);
		HitPreview::Request request;
		request.kit = controller.playedKit();
		request.track = track;
		for(size_t p = 0; p < 8; ++p)
			request.kit.parameters[track][p] = static_cast<uint8_t>(m_sound[g_syn + p]);
		const auto* tracks = m_editor.getTrackView();
		request.velocity = m_current.id ? m_current.velocity
			: static_cast<uint8_t>(tracks ? tracks->getPlayVelocity() : TrackView::ListenVelocity);
		m_requested = m_client.request(std::move(request));
	}

	void HitView::update(const double _nowMs)
	{
		const auto track = static_cast<int>(m_editor.getProcessor().getController().getCurrentPart());
		if(track != m_track)
		{
			m_track = track;
			m_current = {};
			m_previous = {};
			m_requested = 0;
			m_previewReady = false;
			m_showPreview = false;
			bindSound();
		}
		const auto id = m_current.id;
		const auto count = m_current.count;
		m_telemetry.readCapture(track, false, m_current);
		m_telemetry.readCapture(track, true, m_previous);
		if(m_current.id != id || m_current.count != count)
			m_lastGrowthMs = _nowMs;
		// Playing while the capture grows: until it is full, or 50 ms after it stopped (the Track struck again)
		m_playing = m_current.id && m_current.count < Telemetry::CaptureColumns && _nowMs - m_lastGrowthMs < 50.0;

		// The sound changed: its Hit is asked for while a screen shows and no Hit of the Track plays (a sequence playing
		// shows the sound at each Hit, and would keep the preview's thread busy for nothing), unless the Track sounds it
		// first
		const bool visible = std::any_of(m_screens.begin(), m_screens.end(),
			[](const Screen& _s) { return _s.element && _s.element->IsVisible(true); });
		if(const auto now = sound(_nowMs); now != m_sound)
		{
			m_sound = now;
			m_soundChanged = true;
			m_liveAtRequest = m_current.id;
		}
		if(m_current.id != m_liveAtRequest)
		{
			m_soundChanged = false;
			m_requested = 0;
		}
		if(m_soundChanged && visible && !m_playing)
		{
			m_soundChanged = false;
			requestPreview();
		}
		if(m_client.take(m_result) && m_requested && m_result.id == m_requested)
		{
			m_requested = 0;
			if(m_result.capture.id)
			{
				std::swap(m_preview, m_result.capture);
				m_previewReady = true;
				m_liveAtPreview = m_liveAtRequest;
				++m_previews;
			}
		}
		// A Hit since the change shows the sound: the preview gives way. One playing from before plays to its end.
		if(m_previewReady && m_current.id != m_liveAtPreview)
			m_previewReady = false;
		m_showPreview = m_previewReady && !m_playing;

		// While the Hit plays, the window keeps the Hit before it if that was longer: it does not grow under the playhead
		const auto window = m_showPreview ? fit(m_preview)
			: m_playing && m_previous.id ? std::max(fit(m_current), fit(m_previous)) : fit(m_current);
		if(window != m_window)
		{
			m_window = window;
			for(const auto& screen : m_screens)
				updateTicks(screen);
		}

		if(auto* note = find("hit_note"))
		{
			const auto text = m_showPreview ? "Preview, velocity " + std::to_string(m_preview.velocity)
				: m_current.id ? "Last hit, velocity " + std::to_string(m_current.velocity) : std::string("Not played yet");
			if(note->GetInnerRML() != text)
				note->SetInnerRML(text);
		}

		const auto& shownCapture = shown();
		const Shown state{shownCapture.id, shownCapture.count, m_showPreview ? 0 : m_previous.id, m_window, track,
			!m_showPreview && m_playing, m_showPreview ? m_previews : 0};
		for(auto& screen : m_screens)
		{
			if(!screen.canvas || screen.drawn == state || !screen.element->IsVisible(true))
				continue;
			screen.drawn = state;
			screen.canvas->repaint();
		}
	}

	void HitView::updateTicks(const Screen& _screen) const
	{
		// The graduations' words, 4 px after each line, on a baseline 16 px over the screen's foot
		const auto step = tickMs(m_window);
		const auto plotWidth = g_screenWidth - g_plotLeft - g_plotRight;
		for(int i = 0; i < TickCount; ++i)
		{
			auto* tick = find(_screen.ticks + std::to_string(i + 1));
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

	void HitView::paint(juce::Graphics& _g, const float _w, const float _h, const Screen& _screen) const
	{
		const auto* context = _screen.element->GetContext();
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

		// What is shown: the live capture and the Hit before it, or a preview alone
		const auto& current = shown();
		const auto* previous = m_showPreview || !m_previous.id ? nullptr : &m_previous;
		const bool playing = !m_showPreview && m_playing;

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
			if(current.id && c0 < current.count)
				capture = &current;
			else if(previous && c0 < previous->count)
				capture = previous;
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
			_g.setColour(capture == &current ? g_ink : g_track);
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

		if(playing)
		{
			const auto x = left + static_cast<float>(current.count * g_columnSeconds / m_window) * width;
			if(x < right)
			{
				_g.setColour(g_accent);
				_g.fillRect(std::round(x), top, dp, bottom - top);
			}
		}
	}
}
