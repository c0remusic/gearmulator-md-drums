#pragma once

#include "mdLib/mdhostsync.h"
#include "mdLib/mdtypes.h"

#include <cstdint>
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
	// a storage image, and the plug-in settings. Element ids mdSys*: a state line
	// mdSys<Subject>State and its button.
	class SystemPage
	{
	public:
		SystemPage(Editor& _editor, AudioPluginAudioProcessor& _processor, Controller& _controller, Rml::Element& _document);

		// Refreshes what changed while the page is shown, at most twice a second
		// unless a click asked for it. Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// "Global 1 · MIDI : canal de base 1, pistes sur les canaux 1 à 4": the MD
		// uses four consecutive channels, the MM six. 0x7f is NONE, 0xff unknown.
		static std::string globalLine(md::MachineModel _model, uint8_t _global, bool _known, uint8_t _baseChannel);
		static std::string followLine(md::MachineModel _model, md::HostSync::State _state, bool _wanted);

	private:
		static constexpr double RefreshMilliseconds = 500.0;

		bool setText(Rml::Element* _element, std::string& _shown, const std::string& _text);
		bool setChecked(Rml::Element* _button, int& _shown, bool _checked);

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

		std::string m_shownGlobal, m_shownFollow, m_shownParallel, m_shownRam, m_shownSysex, m_shownSysexButton;
		int m_shownFollowChecked = -1;
		int m_shownParallelChecked = -1;
		int m_shownRamChecked = -1;
		double m_lastRefresh = -1.0e9;
		bool m_dirty = true;
	};
}
