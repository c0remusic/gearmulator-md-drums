#pragma once

#include "mdDrumsTelemetry.h"

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
	struct Frame;

	// The v22 skin's meters, scopes and Hit lights (ticket 20), from the Telemetry the Device writes: the Main meter
	// (the top bar's two bars), each Mix strip's meter, each list row's scope (its strip's level over the last second)
	// and each Track's number lit in accent for 140 ms when it is struck. Meters rise at once and fall 20 dB a second,
	// on a scale from -60 to 0 dBFS. The editor's frame reads the Telemetry; a canvas is repainted only when what it
	// shows moves, and not at all while it is hidden.
	class MeterView
	{
	public:
		static constexpr float FloorDb = -60.0f;
		static constexpr float FallDbPerSecond = 20.0f;
		static constexpr int ScopeColumns = 32;
		static constexpr double ScopeColumnMs = 1000.0 / ScopeColumns;	// a second across the scope
		static constexpr double LightMs = 140.0;

		MeterView(Editor& _editor, Telemetry& _telemetry);
		~MeterView();

		MeterView(const MeterView&) = delete;
		MeterView& operator=(const MeterView&) = delete;

		// The editor's frame: one read of the Telemetry; true when it changed the document
		bool step(const Frame& _frame);

		// An output's level as the meters show it, 0 (-60 dBFS or less) to 1 (0 dBFS)
		float level(int _output) const;

	private:
		struct Canvas
		{
			Rml::Element* element = nullptr;
			juceRmlUi::ElemCanvas* canvas = nullptr;
			int drawn = -1;	// what it shows, in its own units (pixels, a column count): repainted when it changes
		};

		// What a canvas paints: its size in pixels and its dp ratio (createCanvas)
		using Paint = std::function<void(juce::Graphics& _g, float _w, float _h, float _dp)>;

		Canvas attach(const std::string& _id, const Paint& _paint);
		// True when it repainted
		bool repaint(Canvas& _canvas, int _shows) const;
		void paintScope(int _track, juce::Graphics& _g, float _w, float _h, float _dp) const;

		Editor& m_editor;
		Telemetry& m_telemetry;
		double m_lastMs = -1.0;
		int m_shown = -1;	// the Track shown, whose scope is drawn in ink
		std::array<std::array<Rml::Element*, 3>, Telemetry::TrackCount> m_numbers{};	// lit by a Hit

		std::array<float, Telemetry::OutputCount> m_db{};	// the meters' level in dB, after the fall
		Canvas m_main;
		std::array<Canvas, Telemetry::TrackCount> m_strips;
		std::array<Canvas, Telemetry::TrackCount> m_scopes;
		std::array<std::array<float, ScopeColumns>, Telemetry::TrackCount> m_history{};	// oldest first
		std::array<float, Telemetry::TrackCount> m_columnPeak{};	// the column being filled
		double m_columnStartMs = 0.0;
		int m_columns = 0;	// columns shifted so far, to know when the scopes moved

		std::array<uint32_t, Telemetry::TrackCount> m_hits{};
		std::array<double, Telemetry::TrackCount> m_litUntil{};
		std::array<bool, Telemetry::TrackCount> m_lit{};
	};
}
