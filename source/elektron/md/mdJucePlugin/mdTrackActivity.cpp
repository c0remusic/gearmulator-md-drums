#include "mdTrackActivity.h"

#include "mdController.h"

#include "RmlUi/Core/Element.h"

#include <string>

namespace mdJucePlugin
{
	TrackActivity::TrackActivity(Controller& _controller, const md::MachineModel _model, Rml::Element& _document)
		: m_controller(_controller)
		, m_model(_model)
	{
		const bool mm = _model == md::MachineModel::Monomachine;
		m_elements.resize(mm ? 6 : 16);
		for(size_t track = 0; track < m_elements.size(); ++track)
		{
			const auto n = std::to_string(track);
			auto& elements = m_elements[track];
			for(const auto& id : {"mdEdTrackLed" + n, (mm ? "mmPlayLed" : "mdPlayLed") + n})
			{
				if(auto* element = _document.GetElementById(id))
					elements.push_back(element);
			}
			// MIX: the row's LED, its first button
			if(auto* row = _document.GetElementById("mdEdLevelRow" + n))
			{
				Rml::ElementList buttons;
				row->GetElementsByTagName(buttons, "button");
				if(!buttons.empty())
					elements.push_back(buttons.front());
			}
		}
	}

	bool TrackActivity::update(const double _nowMilliseconds)
	{
		const auto step = m_controller.getPlayingStep();
		if(step != m_shownStep)
		{
			m_shownStep = step;
			m_litAt = _nowMilliseconds;
			return light(step ? tracksWithTrig(*step) : 0u);
		}
		if(m_lit && _nowMilliseconds - m_litAt >= LitMilliseconds)
			return light(0);
		return false;
	}

	uint32_t TrackActivity::tracksWithTrig(const uint8_t _step) const
	{
		uint32_t tracks = 0;
		const auto add = [&](const auto& _pattern, const uint8_t _steps)
		{
			if(_step >= _pattern.length || _step >= _steps)
				return;
			for(uint8_t track = 0; track < m_elements.size(); ++track)
			{
				if(_pattern.hasTrig(track, _step) && !isMuted(track))
					tracks |= 1u << track;
			}
		};
		if(m_model == md::MachineModel::Monomachine)
		{
			if(const auto pattern = m_controller.getMmPattern())
				add(*pattern, 64);
		}
		// The Machinedrum's dump holds 32 steps, or 64 in its long form
		else if(const auto pattern = m_controller.getPattern())
			add(*pattern, pattern->steps);
		return tracks;
	}

	bool TrackActivity::isMuted(const uint8_t _track) const
	{
		const auto* mute = m_controller.getParameter("Mute", _track);
		return mute && mute->getUnnormalizedValue() != 0;
	}

	bool TrackActivity::light(const uint32_t _tracks)
	{
		const auto changed = m_lit ^ _tracks;
		m_lit = _tracks;
		for(size_t track = 0; track < m_elements.size(); ++track)
		{
			if(!((changed >> track) & 1u))
				continue;
			for(auto* element : m_elements[track])
				element->SetClass("mdTrackHit", (_tracks >> track) & 1u);
		}
		return changed != 0;
	}
}
