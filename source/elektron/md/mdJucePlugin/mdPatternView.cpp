#include "mdPatternView.h"

#include "mdController.h"
#include "mdStepColumns.h"

#include "mdLib/mdmachines.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <bitset>

namespace mdJucePlugin
{
	namespace
	{
		// The 24 parameters a Machinedrum lock row can hold, as SON names them
		constexpr const char* g_shortNames[PatternView::ParameterCount] = {
			"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8",
			"AMD", "AMF", "EQF", "EQG", "BASE", "WDTH", "Q", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};

		std::string number(const unsigned _value)
		{
			return (_value < 10 ? "0" : "") + std::to_string(_value);
		}

		std::string patternName(const uint8_t _slot)
		{
			const auto step = _slot % 16 + 1;
			return std::string(1, static_cast<char>('A' + _slot / 16)) + (step < 10 ? "0" : "") + std::to_string(step);
		}

		void setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
		{
			if(!_element || _text == _shown)
				return;
			_shown = _text;
			_element->SetInnerRML(_text);
		}
	}

	PatternView::PatternView(Controller& _controller, Rml::Element& _document, SelectTrack _selectTrack)
		: m_controller(_controller)
		, m_selectTrack(std::move(_selectTrack))
	{
		m_root = _document.GetElementById("mdEdPagePlay");
		m_info = _document.GetElementById("mdPlayInfo");
		m_laneInfo = _document.GetElementById("mdPlayLaneInfo");
		for(uint8_t step = 0; step < StepCount; ++step)
			m_heads[step] = _document.GetElementById("mdPlayHead" + std::to_string(step));
		for(uint8_t track = 0; track < TrackCount; ++track)
		{
			m_tracks[track] = _document.GetElementById("mdPlayTrack" + std::to_string(track));
			if(m_tracks[track])
			{
				juceRmlUi::EventListener::Add(m_tracks[track], Rml::EventId::Click, [this, track](Rml::Event&)
				{
					m_selectTrack(track);
					update();
				});
			}
			for(uint8_t step = 0; step < StepCount; ++step)
			{
				auto* cell = _document.GetElementById("mdPlayStep" + std::to_string(track) + "_" + std::to_string(step));
				m_steps[track][step] = cell;
				if(!cell)
					continue;
				// A click sets or clears the trig, shown at once; the pattern is written once the clicks
				// pause, as each write costs the playing machine a short silence
				juceRmlUi::EventListener::Add(cell, Rml::EventId::Click, [this, track, step](Rml::Event&)
				{
					const auto pattern = m_controller.getPattern();
					if(pattern && m_controller.setPatternTrig(track, step, !pattern->hasTrig(track, step)))
						m_controller.sendPatternSoon();
					update();
				});
			}
		}
		for(uint8_t parameter = 0; parameter < ParameterCount; ++parameter)
		{
			m_parameters[parameter] = _document.GetElementById("mdPlayParam" + std::to_string(parameter));
			if(!m_parameters[parameter])
				continue;
			juceRmlUi::EventListener::Add(m_parameters[parameter], Rml::EventId::Click, [this, parameter](Rml::Event&)
			{
				m_laneParameter = parameter;
				update();
			});
		}
		m_barValues.fill(-1);
		if(auto* area = _document.GetElementById("mdPlayLaneArea"))
		{
			m_lane = juceRmlUi::ElemCanvas::create(area);
			m_lane->setRepaintGraphicsCallback([this](juce::Image& _image, juce::Graphics& _g) { paintLane(_image, _g); });
		}
		if(auto* refresh = _document.GetElementById("mdPlayRefresh"))
		{
			juceRmlUi::EventListener::Add(refresh, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_controller.requestPattern();
			});
		}
		// TOUT EFFACER: a first click arms it (CONFIRMER, 3 s), a second one clears the pattern and writes it
		m_clear = _document.GetElementById("mdPlayClear");
		if(m_clear)
		{
			juceRmlUi::EventListener::Add(m_clear, Rml::EventId::Click, [this](Rml::Event&)
			{
				const auto now = juce::Time::getMillisecondCounterHiRes();
				if(m_clearArmedAt < 0.0 || now - m_clearArmedAt > ClearConfirmMilliseconds)
				{
					m_clearArmedAt = now;
					m_clear->SetInnerRML("CONFIRMER ?");
					m_clear->SetClass("mdPlayArmed", true);
					return;
				}
				disarmClear();
				if(m_controller.clearPattern())
					m_controller.sendPattern();
				update();
			});
		}
		if(auto* open = _document.GetElementById("mdPlayOpen"))
		{
			auto* document = &_document;
			juceRmlUi::EventListener::Add(open, Rml::EventId::Click, [document](Rml::Event&)
			{
				if(auto* sound = document->GetElementById("mdEdCat0"))
					sound->Click();
			});
		}
		// Parameters 0..23 are the synthesis, effects and routing pages, 8 each
		for(const auto& description : m_controller.getParameterDescriptions().getDescriptions())
		{
			if(description.page <= md::automation::machinedrum::Routing && description.index < 8)
				m_names[description.page * 8 + description.index] = description.name;
		}
	}

	uint32_t PatternView::parameterLocks(const md::automation::sysex::PatternDump& _pattern, const uint8_t _track,
		const uint8_t _parameter)
	{
		if(_track >= TrackCount || _parameter >= ParameterCount || !((_pattern.lockMasks[_track] >> _parameter) & 1u))
			return 0;
		// Lock rows follow the masks, in track then parameter order
		size_t row = 0;
		for(uint8_t track = 0; track < _track; ++track)
			row += std::bitset<32>(_pattern.lockMasks[track] & 0x00ffffffu).count();
		row += std::bitset<32>(_pattern.lockMasks[_track] & ((1u << _parameter) - 1u)).count();
		if(row >= _pattern.lockRows.size())
			return 0;
		uint32_t steps = 0;
		for(uint8_t step = 0; step < StepCount; ++step)
		{
			if(_pattern.lockRows[row][step] < 0x80)
				steps |= 1u << step;
		}
		return steps;
	}

	uint32_t PatternView::lockedSteps(const md::automation::sysex::PatternDump& _pattern, const uint8_t _track)
	{
		uint32_t steps = 0;
		for(uint8_t parameter = 0; parameter < ParameterCount; ++parameter)
			steps |= parameterLocks(_pattern, _track, parameter);
		return steps;
	}

	uint8_t PatternView::kitValue(const uint8_t _track, const uint8_t _parameter) const
	{
		const auto* parameter = m_names[_parameter].empty() ? nullptr : m_controller.getParameter(m_names[_parameter], _track);
		return parameter ? static_cast<uint8_t>(std::clamp(static_cast<int>(parameter->getUnnormalizedValue()), 0, 127)) : 0;
	}

	void PatternView::disarmClear()
	{
		m_clearArmedAt = -1.0;
		if(m_clear)
		{
			m_clear->SetInnerRML("TOUT EFFACER");
			m_clear->SetClass("mdPlayArmed", false);
		}
	}

	bool PatternView::update()
	{
		// TOUT EFFACER not confirmed in time
		bool disarmed = false;
		if(m_clearArmedAt >= 0.0 && juce::Time::getMillisecondCounterHiRes() - m_clearArmedAt > ClearConfirmMilliseconds)
		{
			disarmClear();
			disarmed = true;
		}
		// Hidden: drawn in full when shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_shownPattern = ~uint64_t{0};
			return disarmed;
		}
		const auto revision = m_controller.getPatternRevision();
		const auto machines = m_controller.getMachineRevision();
		const auto track = static_cast<uint8_t>(m_controller.getCurrentPart());
		// The lane shows the Kit value where a step has no lock: follow it
		const auto kit = kitValue(track, m_laneParameter);
		const bool grid = revision != m_shownPattern || machines != m_shownMachines || track != m_shownTrack;
		if(!grid && m_laneParameter == m_shownParameter && kit == m_shownKit)
			return disarmed;
		m_shownPattern = revision;
		m_shownMachines = machines;
		m_shownTrack = track;
		m_shownParameter = m_laneParameter;
		m_shownKit = kit;
		const auto pattern = m_controller.getPattern();
		if(grid)
			renderGrid(pattern ? &*pattern : nullptr);
		renderLane(pattern ? &*pattern : nullptr);
		return true;
	}

	void PatternView::paintLane(juce::Image& _image, juce::Graphics& _g) const
	{
		_image.clear(_image.getBounds());
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < StepCount || h < 8.0f)
			return;
		// On the grid's columns; a lock carries a dot above its bar (board 2)
		stepColumns::paintLane(_g, w, h, m_barValues, m_barLocks);
	}

	void PatternView::renderGrid(const md::automation::sysex::PatternDump* _pattern)
	{
		const uint8_t length = _pattern ? _pattern->length : 0;
		size_t trigs = 0;
		for(uint8_t track = 0; track < TrackCount; ++track)
		{
			if(auto* label = m_tracks[track])
			{
				const auto* machine = md::machines::find(md::MachineModel::Machinedrum, m_controller.getTrackMachine(track));
				setText(label, m_shownLabels[track], number(track + 1u) + " " + (machine ? std::string(machine->name) : std::string("—")));
				label->SetClass("mdEdSelected", track == m_shownTrack);
			}
			const auto locked = _pattern ? lockedSteps(*_pattern, track) : 0u;
			for(uint8_t step = 0; step < StepCount; ++step)
			{
				const bool trig = _pattern && step < length && _pattern->hasTrig(track, step);
				trigs += trig ? 1 : 0;
				auto* cell = m_steps[track][step];
				if(!cell)
					continue;
				cell->SetClass("mdEdStepTrig", trig);
				cell->SetClass("mdEdStepOut", _pattern && step >= length);
				cell->SetClass("mdPlayLocked", trig && ((locked >> step) & 1u));
			}
		}
		for(uint8_t step = 0; step < StepCount; ++step)
		{
			if(m_heads[step])
				m_heads[step]->SetClass("mdEdStepOut", _pattern && step >= length);
		}
		setText(m_info, m_shownInfo, _pattern ? "pattern " + patternName(_pattern->slot) + " · " + std::to_string(length) + " pas"
			+ (length > StepCount ? " (32 affichés)" : "") + " · " + std::to_string(trigs) + " trigs"
			: std::string("pattern : en attente du firmware"));
	}

	void PatternView::renderLane(const md::automation::sysex::PatternDump* _pattern)
	{
		// The steps that play: the Kit value in grey, a lock in orange
		const auto part = m_shownTrack;
		const uint8_t length = _pattern ? _pattern->length : 0;
		const auto locks = _pattern ? parameterLocks(*_pattern, part, m_laneParameter) : 0u;
		for(uint8_t step = 0; step < StepCount; ++step)
		{
			const bool trig = _pattern && step < length && _pattern->hasTrig(part, step);
			const bool lock = (locks >> step) & 1u;
			m_barLocks[step] = trig && lock;
			m_barValues[step] = !trig ? -1 : lock ? _pattern->lock(part, m_laneParameter, step).value_or(m_shownKit) : m_shownKit;
		}
		if(m_lane)
			m_lane->repaint();
		size_t laneLocks = 0;
		for(uint8_t parameter = 0; parameter < ParameterCount; ++parameter)
		{
			const auto count = _pattern ? std::bitset<32>(parameterLocks(*_pattern, part, parameter)).count() : size_t{0};
			if(parameter == m_laneParameter)
				laneLocks = count;
			if(auto* button = m_parameters[parameter])
			{
				setText(button, m_shownParameterLabels[parameter],
					std::string(g_shortNames[parameter]) + (count ? " · " + std::to_string(count) : std::string()));
				button->SetClass("mdEdSelected", parameter == m_laneParameter);
			}
		}
		setText(m_laneInfo, m_shownLaneInfo, "piste " + number(part + 1u) + " · " + g_shortNames[m_laneParameter] + " : "
			+ std::to_string(laneLocks) + (laneLocks == 1 ? " lock" : " locks") + " · kit " + std::to_string(m_shownKit));
	}
}
