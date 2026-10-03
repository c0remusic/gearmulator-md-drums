#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace Rml
{
	class Element;
	class Event;
}

namespace mdJucePlugin
{
	// The mouse on a lane's canvas in JOUER, either machine: a press, and a drag from it, set the value
	// at the pointer's height on the step's column under it, as the lane draws its bars
	// (stepColumns::laneValueAt); a double click clears it. The view writes it as a lock.
	class LaneInput
	{
	public:
		// _column: 0 to 31 of the steps shown; _value: nullopt to clear
		using Edit = std::function<void(uint8_t _column, std::optional<uint8_t> _value)>;

		LaneInput(Rml::Element& _area, Edit _edit);

	private:
		void apply(const Rml::Event& _event, bool _clear) const;

		Rml::Element& m_area;
		Edit m_edit;
		bool m_dragging = false;
	};
}
