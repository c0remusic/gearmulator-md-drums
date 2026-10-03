#include "mdMmPatternView.h"

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
		using Pattern = md::automation::sysex::MmPatternDump;

		constexpr const char* g_pageNames[MmPatternView::PageCount] = {"SYN", "AMP", "FILT", "EFX", "LFO 1", "LFO 2", "LFO 3"};

		// The 8 parameters of each page, as SON names them; SYN's while the track's machine is unknown
		constexpr const char* g_shortNames[MmPatternView::PageCount][8] = {
			{"A", "B", "C", "D", "E", "F", "G", "H"},
			{"ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"},
			{"BASE", "WDTH", "HPQ", "LPQ", "ATK", "DEC", "BOFS", "WOFS"},
			{"EQF", "EQG", "SRR", "DTIM", "DSND", "DFB", "DBAS", "DWID"},
			{"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DEP"},
			{"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DEP"},
			{"PAGE", "DEST", "TRIG", "WAVE", "MULT", "SPD", "INTL", "DEP"}};

		std::string number(const unsigned _value)
		{
			return (_value < 10 ? "0" : "") + std::to_string(_value);
		}

		std::string patternName(const uint8_t _slot)
		{
			const auto step = _slot % 16 + 1;
			return std::string(1, static_cast<char>('A' + _slot / 16)) + (step < 10 ? "0" : "") + std::to_string(step);
		}

		bool blackKey(const int _note)
		{
			const auto key = _note % 12;
			return key == 1 || key == 3 || key == 6 || key == 8 || key == 10;
		}

		// Steps of a track with any lock, from its rows in order
		uint64_t lockedSteps(const Pattern& _pattern, const uint8_t _track)
		{
			size_t row = 0;
			for(uint8_t track = 0; track < _track; ++track)
				row += std::bitset<64>(_pattern.lockMasks[track]).count();
			uint64_t steps = 0;
			const auto rows = std::bitset<64>(_pattern.lockMasks[_track]).count();
			for(size_t i = 0; i < rows && row + i < _pattern.lockRows.size(); ++i)
			{
				for(uint8_t step = 0; step < Pattern::StepCount; ++step)
				{
					if(_pattern.lockRows[row + i][step] < 0x80)
						steps |= uint64_t{1} << step;
				}
			}
			return steps;
		}

		const juce::Colour g_whiteRow(0xff1b1c1f);
		const juce::Colour g_blackRow(0xff151618);
		const juce::Colour g_octave(0xff3a3d42);
		const juce::Colour g_note(0xfff4f1ea);
		const juce::Colour g_lockedNote(0xffff6b1f);
		const juce::Colour g_silentNote(0xff8a8d93);
		const juce::Colour g_noteName(0xff141516);
		const juce::Colour g_past(0xa0151618);
		const juce::Colour g_keyWhite(0xffc9c6bf);
		const juce::Colour g_keyBlack(0xff1b1c1f);
		const juce::Colour g_keyName(0xff3a3d42);

		void setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
		{
			if(!_element || _text == _shown)
				return;
			_shown = _text;
			_element->SetInnerRML(_text);
		}
	}

	MmPatternView::MmPatternView(Controller& _controller, Rml::Element& _document, SelectTrack _selectTrack)
		: m_controller(_controller)
		, m_selectTrack(std::move(_selectTrack))
		, m_commands(_controller, _document, "mmPlay", [this] { forceRedraw(); })
	{
		m_root = _document.GetElementById("mdEdPagePlay");
		m_rollInfo = _document.GetElementById("mmPlayRollInfo");
		for(uint8_t step = 0; step < VisibleSteps; ++step)
			m_heads[step] = _document.GetElementById("mmPlayHead" + std::to_string(step));
		m_laneInfo = _document.GetElementById("mmPlayLaneInfo");
		for(uint8_t track = 0; track < TrackCount; ++track)
		{
			m_tracks[track] = _document.GetElementById("mmPlayTrack" + std::to_string(track));
			if(m_tracks[track])
			{
				juceRmlUi::EventListener::Add(m_tracks[track], Rml::EventId::Click, [this, track](Rml::Event&)
				{
					m_selectTrack(track);
					forceRedraw();
				});
			}
			for(uint8_t step = 0; step < VisibleSteps; ++step)
			{
				auto* cell = _document.GetElementById("mmPlayStep" + std::to_string(track) + "_" + std::to_string(step));
				m_steps[track][step] = cell;
				if(!cell)
					continue;
				// The step of the page shown
				juceRmlUi::EventListener::Add(cell, Rml::EventId::Click, [this, track, step](Rml::Event&)
				{
					const auto page = m_shownStepPage < 2 ? m_shownStepPage : uint8_t{0};
					toggleStep(track, static_cast<uint8_t>(page * VisibleSteps + step));
				});
			}
		}
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			m_stepPages[page] = _document.GetElementById("mmPlayPage" + std::to_string(page));
			if(m_stepPages[page])
			{
				juceRmlUi::EventListener::Add(m_stepPages[page], Rml::EventId::Click, [this, page](Rml::Event&)
				{
					m_stepPage = page;
				});
			}
		}
		for(uint8_t page = 0; page < PageCount; ++page)
		{
			m_parameterPages[page] = _document.GetElementById("mmPlayParamPage" + std::to_string(page));
			if(m_parameterPages[page])
			{
				juceRmlUi::EventListener::Add(m_parameterPages[page], Rml::EventId::Click, [this, page](Rml::Event&)
				{
					m_parameterPage = page;
				});
			}
		}
		for(uint8_t index = 0; index < m_parameters.size(); ++index)
		{
			m_parameters[index] = _document.GetElementById("mmPlayParam" + std::to_string(index));
			if(m_parameters[index])
			{
				juceRmlUi::EventListener::Add(m_parameters[index], Rml::EventId::Click, [this, index](Rml::Event&)
				{
					m_parameterIndex = index;
				});
			}
		}
		m_rollNotes.fill(-1);
		m_barValues.fill(-1);
		if(auto* area = _document.GetElementById("mmPlayRollKeys"))
		{
			m_keys = juceRmlUi::ElemCanvas::create(area);
			m_keys->setRepaintGraphicsCallback([this](juce::Image& _image, juce::Graphics& _g) { paintKeys(_image, _g); });
		}
		if(auto* area = _document.GetElementById("mmPlayRollArea"))
		{
			m_roll = juceRmlUi::ElemCanvas::create(area);
			m_roll->setRepaintGraphicsCallback([this](juce::Image& _image, juce::Graphics& _g) { paintRoll(_image, _g); });
			// A click puts a note where it lands, in the area's own coordinates
			juceRmlUi::EventListener::Add(area, Rml::EventId::Click, [this, area](Rml::Event& _event)
			{
				const auto offset = area->GetAbsoluteOffset(Rml::BoxArea::Content);
				const auto size = area->GetBox().GetSize(Rml::BoxArea::Content);
				const auto x = _event.GetParameter<float>("mouse_x", 0.0f) - offset.x;
				const auto y = _event.GetParameter<float>("mouse_y", 0.0f) - offset.y;
				if(const auto cell = rollCell(x, y, size.x, size.y))
					placeNote(cell->first, cell->second);
			});
			// The wheel moves the notes shown, two semitones a notch, down for the lower ones
			juceRmlUi::EventListener::Add(area, Rml::EventId::Mousescroll, [this](Rml::Event& _event)
			{
				const auto delta = _event.GetParameter<float>("wheel_delta_y", 0.0f);
				if(delta == 0.0f)
					return;
				scrollRoll(delta > 0.0f ? -2 : 2);
				_event.StopPropagation();
			});
		}
		if(auto* down = _document.GetElementById("mmPlayRollDown"))
			juceRmlUi::EventListener::Add(down, Rml::EventId::Click, [this](Rml::Event&) { scrollRoll(-12); });
		if(auto* up = _document.GetElementById("mmPlayRollUp"))
			juceRmlUi::EventListener::Add(up, Rml::EventId::Click, [this](Rml::Event&) { scrollRoll(12); });
		if(auto* area = _document.GetElementById("mmPlayLaneArea"))
		{
			m_lane = juceRmlUi::ElemCanvas::create(area);
			m_lane->setRepaintGraphicsCallback([this](juce::Image& _image, juce::Graphics& _g) { paintLane(_image, _g); });
			m_laneInput = std::make_unique<LaneInput>(*area, [this](const uint8_t _column, const std::optional<uint8_t> _value)
			{
				editLock(_column, _value);
			});
		}
		if(auto* refresh = _document.GetElementById("mmPlayRefresh"))
		{
			juceRmlUi::EventListener::Add(refresh, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_controller.requestPattern();
			});
		}
		// Pages Synthesis to LFO 3, 8 parameters each, in the Kit's order: the lock bit of each
		for(const auto& description : m_controller.getParameterDescriptions().getDescriptions())
		{
			if(description.page <= md::automation::monomachine::Lfo3 && description.index < 8)
				m_names[md::automation::sysex::mmLockBit(description.page, description.index)] = description.name;
		}
	}

	std::string MmPatternView::noteName(const uint8_t _note)
	{
		constexpr const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		return std::string(names[_note % 12]) + std::to_string(_note / 12 - 1);
	}

	std::pair<uint8_t, uint8_t> MmPatternView::rollRange(const Pattern& _pattern, const uint8_t _track)
	{
		int lowest = 128;
		int highest = -1;
		for(uint8_t step = 0; step < std::min(_pattern.length, Pattern::StepCount); ++step)
		{
			if(const auto note = _pattern.note(_track, step))
			{
				lowest = std::min<int>(lowest, *note);
				highest = std::max<int>(highest, *note);
			}
		}
		if(highest < 0)
			return {48, 72};
		// Two octaves at least, around the notes played
		if(highest - lowest < 24)
		{
			lowest -= (24 - (highest - lowest)) / 2;
			highest = lowest + 24;
		}
		if(lowest < 0)
		{
			highest -= lowest;
			lowest = 0;
		}
		if(highest > 127)
		{
			lowest = std::max(0, lowest - (highest - 127));
			highest = 127;
		}
		return {static_cast<uint8_t>(lowest), static_cast<uint8_t>(highest)};
	}

	uint8_t MmPatternView::defaultNote(const Pattern& _pattern, const uint8_t _track, const uint8_t _step)
	{
		for(int step = _step - 1; step >= 0; --step)
		{
			if(const auto note = _pattern.note(_track, static_cast<uint8_t>(step)))
				return *note;
		}
		for(int step = _step + 1; step < Pattern::StepCount; ++step)
		{
			if(const auto note = _pattern.note(_track, static_cast<uint8_t>(step)))
				return *note;
		}
		return DefaultNote;
	}

	void MmPatternView::toggleStep(const uint8_t _track, const uint8_t _step)
	{
		const auto pattern = m_controller.getMmPattern();
		if(!pattern || _track >= TrackCount)
			return;
		const auto note = pattern->hasTrig(_track, _step) ? std::nullopt : std::optional<uint8_t>(defaultNote(*pattern, _track, _step));
		if(m_controller.setMmPatternTrig(_track, _step, note))
			m_controller.sendPatternSoon();
		forceRedraw();
	}

	void MmPatternView::placeNote(const uint8_t _column, const uint8_t _note)
	{
		const auto pattern = m_controller.getMmPattern();
		const auto track = m_shownTrack;
		if(!pattern || track >= TrackCount || _column >= VisibleSteps || _note >= 0x80)
			return;
		const auto page = m_shownStepPage < 2 ? m_shownStepPage : uint8_t{0};
		const auto step = static_cast<uint8_t>(page * VisibleSteps + _column);
		// The note already there goes; another takes its place, the trig's locks kept
		const auto note = pattern->note(track, step) == _note ? std::nullopt : std::optional<uint8_t>(_note);
		if(m_controller.setMmPatternTrig(track, step, note))
			m_controller.sendPatternSoon();
		forceRedraw();
	}

	void MmPatternView::scrollRoll(const int _semitones)
	{
		// Drawn again by the next update, keys and line with it
		const int span = m_rollRange.second - m_rollRange.first;
		m_rollLow = std::clamp(static_cast<int>(m_rollRange.first) + _semitones, 0, 127 - span);
		forceRedraw();
	}

	void MmPatternView::editLock(const uint8_t _column, const std::optional<uint8_t> _value)
	{
		const auto track = m_shownTrack;
		const auto bit = m_shownParameter;
		if(track >= TrackCount || bit >= Pattern::LockBitCount || _column >= VisibleSteps)
			return;
		const auto page = m_shownStepPage < 2 ? m_shownStepPage : uint8_t{0};
		const auto step = static_cast<uint8_t>(page * VisibleSteps + _column);
		// The same value again (a drag passes over it many times): nothing to write
		const auto pattern = m_controller.getMmPattern();
		if(!pattern || pattern->lock(track, bit, step) == _value)
			return;
		if(m_controller.setMmPatternLock(track, bit, step, _value))
			m_controller.sendPatternSoon();
		forceRedraw();
	}

	std::optional<std::pair<uint8_t, uint8_t>> MmPatternView::rollCell(const float _x, const float _y, const float _width,
		const float _height) const
	{
		if(_x < 0.0f || _y < 0.0f || _x >= _width || _y >= _height || _width < VisibleSteps || _height < 8.0f)
			return std::nullopt;
		// As paintRoll lays them: a column per step, a row per semitone, the highest note on top
		const int lowest = m_rollRange.first;
		const int highest = m_rollRange.second;
		const auto rowHeight = _height / static_cast<float>(highest - lowest + 1);
		const auto column = std::min(static_cast<int>(_x / (_width / VisibleSteps)), VisibleSteps - 1);
		const auto note = std::clamp(highest - static_cast<int>(_y / rowHeight), lowest, highest);
		return std::make_pair(static_cast<uint8_t>(column), static_cast<uint8_t>(note));
	}

	uint8_t MmPatternView::kitValue(const uint8_t _track, const uint8_t _parameter) const
	{
		const auto* parameter = m_names[_parameter].empty() ? nullptr : m_controller.getParameter(m_names[_parameter], _track);
		return parameter ? static_cast<uint8_t>(std::clamp(static_cast<int>(parameter->getUnnormalizedValue()), 0, 127)) : 0;
	}

	bool MmPatternView::update(const double _nowMilliseconds)
	{
		// TOUT EFFACER or REMPLACER not confirmed in time, the copy as it goes
		const bool commands = m_commands.update(_nowMilliseconds);
		// Hidden: drawn in full when shown
		if(m_root && !m_root->IsVisible(true))
		{
			forceRedraw();
			return showPlayColumn(-1) || commands;
		}
		const auto playing = m_controller.getPlayingStep();
		const bool playChanged = showPlayColumn(playing && *playing / VisibleSteps == m_stepPage
			? *playing % VisibleSteps : -1);
		// Shown without a pattern (none stored yet: revision 0): asked for, again every 2 s until one comes
		const auto revision = m_controller.getPatternRevision();
		if(revision == 0 && _nowMilliseconds - m_lastRequest > 2000.0)
		{
			m_lastRequest = _nowMilliseconds;
			m_controller.requestPattern();
		}
		const auto machines = m_controller.getMachineRevision();
		const auto track = static_cast<uint8_t>(std::min<int>(m_controller.getCurrentPart(), TrackCount - 1));
		const auto parameter = static_cast<uint8_t>(m_parameterPage * 8 + m_parameterIndex);
		// The lane shows the Kit value where a step has no lock: follow it
		const auto kit = kitValue(track, parameter);
		const bool grid = revision != m_shownPattern || machines != m_shownMachines || track != m_shownTrack
			|| m_stepPage != m_shownStepPage;
		if(!grid && parameter == m_shownParameter && kit == m_shownKit)
			return playChanged || commands;
		// Another track: the roll shows the notes it plays
		if(track != m_shownTrack)
			m_rollLow.reset();
		m_shownPattern = revision;
		m_shownMachines = machines;
		m_shownTrack = track;
		m_shownStepPage = m_stepPage;
		m_shownParameter = parameter;
		m_shownKit = kit;
		const auto pattern = m_controller.getMmPattern();
		const auto* shown = pattern ? &*pattern : nullptr;
		if(grid)
		{
			renderGrid(shown);
			renderRoll(shown);
		}
		renderLane(shown);
		return true;
	}

	bool MmPatternView::showPlayColumn(const int _column)
	{
		if(_column == m_shownPlayColumn)
			return false;
		// A class on each cell of the column: it moves without a layout
		const auto light = [this](const int _cell, const bool _on)
		{
			if(_cell < 0 || _cell >= VisibleSteps)
				return;
			if(auto* head = m_heads[static_cast<size_t>(_cell)])
				head->SetClass("mdPlayNow", _on);
			for(uint8_t track = 0; track < TrackCount; ++track)
			{
				if(auto* element = m_steps[track][static_cast<size_t>(_cell)])
					element->SetClass("mdPlayNow", _on);
			}
		};
		light(m_shownPlayColumn, false);
		light(_column, true);
		m_shownPlayColumn = _column;
		return true;
	}

	void MmPatternView::renderGrid(const Pattern* _pattern)
	{
		const uint8_t length = _pattern ? _pattern->length : 0;
		const auto first = static_cast<uint8_t>(m_shownStepPage * VisibleSteps);
		size_t trigs = 0;
		for(uint8_t track = 0; track < TrackCount; ++track)
		{
			if(auto* label = m_tracks[track])
			{
				const auto* machine = md::machines::find(md::MachineModel::Monomachine, m_controller.getTrackMachine(track));
				setText(label, m_shownLabels[track], number(track + 1u) + " " + (machine ? std::string(machine->name) : std::string("—")));
				label->SetClass("mdEdSelected", track == m_shownTrack);
			}
			const auto locked = _pattern ? lockedSteps(*_pattern, track) : 0u;
			for(uint8_t step = 0; step < length; ++step)
				trigs += _pattern->hasTrig(track, step) ? 1 : 0;
			for(uint8_t cell = 0; cell < VisibleSteps; ++cell)
			{
				auto* element = m_steps[track][cell];
				if(!element)
					continue;
				const auto step = static_cast<uint8_t>(first + cell);
				const bool trig = _pattern && step < length && _pattern->hasTrig(track, step);
				const bool amp = trig && ((_pattern->ampTrigs[track] >> step) & 1u);
				const auto note = trig ? _pattern->note(track, step) : std::nullopt;
				setText(element, m_shownSteps[track][cell], trig ? (note ? noteName(*note) : std::string("·")) : std::string());
				element->SetClass("mdEdStepTrig", amp);
				element->SetClass("mmPlaySilent", trig && !amp);
				element->SetClass("mdEdStepOut", _pattern && step >= length);
				element->SetClass("mdPlayLocked", trig && ((locked >> step) & 1u));
			}
		}
		// The steps' numbers, of the steps shown
		for(uint8_t cell = 0; cell < VisibleSteps; ++cell)
		{
			if(auto* head = m_heads[cell])
			{
				setText(head, m_shownHeads[cell], std::to_string(first + cell + 1));
				head->SetClass("mdEdStepOut", _pattern && first + cell >= length);
			}
		}
		for(uint8_t page = 0; page < m_stepPages.size(); ++page)
		{
			if(auto* button = m_stepPages[page])
			{
				button->SetClass("mdEdSelected", page == m_shownStepPage);
				button->SetClass("mdEdOff", _pattern && page * VisibleSteps >= length);
			}
		}
		m_commands.show(_pattern ? std::optional<uint8_t>(_pattern->slot) : std::nullopt, length, _pattern
			? "pattern " + patternName(_pattern->slot) + " · " + std::to_string(length) + " pas"
				+ (_pattern->doubleTempo ? " · tempo double" : "") + " · " + std::to_string(trigs) + (trigs == 1 ? " trig" : " trigs")
			: std::string("pattern : en attente du firmware"));
	}

	void MmPatternView::renderRoll(const Pattern* _pattern)
	{
		const auto track = m_shownTrack;
		const uint8_t length = _pattern ? _pattern->length : 0;
		const auto first = static_cast<uint8_t>(m_shownStepPage * VisibleSteps);
		const auto locked = _pattern ? lockedSteps(*_pattern, track) : 0u;
		const auto fitted = _pattern ? rollRange(*_pattern, track) : std::pair<uint8_t, uint8_t>{48, 72};
		// Moved (OCTAVE, wheel): as many notes from the lowest chosen; another pattern brings back the fitted ones
		if(_pattern && _pattern->slot != m_rollSlot)
		{
			m_rollLow.reset();
			m_rollSlot = _pattern->slot;
		}
		const int span = fitted.second - fitted.first;
		const int low = m_rollLow ? std::clamp(*m_rollLow, 0, 127 - span) : fitted.first;
		m_rollRange = {static_cast<uint8_t>(low), static_cast<uint8_t>(low + span)};
		m_rollLength = static_cast<uint8_t>(std::clamp(length - first, 0, static_cast<int>(VisibleSteps)));
		size_t notes = 0;
		for(uint8_t cell = 0; cell < VisibleSteps; ++cell)
		{
			const auto step = static_cast<uint8_t>(first + cell);
			const auto note = _pattern && step < length ? _pattern->note(track, step) : std::nullopt;
			m_rollNotes[cell] = note ? *note : -1;
			m_rollStartsAmp[cell] = note && ((_pattern->ampTrigs[track] >> step) & 1u);
			m_rollLocked[cell] = note && ((locked >> step) & 1u);
			notes += note ? 1 : 0;
		}
		if(m_keys)
			m_keys->repaint();
		if(m_roll)
			m_roll->repaint();
		std::string text = "piste " + number(track + 1u) + " · pas " + std::to_string(first + 1) + " à "
			+ std::to_string(first + VisibleSteps) + " · ";
		text += notes ? std::to_string(notes) + (notes == 1 ? " note" : " notes") : std::string("aucune note");
		text += " · de " + noteName(m_rollRange.first) + " à " + noteName(m_rollRange.second);
		setText(m_rollInfo, m_shownRollInfo, _pattern ? text : std::string("—"));
	}

	void MmPatternView::renderLane(const Pattern* _pattern)
	{
		// The steps that play: the Kit value in grey, a lock in orange
		const auto track = m_shownTrack;
		const auto parameter = m_shownParameter;
		const uint8_t length = _pattern ? _pattern->length : 0;
		const auto first = static_cast<uint8_t>(m_shownStepPage * VisibleSteps);
		for(uint8_t cell = 0; cell < VisibleSteps; ++cell)
		{
			const auto step = static_cast<uint8_t>(first + cell);
			const bool trig = _pattern && step < length && _pattern->hasTrig(track, step);
			const auto lock = trig ? _pattern->lock(track, parameter, step) : std::nullopt;
			m_barLocks[cell] = lock.has_value();
			m_barValues[cell] = !trig ? -1 : lock ? *lock : m_shownKit;
		}
		if(m_lane)
			m_lane->repaint();
		// Locks of each parameter of the track, over the whole pattern
		std::array<size_t, Pattern::LockBitCount> counts{};
		if(_pattern)
		{
			for(uint8_t bit = 0; bit < Pattern::LockBitCount; ++bit)
				counts[bit] = std::bitset<64>(_pattern->lockedSteps(track, bit)).count();
		}
		for(uint8_t page = 0; page < PageCount; ++page)
		{
			size_t pageLocks = 0;
			for(uint8_t index = 0; index < 8; ++index)
				pageLocks += counts[page * 8 + index];
			if(auto* button = m_parameterPages[page])
			{
				setText(button, m_shownPageLabels[page], std::string(g_pageNames[page]) + (pageLocks ? " · " + std::to_string(pageLocks) : std::string()));
				button->SetClass("mdEdSelected", page == parameter / 8);
			}
		}
		const auto page = static_cast<uint8_t>(parameter / 8);
		// SYN as the track's machine names its parameters; one it does not use is dimmed
		const auto* names = page == 0 ? md::machines::parameterNames(md::MachineModel::Monomachine, m_controller.getTrackMachine(track)) : nullptr;
		const auto label = [names, page](const uint8_t _index)
		{
			return names && !(*names)[_index].empty() ? std::string((*names)[_index]) : std::string(g_shortNames[page][_index]);
		};
		for(uint8_t index = 0; index < m_parameters.size(); ++index)
		{
			if(auto* button = m_parameters[index])
			{
				const auto count = counts[page * 8 + index];
				setText(button, m_shownParameterLabels[index], label(index) + (count ? " · " + std::to_string(count) : std::string()));
				button->SetClass("mdEdSelected", index == parameter % 8);
				button->SetClass("mdEdUnused", names && (*names)[index].empty() && !count && index != parameter % 8);
			}
		}
		size_t others = 0;
		for(uint8_t bit = ParameterCount; bit < Pattern::LockBitCount; ++bit)
			others += counts[bit];
		const auto locks = counts[parameter];
		std::string text = "piste " + number(track + 1u) + " · " + g_pageNames[page] + " " + label(parameter % 8) + " : "
			+ std::to_string(locks) + (locks == 1 ? " lock" : " locks") + " · kit " + std::to_string(m_shownKit);
		if(others)
			text += " · " + std::to_string(others) + " autres locks (paramètres sans nom)";
		setText(m_laneInfo, m_shownLaneInfo, text);
	}

	void MmPatternView::paintRoll(juce::Image& _image, juce::Graphics& _g) const
	{
		_image.clear(_image.getBounds());
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < VisibleSteps || h < 8.0f)
			return;
		// A row per semitone, the highest note on top; a black key's row darker, a line under each C
		const int lowest = m_rollRange.first;
		const int highest = m_rollRange.second;
		const auto rows = static_cast<float>(highest - lowest + 1);
		const auto rowHeight = h / rows;
		const auto pitch = w / VisibleSteps;
		for(int note = lowest; note <= highest; ++note)
		{
			const auto top = static_cast<float>(highest - note) * rowHeight;
			_g.setColour(blackKey(note) ? g_blackRow : g_whiteRow);
			_g.fillRect(0.0f, top, w, rowHeight);
			if(note % 12 == 0)
			{
				_g.setColour(g_octave);
				_g.drawHorizontalLine(static_cast<int>(top + rowHeight - 1.0f), 0.0f, w);
			}
		}
		stepColumns::paintBeats(_g, w, h);
		// A block per note on its step's cell, named when it has room: white, orange with locks (as in the grid),
		// grey when the trig does not start the amp envelope
		const auto cell = pitch * stepColumns::CellFraction;
		const auto named = cell >= 18.0f && rowHeight >= 9.0f;
		_g.setFont(juce::Font(std::min(11.0f, rowHeight - 1.0f), juce::Font::bold));
		for(uint8_t step = 0; step < VisibleSteps; ++step)
		{
			const auto note = m_rollNotes[step];
			if(note < lowest || note > highest)
				continue;
			const auto top = static_cast<float>(highest - note) * rowHeight;
			const auto x = pitch * step;
			_g.setColour(m_rollLocked[step] ? g_lockedNote : m_rollStartsAmp[step] ? g_note : g_silentNote);
			_g.fillRoundedRectangle(x, top + 0.5f, cell, std::max(2.0f, rowHeight - 1.0f), 2.0f);
			if(named)
			{
				_g.setColour(g_noteName);
				_g.drawText(noteName(static_cast<uint8_t>(note)), juce::Rectangle<float>(x, top, cell, rowHeight),
					juce::Justification::centred);
			}
		}
		// Past the length
		if(m_rollLength < VisibleSteps)
		{
			_g.setColour(g_past);
			_g.fillRect(pitch * m_rollLength, 0.0f, w - pitch * m_rollLength, h);
		}
	}

	void MmPatternView::paintKeys(juce::Image& _image, juce::Graphics& _g) const
	{
		_image.clear(_image.getBounds());
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < 8.0f || h < 8.0f)
			return;
		// The keyboard beside the roll, on the same rows: black keys two thirds wide, each C named
		const int lowest = m_rollRange.first;
		const int highest = m_rollRange.second;
		const auto rowHeight = h / static_cast<float>(highest - lowest + 1);
		_g.setColour(g_keyWhite);
		_g.fillRect(0.0f, 0.0f, w, h);
		_g.setFont(juce::Font(std::max(9.0f, std::min(11.0f, rowHeight * 1.2f))));
		for(int note = lowest; note <= highest; ++note)
		{
			const auto top = static_cast<float>(highest - note) * rowHeight;
			if(blackKey(note))
			{
				_g.setColour(g_keyBlack);
				_g.fillRect(0.0f, top, w * 2.0f / 3.0f, rowHeight);
			}
			if(note % 12 == 0 || note % 12 == 5)
			{
				_g.setColour(g_octave);
				_g.drawHorizontalLine(static_cast<int>(top + rowHeight - 1.0f), 0.0f, w);
			}
			if(note % 12 == 0)
			{
				_g.setColour(g_keyName);
				_g.drawText(noteName(static_cast<uint8_t>(note)), juce::Rectangle<float>(0.0f, top + rowHeight - 14.0f, w - 4.0f, 13.0f),
					juce::Justification::bottomRight);
			}
		}
	}

	void MmPatternView::paintLane(juce::Image& _image, juce::Graphics& _g) const
	{
		_image.clear(_image.getBounds());
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < VisibleSteps || h < 8.0f)
			return;
		stepColumns::paintLane(_g, w, h, m_barValues, m_barLocks);
	}
}
