#pragma once

#include "mdLib/mdsysexautomation.h"

#include <array>
#include <cstdint>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The MASTER view (Machinedrum): the Kit's four master effects, 8 knobs each,
	// mdEdFx<effect>_<parameter> with their value line mdEdFxVal<effect>_<parameter>
	// (effects in md::automation::sysex::MasterEffect order). The knobs are not
	// bound to host parameters: they show the controller's values, "—" and greyed
	// while unknown, and a turn sends the value through Controller::setMasterEffect.
	class MasterEffectsView
	{
	public:
		MasterEffectsView(Controller& _controller, Rml::Element& _document);

		// Shows the controller's values when they changed. Returns true when it
		// changed the DOM.
		bool update();

	private:
		struct Control
		{
			Rml::Element* knob = nullptr;
			Rml::Element* value = nullptr;
			int shown = -1;            // -1: unknown
		};

		void onTurn(uint8_t _effect, uint8_t _parameter, float _value);
		void show(Control& _control, int _value);

		Controller& m_controller;
		std::array<std::array<Control, md::automation::sysex::MasterEffectParameters>,
			md::automation::sysex::MasterEffectCount> m_controls{};
		uint64_t m_shownRevision = ~uint64_t{0};
	};
}
