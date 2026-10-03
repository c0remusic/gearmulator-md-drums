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
		// The 24 parameters a Machinedrum lock row can hold, as SON names them; P1 to P8 while the
		// track's machine is unknown
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
		, m_commands(_controller, _document, "mdPlay", [this] { update(); })
	{
		m_root = _document.GetElementById("mdEdPagePlay");
		m_laneInfo = _document.GetElementById("mdPlayLaneInfo");
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			m_stepPages[page] = _document.GetElementById("mdPlayPage" + std::to_string(page));
			if(!m_stepPages[page])
				continue;
			juceRmlUi::EventListener::Add(m_stepPages[page], Rml::EventId::Click, [this, page](Rml::Event&)
			{
				m_stepPage = page;
				update();
			});
		}
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
				// A click sets or clears the trig of the step shown, at once; the pattern is written once the
				// clicks pause, as each write costs the playing machine a short silence
				juceRmlUi::EventListener::Add(cell, Rml::EventId::Click, [this, track, step](Rml::Event&)
				{
					const auto pattern = m_controller.getPattern();
					const auto shown = static_cast<uint8_t>(m_shownStepPage * StepCount + step);
					if(pattern && m_controller.setPatternTrig(track, shown, !pattern->hasTrig(track, shown)))
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
			m_laneInput = std::make_unique<LaneInput>(*area, [this](const uint8_t _column, const std::optional<uint8_t> _value)
			{
				editLock(_column, _value);
			});
		}
		if(auto* refresh = _document.GetElementById("mdPlayRefresh"))
		{
			juceRmlUi::EventListener::Add(refresh, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_controller.requestPattern();
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

	uint64_t PatternView::parameterLocks(const md::automation::sysex::PatternDump& _pattern, const uint8_t _track,
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
		uint64_t steps = 0;
		for(uint8_t step = 0; step < _pattern.steps; ++step)
		{
			if(_pattern.lockRows[row][step] < 0x80)
				steps |= uint64_t{1} << step;
		}
		return steps;
	}

	uint64_t PatternView::lockedSteps(const md::automation::sysex::PatternDump& _pattern, const uint8_t _track)
	{
		uint64_t steps = 0;
		for(uint8_t parameter = 0; parameter < ParameterCount; ++parameter)
			steps |= parameterLocks(_pattern, _track, parameter);
		return steps;
	}

	void PatternView::editLock(const uint8_t _column, const std::optional<uint8_t> _value)
	{
		const auto track = m_shownTrack;
		if(track >= TrackCount || _column >= StepCount)
			return;
		const auto step = static_cast<uint8_t>(m_shownStepPage * StepCount + _column);
		// The same value again (a drag passes over it many times): nothing to write
		const auto pattern = m_controller.getPattern();
		if(!pattern || pattern->lock(track, m_laneParameter, step) == _value)
			return;
		if(m_controller.setPatternLock(track, m_laneParameter, step, _value))
			m_controller.sendPatternSoon();
		update();
	}

	uint8_t PatternView::kitValue(const uint8_t _track, const uint8_t _parameter) const
	{
		const auto* parameter = m_names[_parameter].empty() ? nullptr : m_controller.getParameter(m_names[_parameter], _track);
		return parameter ? static_cast<uint8_t>(std::clamp(static_cast<int>(parameter->getUnnormalizedValue()), 0, 127)) : 0;
	}

	bool PatternView::update()
	{
		// TOUT EFFACER or REMPLACER not confirmed in time, the copy as it goes
		const bool commands = m_commands.update(juce::Time::getMillisecondCounterHiRes());
		// Hidden: drawn in full when shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_shownPattern = ~uint64_t{0};
			return showPlayStep(-1) || commands;
		}
		const auto revision = m_controller.getPatternRevision();
		const auto machines = m_controller.getMachineRevision();
		const auto track = static_cast<uint8_t>(m_controller.getCurrentPart());
		// Steps 33 to 64 only for a pattern over 32 steps: settled when the pattern or the page asked for
		// changes
		std::optional<md::automation::sysex::PatternDump> pattern;
		auto page = m_shownStepPage;
		if(revision != m_shownPattern || m_stepPage != m_shownStepPage)
		{
			pattern = m_controller.getPattern();
			page = pattern && pattern->length > StepCount ? m_stepPage : 0;
			m_stepPage = page;
		}
		const auto playing = m_controller.getPlayingStep();
		const bool playStepChanged = showPlayStep(playing && *playing / StepCount == page ? *playing % StepCount : -1);
		// The lane shows the Kit value where a step has no lock: follow it
		const auto kit = kitValue(track, m_laneParameter);
		const bool grid = revision != m_shownPattern || machines != m_shownMachines || track != m_shownTrack
			|| page != m_shownStepPage;
		if(!grid && m_laneParameter == m_shownParameter && kit == m_shownKit)
			return commands || playStepChanged;
		m_shownPattern = revision;
		m_shownMachines = machines;
		m_shownTrack = track;
		m_shownParameter = m_laneParameter;
		m_shownKit = kit;
		m_shownStepPage = page;
		if(!pattern)
			pattern = m_controller.getPattern();
		if(grid)
			renderGrid(pattern ? &*pattern : nullptr);
		renderLane(pattern ? &*pattern : nullptr);
		return true;
	}

	bool PatternView::showPlayStep(const int _step)
	{
		if(_step == m_shownPlayStep)
			return false;
		const auto light = [this](const int _column, const bool _on)
		{
			if(_column < 0 || _column >= StepCount)
				return;
			if(auto* head = m_heads[static_cast<size_t>(_column)])
				head->SetClass("mdPlayNow", _on);
			for(uint8_t track = 0; track < TrackCount; ++track)
			{
				if(auto* cell = m_steps[track][static_cast<size_t>(_column)])
					cell->SetClass("mdPlayNow", _on);
			}
		};
		light(m_shownPlayStep, false);
		light(_step, true);
		m_shownPlayStep = _step;
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
		const auto first = static_cast<uint8_t>(m_shownStepPage * StepCount);
		size_t trigs = 0;
		for(uint8_t track = 0; track < TrackCount; ++track)
		{
			if(auto* label = m_tracks[track])
			{
				const auto* machine = md::machines::find(md::MachineModel::Machinedrum, m_controller.getTrackMachine(track));
				setText(label, m_shownLabels[track], number(track + 1u) + " " + (machine ? std::string(machine->name) : std::string("—")));
				label->SetClass("mdEdSelected", track == m_shownTrack);
			}
			// The trigs within the length, on every step
			for(uint8_t step = 0; step < length; ++step)
				trigs += _pattern && _pattern->hasTrig(track, step) ? 1 : 0;
			const auto locked = _pattern ? lockedSteps(*_pattern, track) : 0u;
			for(uint8_t column = 0; column < StepCount; ++column)
			{
				const auto step = static_cast<uint8_t>(first + column);
				const bool trig = _pattern && step < length && _pattern->hasTrig(track, step);
				auto* cell = m_steps[track][column];
				if(!cell)
					continue;
				cell->SetClass("mdEdStepTrig", trig);
				cell->SetClass("mdEdStepOut", _pattern && step >= length);
				cell->SetClass("mdPlayLocked", trig && ((locked >> step) & 1u));
			}
		}
		for(uint8_t column = 0; column < StepCount; ++column)
		{
			auto* head = m_heads[column];
			if(!head)
				continue;
			if(m_shownHeadPage != m_shownStepPage)
				head->SetInnerRML(std::to_string(first + column + 1));
			head->SetClass("mdEdStepOut", _pattern && first + column >= length);
		}
		m_shownHeadPage = m_shownStepPage;
		// PAS 33-64 only for a pattern over 32 steps
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			if(!m_stepPages[page])
				continue;
			m_stepPages[page]->SetClass("mdEdSelected", page == m_shownStepPage);
			m_stepPages[page]->SetClass("mdEdOff", page > 0 && (!_pattern || _pattern->length <= StepCount));
		}
		m_commands.show(_pattern ? std::optional<uint8_t>(_pattern->slot) : std::nullopt, length, _pattern
			? "pattern " + patternName(_pattern->slot) + " · " + std::to_string(length) + " pas" + " · " + std::to_string(trigs)
				+ (trigs == 1 ? " trig" : " trigs")
			: std::string("pattern : en attente du firmware"));
	}

	void PatternView::renderLane(const md::automation::sysex::PatternDump* _pattern)
	{
		// The steps shown that play: the Kit value in grey, a lock in orange
		const auto part = m_shownTrack;
		const uint8_t length = _pattern ? _pattern->length : 0;
		const auto first = static_cast<uint8_t>(m_shownStepPage * StepCount);
		const auto locks = _pattern ? parameterLocks(*_pattern, part, m_laneParameter) : 0u;
		for(uint8_t column = 0; column < StepCount; ++column)
		{
			const auto step = static_cast<uint8_t>(first + column);
			const bool trig = _pattern && step < length && _pattern->hasTrig(part, step);
			const bool lock = (locks >> step) & 1u;
			m_barLocks[column] = trig && lock;
			m_barValues[column] = !trig ? -1 : lock ? _pattern->lock(part, m_laneParameter, step).value_or(m_shownKit) : m_shownKit;
		}
		if(m_lane)
			m_lane->repaint();
		// The synthesis parameters as the track's machine names them; one it does not use is dimmed
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum, m_controller.getTrackMachine(part));
		const auto label = [names](const uint8_t _parameter)
		{
			return _parameter < 8 && names && !(*names)[_parameter].empty() ? std::string((*names)[_parameter])
				: std::string(g_shortNames[_parameter]);
		};
		size_t laneLocks = 0;
		for(uint8_t parameter = 0; parameter < ParameterCount; ++parameter)
		{
			const auto count = _pattern ? std::bitset<64>(parameterLocks(*_pattern, part, parameter)).count() : size_t{0};
			if(parameter == m_laneParameter)
				laneLocks = count;
			if(auto* button = m_parameters[parameter])
			{
				setText(button, m_shownParameterLabels[parameter], label(parameter) + (count ? " · " + std::to_string(count) : std::string()));
				button->SetClass("mdEdSelected", parameter == m_laneParameter);
				button->SetClass("mdEdUnused", parameter < 8 && names && (*names)[parameter].empty() && !count
					&& parameter != m_laneParameter);
			}
		}
		setText(m_laneInfo, m_shownLaneInfo, "piste " + number(part + 1u) + " · " + label(m_laneParameter) + " : "
			+ std::to_string(laneLocks) + (laneLocks == 1 ? " lock" : " locks") + " · kit " + std::to_string(m_shownKit));
	}
}
