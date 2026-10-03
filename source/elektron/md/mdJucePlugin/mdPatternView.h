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
	class Event;
}

namespace mdJucePlugin
{
	class Controller;

	// JOUER (Machinedrum, boards 1 and 2): the current pattern, every track on 32
	// steps at a time, 1 to 32 or, for a pattern over 32 steps, 33 to 64 (mdPlayPage0
	// and 1); mdPlayStep<track>_<column>: a trig in white, in orange with locks; the
	// steps and their numbers, mdPlayHead<column>, greyed past the length. Under it the
	// lane of the edited track and one parameter on the same steps, a canvas in
	// mdPlayLaneArea: the Kit value in grey, a lock in orange with a dot, for the steps
	// that play; mdPlayParam<parameter> chooses the parameter and counts its locks.
	// A click on a track name (mdPlayTrack<track>) makes it the edited track;
	// a click on a step sets or clears its trig, written to the firmware as the PAS
	// block does. In the lane, a press or a drag locks the parameter on a step with a
	// trig at the pointer's height (LaneInput), a double click clears the lock.
	// LONGUEUR, COPIER VERS and TOUT EFFACER: PatternCommands. OUVRIR DANS
	// SON (mdPlayOpen) shows the track in SON. While the sequencer plays, the column of
	// its step is lit when its steps are shown (mdPlayNow on the step number and the
	// cells), as the machine's position in its RAM gives it (Controller::getPlayingStep).
	class PatternView
	{
	public:
		using SelectTrack = std::function<void(uint8_t)>;

		static constexpr uint8_t TrackCount = 16;
		static constexpr uint8_t StepCount = 32;	// the columns shown
		static constexpr uint8_t MaxSteps = 64;
		static constexpr uint8_t ParameterCount = 24;

		PatternView(Controller& _controller, Rml::Element& _document, SelectTrack _selectTrack);

		// Redraws, while JOUER is shown, what changed: the pattern, the machines or the
		// edited track (grid and lane), the lane parameter or its Kit value (lane), the
		// playing step, the copy. Returns true when it changed the DOM.
		bool update();

		// What the lane does under the mouse (LaneInput): the lane's parameter locked on the step
		// of a visible column, or its lock cleared (nullopt)
		void editLock(uint8_t _column, std::optional<uint8_t> _value);
		// What a slot chosen in COPIER VERS's menu does
		void copyTo(const uint8_t _slot) { m_commands.copyTo(_slot); }
		bool isCopyArmed() const { return m_commands.isCopyArmed(); }
		bool isClearArmed() const { return m_commands.isClearArmed(); }
		// The column lit as playing, -1 for none
		int getShownPlayStep() const { return m_shownPlayStep; }
		// The steps shown: 0 for 1 to 32, 1 for 33 to 64
		uint8_t getStepPage() const { return m_shownStepPage; }

		// Locks of one parameter on a track, bit n for step n + 1
		static uint64_t parameterLocks(const md::automation::sysex::PatternDump& _pattern, uint8_t _track, uint8_t _parameter);
		// Steps with a lock on a track, any parameter
		static uint64_t lockedSteps(const md::automation::sysex::PatternDump& _pattern, uint8_t _track);

		// What the lane shows in a column: its value, -1 for none, and whether it is a lock
		int barValue(uint8_t _column) const { return _column < StepCount ? m_barValues[_column] : -1; }
		bool barLocked(uint8_t _column) const { return _column < StepCount && m_barLocks[_column]; }

	private:
		// Lights the playing step's column, a class on each cell: the column moves without a layout
		bool showPlayStep(int _step);
		void renderGrid(const md::automation::sysex::PatternDump* _pattern);
		void renderLane(const md::automation::sysex::PatternDump* _pattern);
		uint8_t kitValue(uint8_t _track, uint8_t _parameter) const;
		void paintLane(juce::Image& _image, juce::Graphics& _g) const;

		Controller& m_controller;
		SelectTrack m_selectTrack;
		PatternCommands m_commands;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_laneInfo = nullptr;
		std::array<Rml::Element*, 2> m_stepPages{};
		uint8_t m_stepPage = 0;		// asked for; steps 33 to 64 show only for a pattern over 32 steps
		std::array<Rml::Element*, StepCount> m_heads{};
		std::array<Rml::Element*, TrackCount> m_tracks{};
		std::array<std::array<Rml::Element*, StepCount>, TrackCount> m_steps{};
		std::array<Rml::Element*, ParameterCount> m_parameters{};
		juceRmlUi::ElemCanvas* m_lane = nullptr;
		std::unique_ptr<LaneInput> m_laneInput;
		std::array<int, StepCount> m_barValues{};
		std::array<bool, StepCount> m_barLocks{};
		std::array<std::string, ParameterCount> m_names{};
		uint8_t m_laneParameter = 0;

		uint64_t m_shownPattern = ~uint64_t{0};
		uint64_t m_shownMachines = ~uint64_t{0};
		uint8_t m_shownTrack = 0xff;
		uint8_t m_shownParameter = 0xff;
		uint8_t m_shownKit = 0xff;
		uint8_t m_shownStepPage = 0;
		uint8_t m_shownHeadPage = 0;		// the step numbers the heads show: the skin's are 1 to 32
		int m_shownPlayStep = -1;
		std::array<std::string, TrackCount> m_shownLabels{};
		std::array<std::string, ParameterCount> m_shownParameterLabels{};
		std::string m_shownLaneInfo;
	};
}
