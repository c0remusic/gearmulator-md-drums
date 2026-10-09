#pragma once

#include "juce_events/juce_events.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

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
	class Telemetry;

	// The v22 skin's meters, scopes and Hit lights (ticket 20), from the Telemetry the Device writes: the Main meter
	// (the top bar's two bars), each Mix strip's meter, each list row's scope (its strip's level over the last second)
	// and each Track's number lit in accent for 140 ms when it is struck. Meters rise at once and fall 20 dB a second,
	// on a scale from -60 to 0 dBFS. A timer at the display's rate reads the Telemetry; a canvas is repainted only when
	// what it shows moves, and not at all while it is hidden.
	class MeterView : juce::Timer
	{
	public:
		static constexpr float FloorDb = -60.0f;
		static constexpr float FallDbPerSecond = 20.0f;
		static constexpr int ScopeColumns = 32;
		static constexpr double ScopeColumnMs = 1000.0 / ScopeColumns;	// a second across the scope
		static constexpr double LightMs = 140.0;
		static constexpr int OutputCount = 18;
		static constexpr int TrackCount = 16;

		MeterView(Editor& _editor, Telemetry& _telemetry, int _refreshHz);
		~MeterView() override;

		MeterView(const MeterView&) = delete;
		MeterView& operator=(const MeterView&) = delete;

		// One read of the Telemetry at _nowMs (the timer's, or a test's)
		void update(double _nowMs);

		// An output's level as the meters show it, 0 (-60 dBFS or less) to 1 (0 dBFS)
		float level(int _output) const;

	private:
		struct Canvas
		{
			Rml::Element* element = nullptr;
			juceRmlUi::ElemCanvas* canvas = nullptr;
			int drawn = -1;	// what it shows, in its own units (pixels, a column count): repainted when it changes
		};

		void timerCallback() override;
		Canvas attach(const std::string& _id, const std::function<void(juce::Graphics&, float, float)>& _paint);
		void repaint(Canvas& _canvas, int _shows) const;
		void paintScope(int _track, juce::Graphics& _g, float _w, float _h) const;

		Editor& m_editor;
		Telemetry& m_telemetry;
		double m_lastMs = -1.0;

		std::array<float, OutputCount> m_db{};	// the meters' level in dB, after the fall
		Canvas m_main;
		std::array<Canvas, TrackCount> m_strips;
		std::array<Canvas, TrackCount> m_scopes;
		std::array<std::array<float, ScopeColumns>, TrackCount> m_history{};	// oldest first
		std::array<float, TrackCount> m_columnPeak{};	// the column being filled
		double m_columnStartMs = 0.0;
		int m_columns = 0;	// columns shifted so far, to know when the scopes moved

		std::array<uint32_t, TrackCount> m_hits{};
		std::array<double, TrackCount> m_litUntil{};
		std::array<bool, TrackCount> m_lit{};
	};
}
