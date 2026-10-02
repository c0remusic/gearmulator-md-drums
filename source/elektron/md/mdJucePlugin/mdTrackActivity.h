#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "mdLib/mdtypes.h"

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// A trig LED per track, lit for a moment when the sequencer plays a trig on the track: in the editor's
	// track strip (mdEdTrackLed<track>), next to the track's name in JOUER (mdPlayLed<track>, mmPlayLed<track>)
	// and, brighter, the track's LED in MIX. The step comes from the firmware (Controller::getPlayingStep),
	// the trigs from the pattern the editor shows; a muted track stays dark. Classes only (mdTrackHit):
	// colours, no layout. Elements the skin lacks are skipped.
	class TrackActivity
	{
	public:
		// Long enough to see at 60 frames a second, short enough to blink apart at 16ths of 150 BPM (100 ms)
		static constexpr double LitMilliseconds = 70.0;

		TrackActivity(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Lights the tracks with a trig on a new playing step, darkens them LitMilliseconds later.
		// Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// The tracks lit, bit n for track n
		uint32_t getLit() const { return m_lit; }

	private:
		uint32_t tracksWithTrig(uint8_t _step) const;
		bool isMuted(uint8_t _track) const;
		bool light(uint32_t _tracks);

		Controller& m_controller;
		const md::MachineModel m_model;
		std::vector<std::vector<Rml::Element*>> m_elements;	// per track
		std::optional<uint8_t> m_shownStep;
		uint32_t m_lit = 0;
		double m_litAt = 0.0;
	};
}
