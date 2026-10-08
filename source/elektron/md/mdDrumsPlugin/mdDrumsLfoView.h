#pragma once

#include "mdDrumsLfo.h"

#include "baseLib/event.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace juce
{
	class Graphics;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;

	// The LFO screen's behaviour (Design/HANDOFF.md, "LFO"; ticket 22):
	// - the plot: the shown Track's LFO as the OS runs it (Lfo, bit-exact), moving its target's value from a Hit of the
	//   Track at the plot's left edge, 1 to 6 periods as LFOS rises (more for a RMP's or an EXP's whole fall), zoomed on
	//   the values it reaches, which it names at the plot's top and foot; "LFOD 0: no modulation" without depth;
	// - ASSIGN: armed by "Assign", every knob of the bands a target, the title asking for one; a click on a knob, on this
	//   Track or on another one shown meanwhile, gives the LFO that armed it its target, and shows its Track again; Cancel,
	//   Esc, a tab or an overlay disarm it;
	// - the bands' names that an LFO modulates (its target, with some depth), underlined in accent.
	class LfoView
	{
	public:
		explicit LfoView(Editor& _editor);
		~LfoView();

		LfoView(const LfoView&) = delete;
		LfoView& operator=(const LfoView&) = delete;

		void arm();
		void disarm();
		bool isArmed() const { return m_armed; }
		// Esc: disarms; false when there was nothing to do
		bool escape();

		const Lfo::Trace& trace() const { return m_trace; }
		// Reads the parameters again (their notifications reach it later, from the message loop)
		void refresh();

	private:
		void onPartChanged(uint8_t _part);
		void listenToTarget();
		void update();
		void updateUnderlines() const;
		void updateTargets();
		void updateTitle() const;
		void pick(int _param);
		void paint(juce::Graphics& _g) const;
		int value(const char* _name, uint8_t _part) const;
		pluginLib::Parameter* parameter(const char* _name, uint8_t _part) const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		Rml::Element* m_screen = nullptr;
		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		uint8_t m_part = 0;
		bool m_armed = false;
		uint8_t m_owner = 0;
		Lfo::Trace m_trace;
		int m_depth = 0;

		baseLib::EventListener<uint8_t> m_onPartChanged;
		std::vector<baseLib::EventListener<pluginLib::Parameter*>> m_onShown;		// the shown Track's LFO and machine
		baseLib::EventListener<pluginLib::Parameter*> m_onTarget;					// the value it moves
		std::vector<baseLib::EventListener<pluginLib::Parameter*>> m_onAnyLfo;	// every Track's target and depth
	};
}
