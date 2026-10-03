#pragma once

#include "mdLaneInput.h"
#include "mdPatternCommands.h"

#include "mdLib/mdsysexautomation.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace juce
{
	class Graphics;
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// JOUER (Monomachine, boards 1 and 3): the current pattern, the 6 tracks on 32 steps at a time,
	// steps 1-32 or 33-64 (mmPlayPage<n>). A step (mmPlayStep<track>_<step>) shows the note of its trig,
	// dimmed when the trig does not start the amp envelope, framed when it has locks, greyed past the
	// length. Under it, as tabs, the edited track's piano roll (canvases: a keyboard in mmPlayRollKeys,
	// in mmPlayRollArea a bar per trig at its note's height, a step long, two octaves at least) and the
	// lane of one of its parameters (a canvas in mmPlayLaneArea, as on the Machinedrum: the Kit value in
	// grey, a lock in orange with a dot, on the steps that play), chosen by page (mmPlayParamPage<page>)
	// then index (mmPlayParam<index>), each with its count of locks. A click on a track name
	// (mmPlayTrack<track>) makes it the edited track. While the sequencer plays a step of the steps shown,
	// its column is lit (mdPlayNow), as on the Machinedrum.
	// Editing: a click on a step sets a trig, playing the note of the track's nearest trig (C4 without
	// one), or clears the trig there; a click in the roll puts the edited track's note at that height on
	// that step, or clears it when it is the note already there. OCTAVE – and + (mmPlayRollDown, Up) and
	// the wheel over the roll move the notes it shows; another track or pattern brings back the notes it
	// plays. In the lane, a press or a drag locks the parameter on a step with a trig at the pointer's
	// height (LaneInput), a double click clears the lock. LONGUEUR, COPIER VERS and TOUT EFFACER:
	// PatternCommands. Written as on the Machinedrum once the clicks pause (Controller::sendPatternSoon),
	// through the Monomachine's SYSEX RECV menu, which the Device drives: about half a second a write.
	class MmPatternView
	{
	public:
		using SelectTrack = std::function<void(uint8_t)>;
		using Pattern = md::automation::sysex::MmPatternDump;

		static constexpr uint8_t TrackCount = Pattern::TrackCount;
		static constexpr uint8_t VisibleSteps = 32;
		static constexpr uint8_t PageCount = 7;			// SYN, AMP, FILT, EFX, LFO 1 to 3
		static constexpr uint8_t ParameterCount = PageCount * 8;
		static constexpr uint8_t DefaultNote = 60;		// C4

		MmPatternView(Controller& _controller, Rml::Element& _document, SelectTrack _selectTrack);

		// Asks for the pattern while JOUER shows none, and redraws what changed: the pattern, the
		// machines, the edited track, the steps shown (grid, roll and lane), the lane parameter or its
		// Kit value (lane). Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// What a click on a step does (_step: 0 to 63)
		void toggleStep(uint8_t _track, uint8_t _step);
		// What a click in the roll does, on a visible column and a note
		void placeNote(uint8_t _column, uint8_t _note);
		// The visible column and the note a click in the roll at (_x, _y) of a _width by _height roll
		// lands on, nullopt outside
		std::optional<std::pair<uint8_t, uint8_t>> rollCell(float _x, float _y, float _width, float _height) const;
		// The note a new trig on a step plays: the note of the track's nearest trig before it, or after
		// it, DefaultNote without one
		static uint8_t defaultNote(const Pattern& _pattern, uint8_t _track, uint8_t _step);
		// What OCTAVE – and + (12 semitones) and the wheel do: the roll's notes moved, within 0 to 127
		void scrollRoll(int _semitones);
		// The notes the roll shows, lowest and highest
		std::pair<uint8_t, uint8_t> getRollRange() const { return m_rollRange; }
		// What the lane does under the mouse (LaneInput): the parameter shown locked on the step of a
		// visible column, or its lock cleared (nullopt)
		void editLock(uint8_t _column, std::optional<uint8_t> _value);

		// What a slot chosen in COPIER VERS's menu does
		void copyTo(const uint8_t _slot) { m_commands.copyTo(_slot); }
		bool isCopyArmed() const { return m_commands.isCopyArmed(); }
		bool isClearArmed() const { return m_commands.isClearArmed(); }

		// A MIDI note as the roll and the grid write it, C4 for 60
		static std::string noteName(uint8_t _note);
		// The notes the roll shows for a track: from the lowest to the highest the track plays, widened
		// around them to two octaves at least, and kept within 0..127
		static std::pair<uint8_t, uint8_t> rollRange(const Pattern& _pattern, uint8_t _track);

		// What the roll and the lane show on a visible step, for the tests: the note (-1: none), the
		// lane value (-1: none) and whether it is a lock
		int rollNote(uint8_t _step) const { return _step < VisibleSteps ? m_rollNotes[_step] : -1; }
		int barValue(uint8_t _step) const { return _step < VisibleSteps ? m_barValues[_step] : -1; }
		bool barLocked(uint8_t _step) const { return _step < VisibleSteps && m_barLocks[_step]; }
		// The visible column lit as playing, -1 for none
		int getShownPlayColumn() const { return m_shownPlayColumn; }

	private:
		bool showPlayColumn(int _column);
		void renderGrid(const Pattern* _pattern);
		void renderRoll(const Pattern* _pattern);
		void renderLane(const Pattern* _pattern);
		uint8_t kitValue(uint8_t _track, uint8_t _parameter) const;
		void paintKeys(juce::Image& _image, juce::Graphics& _g) const;
		void paintRoll(juce::Image& _image, juce::Graphics& _g) const;
		void paintLane(juce::Image& _image, juce::Graphics& _g) const;
		void forceRedraw() { m_shownPattern = ~uint64_t{0}; }

		Controller& m_controller;
		SelectTrack m_selectTrack;
		PatternCommands m_commands;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_rollInfo = nullptr;
		Rml::Element* m_laneInfo = nullptr;
		std::array<Rml::Element*, VisibleSteps> m_heads{};
		std::array<Rml::Element*, TrackCount> m_tracks{};
		std::array<std::array<Rml::Element*, VisibleSteps>, TrackCount> m_steps{};
		std::array<Rml::Element*, 2> m_stepPages{};
		std::array<Rml::Element*, PageCount> m_parameterPages{};
		std::array<Rml::Element*, 8> m_parameters{};
		juceRmlUi::ElemCanvas* m_keys = nullptr;
		juceRmlUi::ElemCanvas* m_roll = nullptr;
		juceRmlUi::ElemCanvas* m_lane = nullptr;
		std::unique_ptr<LaneInput> m_laneInput;
		// The roll's lowest note once moved (OCTAVE, wheel); none: around the notes the track plays
		std::optional<int> m_rollLow;
		uint8_t m_rollSlot = 0xff;			// the pattern the roll was moved on
		std::array<std::string, ParameterCount> m_names{};

		uint8_t m_stepPage = 0;
		uint8_t m_parameterPage = 1;		// AMP
		uint8_t m_parameterIndex = 5;		// VOL
		double m_lastRequest = -1.0e9;

		// What the canvases paint, for the visible steps
		std::array<int, VisibleSteps> m_rollNotes{};
		std::array<bool, VisibleSteps> m_rollStartsAmp{};
		std::array<bool, VisibleSteps> m_rollLocked{};
		std::pair<uint8_t, uint8_t> m_rollRange{48, 72};
		uint8_t m_rollLength = 0;			// visible steps below the length
		std::array<int, VisibleSteps> m_barValues{};
		std::array<bool, VisibleSteps> m_barLocks{};

		uint64_t m_shownPattern = ~uint64_t{0};
		uint64_t m_shownMachines = ~uint64_t{0};
		uint8_t m_shownTrack = 0xff;
		uint8_t m_shownStepPage = 0xff;
		uint8_t m_shownParameter = 0xff;
		uint8_t m_shownKit = 0xff;
		int m_shownPlayColumn = -1;
		std::array<std::string, VisibleSteps> m_shownHeads{};
		std::array<std::string, TrackCount> m_shownLabels{};
		std::array<std::array<std::string, VisibleSteps>, TrackCount> m_shownSteps{};
		std::array<std::string, PageCount> m_shownPageLabels{};
		std::array<std::string, 8> m_shownParameterLabels{};
		std::string m_shownRollInfo;
		std::string m_shownLaneInfo;
	};
}
