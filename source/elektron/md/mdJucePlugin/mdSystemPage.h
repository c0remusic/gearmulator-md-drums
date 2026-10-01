#pragma once

#include "mdLib/mdhostsync.h"
#include "mdLib/mdtypes.h"

#include "synthLib/performanceReport.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;
	class Editor;

	// The SYSTÈME page (board 10), one row per subject with its state and action:
	// the machine's Global (slot, MIDI channels), following the host's tempo,
	// parallel transport, Machinedrum RAM recording, SysEx file transfer, loading
	// a storage image, the GUI scale, performance diagnostics and the plug-in
	// settings: every entry of the plug-in menu (OPTIONS, right click) has a row.
	// Element ids mdSys*: a state line mdSys<Subject>State and its button.
	class SystemPage
	{
	public:
		// The plug-in menu's scales, in percent (PluginEditorState::openMenu), and
		// 130, the scale a new install opens at
		static constexpr std::array<int, 12> Scales{50, 65, 75, 85, 100, 125, 130, 150, 175, 200, 250, 300};

		SystemPage(Editor& _editor, AudioPluginAudioProcessor& _processor, Controller& _controller, Rml::Element& _document);

		// Refreshes what changed while the page is shown, at most twice a second
		// unless a click asked for it. Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// "Global 1 · MIDI : canal de base 1, pistes sur les canaux 1 à 4": the MD
		// uses four consecutive channels, the MM six. 0x7f is NONE, 0xff unknown.
		static std::string globalLine(md::MachineModel _model, uint8_t _global, bool _known, uint8_t _baseChannel);
		static std::string followLine(md::MachineModel _model, md::HostSync::State _state, bool _wanted);
		static std::string diagnosticsLine(std::optional<synthLib::PerformanceReport::Status> _state, bool _folderError);

	private:
		static constexpr double RefreshMilliseconds = 500.0;

		bool setText(Rml::Element* _element, std::string& _shown, const std::string& _text);
		bool setChecked(Rml::Element* _button, int& _shown, bool _checked);
		int currentScale() const;

		Editor& m_editor;
		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;
		const md::MachineModel m_model;

		Rml::Element* m_root = nullptr;
		Rml::Element* m_globalState = nullptr;
		Rml::Element* m_follow = nullptr;
		Rml::Element* m_followState = nullptr;
		Rml::Element* m_parallel = nullptr;
		Rml::Element* m_parallelState = nullptr;
		Rml::Element* m_ram = nullptr;
		Rml::Element* m_ramState = nullptr;
		Rml::Element* m_sysex = nullptr;
		Rml::Element* m_sysexState = nullptr;
		std::array<Rml::Element*, Scales.size()> m_scales{};
		Rml::Element* m_scaleState = nullptr;
		Rml::Element* m_capture = nullptr;
		Rml::Element* m_diagnosticsState = nullptr;

		std::string m_shownGlobal, m_shownFollow, m_shownParallel, m_shownRam, m_shownSysex, m_shownSysexButton;
		std::string m_shownScale, m_shownDiagnostics;
		int m_shownFollowChecked = -1;
		int m_shownParallelChecked = -1;
		int m_shownRamChecked = -1;
		int m_shownScaleChoice = -1;
		int m_shownCaptureChecked = -1;
		double m_lastRefresh = -1.0e9;
		bool m_dirty = true;
	};
}
