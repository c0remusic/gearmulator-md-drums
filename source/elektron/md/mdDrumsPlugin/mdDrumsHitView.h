#pragma once

#include "mdDrumsHitPreview.h"
#include "mdDrumsTelemetry.h"

#include "juce_events/juce_events.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

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

namespace pluginLib
{
	class Parameter;
}

namespace mdDrums
{
	class Editor;

	// The Hit screen (Design/HANDOFF.md, "Hit"; tickets 21 and 28 of the editor map), and the machine browser's preview
	// screen, which shows the same: the shown Track's last Hit from the captures the Device writes in the Telemetry, as
	// min/max columns, drawn as it plays in ink behind a 1 px accent playhead, the rest of the window showing the Hit
	// before it in the controls' border colour, and its amplitude envelope in accent.
	// When the Track's sound changes (its machine, 24 parameters, LFO or level), its Hit at the last Hit's velocity (else
	// the head band's) is rendered on the process's HitPreview and shown once rendered, "Preview, velocity n", until a Hit
	// of the Track sounds again; a Hit still playing when the sound changed plays to its end first. A machine just chosen
	// previews with its own SYN1-8, which the Device reports a block later.
	// The window fits the Hit (0.1 to 0.8 s, ticks in ms); full scale is ±0.5 at the plot's edges, as the mockup draws
	// it. A timer at the display's rate reads the captures, the sound and the preview's result, and repaints a visible
	// screen only when what it shows changed.
	class HitView : juce::Timer
	{
	public:
		static constexpr std::array<double, 6> Windows{0.1, 0.2, 0.3, 0.4, 0.6, 0.8};
		static constexpr double SampleRate = 44100.0;
		static constexpr int TickCount = 8;
		// A Track's sound as host parameters: Machine, the 24 parameters, the LFO's five, Level
		static constexpr int SoundCount = 31;
		// How long a machine just chosen previews with its own SYN1-8, whatever its parameters still say
		static constexpr double MachineReportMs = 500.0;

		// _romFolders: where the Processor looks for the flash image, for the preview's engine
		HitView(Editor& _editor, Telemetry& _telemetry, int _refreshHz, std::vector<std::filesystem::path> _romFolders);
		~HitView() override;

		HitView(const HitView&) = delete;
		HitView& operator=(const HitView&) = delete;

		// One read of the captures, the sound and the preview at _nowMs (the timer's, or a test's)
		void update(double _nowMs);

		double window() const { return m_window; }
		const Telemetry::Capture& current() const { return m_current; }
		bool playing() const { return m_playing; }
		// The screens show a preview; one is being rendered
		bool previewing() const { return m_showPreview; }
		bool previewPending() const { return m_requested != 0; }
		const Telemetry::Capture& preview() const { return m_preview; }

		// The window that fits a capture: the first of Windows at least 1.1 times as long as what of it is audible
		static double fit(const Telemetry::Capture& _capture);

	private:
		// What a screen's canvas shows: the capture and its length, the Hit before it, the window, the Track, the
		// playhead, the preview shown (0: the live capture)
		using Shown = std::tuple<uint32_t, int, uint32_t, double, int, bool, uint32_t>;

		struct Screen
		{
			Rml::Element* element = nullptr;
			juceRmlUi::ElemCanvas* canvas = nullptr;
			std::string ticks;	// the graduations' IDs, less their number
			Shown drawn{0, -1, 0, 0.0, -1, false, 0};
		};

		void timerCallback() override;
		void createScreen(Screen& _screen, const std::string& _id, const std::string& _ticks);
		void bindSound();
		std::array<int, SoundCount> sound(double _nowMs);
		void requestPreview();
		void paint(juce::Graphics& _g, float _w, float _h, const Screen& _screen) const;
		void updateTicks(const Screen& _screen) const;
		Rml::Element* find(const std::string& _id) const;

		const Telemetry::Capture& shown() const { return m_showPreview ? m_preview : m_current; }

		Editor& m_editor;
		Telemetry& m_telemetry;
		std::array<Screen, 2> m_screens;	// the Hit screen, the browser's preview

		int m_track = -1;
		Telemetry::Capture m_current;
		Telemetry::Capture m_previous;
		double m_window = 0.1;
		double m_lastGrowthMs = 0.0;
		bool m_playing = false;

		// The preview
		HitPreview::Client m_client;
		std::array<pluginLib::Parameter*, SoundCount> m_soundParameters{};
		std::array<int, SoundCount> m_raw{};	// the parameters as last read
		std::array<int, SoundCount> m_sound{};	// the sound last asked for, or the Track's when shown
		int m_chosenMachine = -1;				// a machine just chosen alone, previewed with its own SYN1-8
		double m_chosenAtMs = 0.0;
		std::array<int, 8> m_synBefore{};		// the SYN1-8 it came with, until the Device reports its own
		bool m_soundChanged = false;	// a change not asked for yet: no screen showed
		uint32_t m_requested = 0;		// the request being rendered; 0: none
		uint32_t m_liveAtRequest = 0;	// the live capture when the sound changed
		uint32_t m_liveAtPreview = 0;	// and when the preview held was asked for
		HitPreview::Result m_result;
		Telemetry::Capture m_preview;
		bool m_previewReady = false;	// a preview newer than the live capture
		bool m_showPreview = false;
		uint32_t m_previews = 0;		// previews received, to tell them apart
	};
}
