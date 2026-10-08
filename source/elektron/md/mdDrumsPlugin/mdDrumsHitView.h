#pragma once

#include "mdDrumsTelemetry.h"

#include "juce_events/juce_events.h"

#include <array>
#include <cstdint>

namespace juce
{
	class Graphics;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;

	// The Hit screen (Design/HANDOFF.md, "Hit"; ticket 21), from the Hit captures the Device writes in the Telemetry: the
	// shown Track's last Hit as min/max columns, drawn as it plays in ink behind a 1 px accent playhead, the rest of the
	// window showing the Hit before it in the controls' border colour, and its amplitude envelope in accent. The window
	// fits the Hit (0.1 to 0.8 s, ticks in ms); full scale is ±0.5 at the plot's edges, as the mockup draws it. A timer at
	// the display's rate reads the captures and repaints only when they grow.
	class HitView : juce::Timer
	{
	public:
		static constexpr std::array<double, 6> Windows{0.1, 0.2, 0.3, 0.4, 0.6, 0.8};
		static constexpr double SampleRate = 44100.0;
		static constexpr int TickCount = 8;

		HitView(Editor& _editor, Telemetry& _telemetry, int _refreshHz);
		~HitView() override;

		HitView(const HitView&) = delete;
		HitView& operator=(const HitView&) = delete;

		// One read of the captures at _nowMs (the timer's, or a test's)
		void update(double _nowMs);

		double window() const { return m_window; }
		const Telemetry::Capture& current() const { return m_current; }
		bool playing() const { return m_playing; }

		// The window that fits a capture: the first of Windows at least 1.1 times as long as what of it is audible
		static double fit(const Telemetry::Capture& _capture);

	private:
		void timerCallback() override;
		void paint(juce::Graphics& _g, float _w, float _h) const;
		void updateTicks() const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		Telemetry& m_telemetry;
		Rml::Element* m_screen = nullptr;
		juceRmlUi::ElemCanvas* m_canvas = nullptr;

		int m_track = -1;
		Telemetry::Capture m_current;
		Telemetry::Capture m_previous;
		double m_window = 0.1;
		double m_lastGrowthMs = 0.0;
		bool m_playing = false;

		// What the canvas shows, to repaint only when it changes
		uint32_t m_drawnId = 0;
		int m_drawnCount = -1;
		uint32_t m_drawnPrevious = 0;
		double m_drawnWindow = 0.0;
		int m_drawnTrack = -1;
		bool m_drawnPlaying = false;
	};
}
