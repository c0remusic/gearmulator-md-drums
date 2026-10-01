#pragma once

#include "mdLib/mdsysexautomation.h"

#include <array>
#include <cstdint>
#include <functional>
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
}

namespace mdJucePlugin
{
	class Controller;

	// JOUER (Machinedrum, boards 1 and 2): the current pattern, every track on its
	// 32 first steps (mdPlayStep<track>_<step>: trig, a frame for a step with locks,
	// greyed past the length), and under it the lane of the edited track and one
	// parameter, a canvas in mdPlayLaneArea: the Kit value in grey, a lock in orange
	// with a dot, for the steps that play; mdPlayParam<parameter> chooses the
	// parameter and counts its locks. A click on a track name (mdPlayTrack<track>) makes it the edited track;
	// a double click on a step sets or clears its trig, written to the firmware as
	// the PAS block does. OUVRIR DANS SON (mdPlayOpen) shows the track in SON.
	class PatternView
	{
	public:
		using SelectTrack = std::function<void(uint8_t)>;

		static constexpr uint8_t TrackCount = 16;
		static constexpr uint8_t StepCount = 32;
		static constexpr uint8_t ParameterCount = 24;

		PatternView(Controller& _controller, Rml::Element& _document, SelectTrack _selectTrack);

		// Redraws, while JOUER is shown, what changed: the pattern, the machines or the
		// edited track (grid and lane), the lane parameter or its Kit value (lane).
		// Returns true when it changed the DOM.
		bool update();

		// Locks of one parameter on a track, bit n for step n + 1
		static uint32_t parameterLocks(const md::automation::sysex::PatternDump& _pattern, uint8_t _track, uint8_t _parameter);
		// Steps with a lock on a track, any parameter
		static uint32_t lockedSteps(const md::automation::sysex::PatternDump& _pattern, uint8_t _track);

		// What the lane shows on a step: its value, -1 for none, and whether it is a lock
		int barValue(uint8_t _step) const { return _step < StepCount ? m_barValues[_step] : -1; }
		bool barLocked(uint8_t _step) const { return _step < StepCount && m_barLocks[_step]; }

	private:
		void renderGrid(const md::automation::sysex::PatternDump* _pattern);
		void renderLane(const md::automation::sysex::PatternDump* _pattern);
		uint8_t kitValue(uint8_t _track, uint8_t _parameter) const;
		void paintLane(juce::Image& _image, juce::Graphics& _g) const;

		Controller& m_controller;
		SelectTrack m_selectTrack;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_info = nullptr;
		Rml::Element* m_laneInfo = nullptr;
		std::array<Rml::Element*, TrackCount> m_tracks{};
		std::array<std::array<Rml::Element*, StepCount>, TrackCount> m_steps{};
		std::array<Rml::Element*, ParameterCount> m_parameters{};
		juceRmlUi::ElemCanvas* m_lane = nullptr;
		std::array<int, StepCount> m_barValues{};
		std::array<bool, StepCount> m_barLocks{};
		std::array<std::string, ParameterCount> m_names{};
		uint8_t m_laneParameter = 0;

		uint64_t m_shownPattern = ~uint64_t{0};
		uint64_t m_shownMachines = ~uint64_t{0};
		uint8_t m_shownTrack = 0xff;
		uint8_t m_shownParameter = 0xff;
		uint8_t m_shownKit = 0xff;
		std::array<std::string, TrackCount> m_shownLabels{};
		std::array<std::string, ParameterCount> m_shownParameterLabels{};
		std::string m_shownInfo;
		std::string m_shownLaneInfo;
	};
}
