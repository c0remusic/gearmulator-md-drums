#pragma once

#include <array>
#include <cstdint>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The PAS block of the Machinedrum editor: the 32 steps of the edited track in
	// the current pattern, read from the firmware (Controller::requestPattern).
	// A click on a step focuses it: the parameters locked on that step show their
	// locked value in orange, the others are dimmed. A second click clears the focus.
	// Read-only: nothing here writes the pattern.
	class StepGrid
	{
	public:
		StepGrid(Controller& _controller, Rml::Element& _document);

		// Asks for the pattern when none is shown yet, and redraws when the edited
		// track, the pattern or the focus changed. Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		void refresh();
		void toggleFocus(uint8_t _step);
		int getFocus() const { return m_focus; }

	private:
		void render();

		struct Control
		{
			Rml::Element* control = nullptr;   // knob or fader
			Rml::Element* value = nullptr;     // bound value label
			Rml::Element* lock = nullptr;      // locked value, shown instead of the value
		};

		Controller& m_controller;
		std::array<Rml::Element*, 32> m_steps{};
		Rml::Element* m_info = nullptr;
		std::array<Control, 24> m_controls{};  // by Machinedrum track parameter 0..23

		int m_focus = -1;
		bool m_dirty = true;
		bool m_reading = false;
		double m_lastRequest = -1.0e9;
		uint8_t m_shownPart = 0xff;
		uint64_t m_shownRevision = ~uint64_t{0};
	};
}
