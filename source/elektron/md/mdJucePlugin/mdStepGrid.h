#pragma once

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
	class Controller;

	// The PAS block of the Machinedrum editor: the 32 steps of the edited track in
	// the current pattern, read from the firmware (Controller::requestPattern).
	// A click on a step focuses it: the parameters locked on that step show their
	// locked value in orange, the others are dimmed. A second click clears the focus.
	// A double click sets or clears the step's trig. While a step with a trig has
	// the focus, a control writes a lock on that step instead of changing the Kit,
	// and a double click on it clears the lock. Edits are written to the firmware
	// with the whole pattern (Controller::sendPattern): a drag once it ends, a
	// wheel turn once it pauses. While the sequencer plays, the step it plays is
	// lit (class mdEdStepNow).
	class StepGrid
	{
	public:
		StepGrid(Controller& _controller, Rml::Element& _document);

		// Asks for the pattern when none is shown yet, sends pending lock edits, and
		// redraws when the edited track, the pattern or the focus changed, once SON
		// shows the grid. Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		void refresh();
		void toggleFocus(uint8_t _step);
		void toggleTrig(uint8_t _step);
		int getFocus() const { return m_focus; }
		// Whether controls write locks: a focused step with a trig.
		bool isLocking() const;
		// The step lit as the one the sequencer plays, -1 for none
		int getShownPlayStep() const { return m_shownPlayStep; }

	private:
		bool showPlayStep(int _step);
		void render();
		void editLock(uint8_t _parameter, std::optional<uint8_t> _value);
		void send();
		uint8_t kitValue(uint8_t _parameter) const;

		struct Control
		{
			std::string name;                  // host parameter
			Rml::Element* control = nullptr;   // knob or fader
			Rml::Element* value = nullptr;     // bound value label
			Rml::Element* lock = nullptr;      // locked value, shown instead of the value
			Rml::Element* lockKnob = nullptr;  // over the control while locking; writes the lock
			// What the DOM shows: written again only when it changes, each write lays the document out
			std::string shownLock;
			bool lockKnobShown = false;
		};

		Controller& m_controller;
		Rml::Element* m_root = nullptr;        // the grid's block: hidden, nothing is drawn
		std::array<Rml::Element*, 32> m_steps{};
		Rml::Element* m_info = nullptr;
		std::string m_shownInfo;
		std::array<Control, 24> m_controls{};  // by Machinedrum track parameter 0..23

		int m_focus = -1;
		int m_shownPlayStep = -1;
		bool m_dirty = true;
		bool m_reading = false;
		bool m_dragging = false;
		bool m_sendPending = false;
		double m_now = 0.0;
		double m_lastEdit = 0.0;
		double m_lastRequest = -1.0e9;
		uint8_t m_shownPart = 0xff;
		uint64_t m_shownRevision = ~uint64_t{0};
		uint8_t m_shownWrite = 0xff;
	};
}
