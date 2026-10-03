#include "mdLaneInput.h"

#include "mdStepColumns.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Event.h"

namespace mdJucePlugin
{
	LaneInput::LaneInput(Rml::Element& _area, Edit _edit) : m_area(_area), m_edit(std::move(_edit))
	{
		juceRmlUi::EventListener::Add(&m_area, Rml::EventId::Mousedown, [this](const Rml::Event& _event)
		{
			if(_event.GetParameter<int>("button", 0) != 0)
				return;
			m_dragging = true;
			apply(_event, false);
		});
		juceRmlUi::EventListener::Add(&m_area, Rml::EventId::Mousemove, [this](const Rml::Event& _event)
		{
			if(m_dragging)
				apply(_event, false);
		});
		juceRmlUi::EventListener::Add(&m_area, Rml::EventId::Mouseup, [this](Rml::Event&) { m_dragging = false; });
		juceRmlUi::EventListener::Add(&m_area, Rml::EventId::Mouseout, [this](Rml::Event&) { m_dragging = false; });
		// The two presses before it have set a value: the double click takes it away
		juceRmlUi::EventListener::Add(&m_area, Rml::EventId::Dblclick, [this](const Rml::Event& _event)
		{
			m_dragging = false;
			apply(_event, true);
		});
	}

	void LaneInput::apply(const Rml::Event& _event, const bool _clear) const
	{
		const auto offset = m_area.GetAbsoluteOffset(Rml::BoxArea::Content);
		const auto size = m_area.GetBox().GetSize(Rml::BoxArea::Content);
		const auto x = _event.GetParameter<float>("mouse_x", -1.0f) - offset.x;
		const auto y = _event.GetParameter<float>("mouse_y", -1.0f) - offset.y;
		const auto column = stepColumns::columnAt(x, size.x);
		if(column < 0 || y < 0.0f || y >= size.y || !m_edit)
			return;
		m_edit(static_cast<uint8_t>(column), _clear ? std::nullopt : std::optional<uint8_t>(stepColumns::laneValueAt(y, size.x, size.y)));
	}
}
