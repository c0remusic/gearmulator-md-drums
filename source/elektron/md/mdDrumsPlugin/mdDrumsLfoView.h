#pragma once

#include "mdDrumsLfo.h"

#include <array>
#include <cstdint>
#include <string>

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
	class Knob;
	struct Frame;

	// The LFO screen's behaviour (Design/HANDOFF.md, "LFO"; ticket 22):
	// - the plot: the shown Track's LFO as the OS runs it (Lfo, bit-exact), moving its target's value from a Hit of the
	//   Track at the plot's left edge, 1 to 6 periods as LFOS rises (more for a RMP's or an EXP's whole fall), zoomed on
	//   the values it reaches, which it names at the plot's top and foot; "LFOD 0: no modulation" without depth;
	// - ASSIGN: armed by "Assign", every knob of the bands a target, the title asking for one; a click on a knob, on this
	//   Track or on another one shown meanwhile, gives the LFO that armed it its target, and shows its Track again; Cancel,
	//   Esc, a tab or an overlay disarm it;
	// - the bands' names that an LFO modulates (its target, with some depth), underlined in accent, and their knobs a
	//   dot where the engine plays the value (the Telemetry's, ticket 40).
	// The editor's frame traces the plot again when its LFO, its target's value or the tempo changed, underlines the
	// names whose modulation changed and, armed, rings the knobs the shown Track's machine uses.
	class LfoView
	{
	public:
		explicit LfoView(Editor& _editor);
		~LfoView();

		LfoView(const LfoView&) = delete;
		LfoView& operator=(const LfoView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

		void arm();
		void disarm();
		bool isArmed() const { return m_armed; }
		// Esc: disarms; false when there was nothing to do
		bool escape();

		const Lfo::Trace& trace() const { return m_trace; }

	private:
		// A Track's parameters that the screen reads, in m_parameters
		enum Index
		{
			Shape1, Shape2, Mode, Speed, Depth, Mix, Track, Param, Machine, IndexCount
		};

		// What the plot was traced from, in the order of Index for the LFO's own six: then its target's value and the
		// tempo as the OS takes it (Lfo::tempo); all 0 without depth
		static constexpr size_t TargetValue = 6;
		using TraceKey = std::array<int, 8>;

		bool followTrace();
		bool followUnderlines();
		bool followPlayed();
		bool followTargets();
		void updateTargets();
		void updateTitle() const;
		void pick(int _param);
		void paint(juce::Graphics& _g, float _dp) const;
		int value(Index _index, uint8_t _part) const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		std::array<std::array<pluginLib::Parameter*, IndexCount>, 16> m_parameters{};
		uint8_t m_part = 0;
		bool m_armed = false;
		uint8_t m_owner = 0;
		Lfo::Trace m_trace;

		// The LFO's target, found by name when LfoTrack or LfoParam change
		pluginLib::Parameter* m_target = nullptr;
		std::array<int, 2> m_targetOf{-1, -1};
		// What the document shows: -1 for not yet
		TraceKey m_traced{};
		int64_t m_underlined = -1;	// a bit per name of Engine::paramNames
		std::array<Knob*, 24> m_knobs{};	// the bands' knobs, by parameter (Engine::ParamCount)
		int m_ringedFor = -1;		// the Track and its machine the rings were drawn for, while armed
	};
}
