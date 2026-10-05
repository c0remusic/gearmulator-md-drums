#include "mdTrackRoutingView.h"

#include "mdController.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"

#include <string>

namespace mdJucePlugin
{
	TrackRoutingView::TrackRoutingView(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
	{
		for(uint8_t track = 0; track < m_segments.size(); ++track)
		{
			for(uint8_t output = 0; output < OutputCount; ++output)
			{
				auto* segment = _document.GetElementById("mdEdOut" + std::to_string(track) + "_" + std::to_string(output));
				m_segments[track][output] = segment;
				if(!segment)
					continue;
				juceRmlUi::EventListener::Add(segment, Rml::EventId::Click, [this, track, output](Rml::Event&)
				{
					if(m_controller.setTrackOutput(track, static_cast<md::automation::sysex::TrackOutput>(output)))
						update();
				});
			}
		}
	}

	bool TrackRoutingView::update()
	{
		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getRoutingRevision();
		if(revision == m_shownRevision)
			return false;
		m_shownRevision = revision;
		for(uint8_t track = 0; track < m_segments.size(); ++track)
		{
			const auto output = m_controller.getTrackOutput(track);
			for(uint8_t index = 0; index < OutputCount; ++index)
			{
				auto* segment = m_segments[track][index];
				if(!segment)
					continue;
				segment->SetClass("mdEdSelected", output && static_cast<uint8_t>(*output) == index);
				segment->SetClass("mdEdUnread", !output);
			}
		}
		return true;
	}
}
