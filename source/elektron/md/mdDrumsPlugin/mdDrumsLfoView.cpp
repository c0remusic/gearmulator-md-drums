#include "mdDrumsLfoView.h"

#include "mdDrumsCanvas.h"
#include "mdDrumsColours.h"
#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsEngine.h"
#include "mdDrumsFrame.h"
#include "mdDrumsKnob.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlElemCanvas.h"

#include "mdProtocol/mdmachines.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mdDrums
{
	namespace
	{
		// The plot, in the screen's canvas (which starts after the screen's 1 px left rule): from the chips' column (x 964)
		// to the screen's right inset (x 1280), from 48 px under the screen's top to the first row of chips less 8 (y 712)
		constexpr float g_left = 67.0f, g_right = 383.0f, g_top = 48.0f, g_bottom = 104.0f;

		constexpr double g_defaultBpm = 125.0;	// the engine's until the host gives its tempo

		constexpr std::array<const char*, 9> g_parameters{
			"LfoShape1", "LfoShape2", "LfoMode", "LFOS", "LFOD", "LFOM", "LfoTrack", "LfoParam", "Machine"};
	}

	LfoView::LfoView(Editor& _editor) : m_editor(_editor)
	{
		static_assert(g_parameters.size() == IndexCount);
		auto& controller = m_editor.getProcessor().getController();
		for(uint8_t t = 0; t < m_parameters.size(); ++t)
			for(size_t i = 0; i < g_parameters.size(); ++i)
				m_parameters[t][i] = controller.getParameter(g_parameters[i], t);
		m_traced.fill(-1);
		setText(find("lfo_none"), "LFOD 0: no modulation");

		if(auto* screen = find("screen_lfo"))
			m_canvas = createCanvas(screen, [this](juce::Graphics& _g, float, float, const float _dp)
			{
				paint(_g, _dp);
			});

		// ASSIGN: armed by its word, which then reads Cancel and brings the LFO's own Track back; while armed, the title
		// opens the target menu as the target does; a tab disarms it
		m_editor.addClick("lfo_assign", [this](Rml::Event&)
		{
			if(!m_armed)
			{
				arm();
				return;
			}
			disarm();
			m_editor.setCurrentPart(m_owner);
		});
		m_editor.addClick("lfo_assigning", [this](Rml::Event&)
		{
			if(auto* tracks = m_editor.getTrackView())
				tracks->setOverlay(TrackView::Overlay::LfoMenu);
		});
		for(const auto* tab : {"tab_mix", "tab_master"})
			m_editor.addClick(tab, [this](Rml::Event&) { disarm(); });

		updateTitle();
	}

	LfoView::~LfoView() = default;

	Rml::Element* LfoView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	int LfoView::value(const Index _index, const uint8_t _part) const
	{
		const auto* p = m_parameters[_part][_index];
		return p ? p->getUnnormalizedValue() : 0;
	}

	bool LfoView::step(const Frame& _frame)
	{
		m_part = _frame.shown;
		bool wrote = followTrace();
		wrote |= followUnderlines();
		wrote |= followTargets();
		return wrote;
	}

	bool LfoView::followTrace()
	{
		// Hidden too (another tab, an overlay over it): RmlUi knows the screen shows only after the update that follows
		// the frame, and a hidden canvas paints nothing until then

		// The parameter the shown Track's LFO moves: LfoParam's, of LfoTrack
		const auto track = std::clamp(value(Track, m_part), 0, Controller::TrackCount - 1);
		const auto param = std::clamp(value(Param, m_part), 0, Engine::ParamCount - 1);
		if(m_targetOf != std::array<int, 2>{track, param})
		{
			m_targetOf = {track, param};
			m_target = m_editor.getProcessor().getController().getParameter(
				Engine::paramNames()[static_cast<size_t>(param)], static_cast<uint8_t>(track));
		}

		// What the plot is traced from: the LFO, its target's value, the tempo as the OS's routines take it. Without
		// depth it shows only why there is no wave, whatever they are
		const auto depth = value(Depth, m_part);
		const auto bpm = m_editor.processor().getBpm();
		const auto tempo = bpm > 0.0f ? static_cast<double>(bpm) : g_defaultBpm;
		TraceKey key{};
		if(depth > 0)
		{
			key = {value(Shape1, m_part), value(Shape2, m_part), value(Mode, m_part), value(Speed, m_part), depth,
				value(Mix, m_part), m_target ? m_target->getUnnormalizedValue() : 0, Lfo::tempo(tempo)};
		}
		if(key == m_traced)
			return false;
		m_traced = key;
		m_depth = depth;

		// The values it reaches, in the label column at the plot's top and foot (in the knob's steps: a word's
		// nearest, $3f80 to $3fff being 127); without depth, why there is no wave
		const bool moves = depth > 0;
		if(moves)
		{
			const Lfo lfo{static_cast<uint8_t>(key[Shape1]), static_cast<uint8_t>(key[Shape2]), static_cast<uint8_t>(key[Mode])};
			m_trace = lfo.trace(key[Speed], depth, key[Mix], key[TargetValue], tempo, lfo.window(key[Speed], key[Mix], tempo));
			const auto steps = [](const uint16_t _word) { return std::to_string(std::min(127L, std::lround(_word / 128.0))); };
			setText(find("lfo_high"), steps(m_trace.highest));
			setText(find("lfo_low"), steps(m_trace.lowest));
		}
		setDisplay(find("lfo_high"), moves);
		setDisplay(find("lfo_low"), moves);
		setDisplay(find("lfo_none"), !moves);
		if(m_canvas)
			m_canvas->repaint();
		return true;
	}

	bool LfoView::followUnderlines()
	{
		// A name is underlined when an LFO, any Track's, aims at it on the shown Track with some depth
		int64_t modulated = 0;
		for(uint8_t k = 0; k < Controller::TrackCount; ++k)
		{
			const auto param = value(Param, k);
			if(value(Track, k) == m_part && value(Depth, k) > 0 && param >= 0 && param < Engine::ParamCount)
				modulated |= int64_t{1} << param;
		}
		if(modulated == m_underlined)
			return false;
		const auto& names = Engine::paramNames();
		for(size_t i = 0; i < names.size(); ++i)
		{
			const auto on = (modulated >> i) & 1;
			if(m_underlined >= 0 && on == ((m_underlined >> i) & 1))
				continue;
			if(auto* label = find(std::string("n_") + names[i]))
				label->SetClass("lfo", on != 0);
		}
		m_underlined = modulated;
		return true;
	}

	bool LfoView::followTargets()
	{
		// Armed: the knobs of the shown Track's machine, a Track or a machine later
		if(!m_armed)
			return false;
		const auto ringedFor = m_part * 256 + value(Machine, m_part);
		if(ringedFor == m_ringedFor)
			return false;
		updateTargets();
		return true;
	}

	void LfoView::updateTargets()
	{
		m_ringedFor = m_part * 256 + value(Machine, m_part);
		// Every knob of the bands, but SYN slots the shown Track's machine does not use
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum,
			static_cast<uint16_t>(value(Machine, m_part)));
		for(int i = 0; i < Engine::ParamCount; ++i)
		{
			auto* knob = dynamic_cast<Knob*>(find(std::string("k_") + Engine::paramNames()[static_cast<size_t>(i)]));
			if(!knob)
				continue;
			const bool unused = i < 8 && names && (*names)[static_cast<size_t>(i)].empty();
			const bool target = m_armed && !unused;
			if(target == knob->isTarget())
				continue;
			knob->setTarget(target ? std::function<void()>([this, i] { pick(i); }) : std::function<void()>());
		}
	}

	void LfoView::updateTitle() const
	{
		if(auto* target = find("lfo_target"))
			target->SetProperty("display", m_armed ? "none" : "inline-block");
		if(auto* asking = find("lfo_assigning"))
			asking->SetProperty("display", m_armed ? "inline-block" : "none");
		if(auto* assign = find("lfo_assign"))
		{
			assign->SetInnerRML(m_armed ? "Cancel" : "Assign");
			assign->SetClass("on", m_armed);
		}
	}

	void LfoView::arm()
	{
		if(m_armed)
			return;
		m_owner = m_part;
		m_armed = true;
		if(auto* tracks = m_editor.getTrackView())
			tracks->setOverlay(TrackView::Overlay::None);
		updateTargets();
		updateTitle();
	}

	void LfoView::disarm()
	{
		if(!m_armed)
			return;
		m_armed = false;
		updateTargets();
		updateTitle();
	}

	bool LfoView::escape()
	{
		if(!m_armed)
			return false;
		disarm();
		m_editor.setCurrentPart(m_owner);
		return true;
	}

	void LfoView::pick(const int _param)
	{
		// The LFO that armed it modulates the shown Track's knob, then its own Track shows again
		const auto owner = m_owner;
		const auto shown = m_part;
		disarm();
		if(auto* p = m_parameters[owner][Track])
			p->setUnnormalizedValueNotifyingHost(shown, pluginLib::Parameter::Origin::Ui);
		if(auto* p = m_parameters[owner][Param])
			p->setUnnormalizedValueNotifyingHost(_param, pluginLib::Parameter::Origin::Ui);
		m_editor.setCurrentPart(owner);
	}

	void LfoView::paint(juce::Graphics& _g, const float _dp) const
	{
		if(m_depth <= 0 || m_trace.words.empty())
			return;
		const auto left = g_left * _dp, top = g_top * _dp, bottom = g_bottom * _dp;
		const auto pixels = static_cast<int>(std::round((g_right - g_left) * _dp));
		const auto span = static_cast<float>(m_trace.highest - m_trace.lowest);
		const auto yOf = [&](const uint16_t _word)
		{
			if(span <= 0.0f)
				return std::round((top + bottom) * 0.5f);
			return bottom - static_cast<float>(_word - m_trace.lowest) / span * (bottom - top);
		};

		// A pixel column: the ticks of its time, the value at its start, the extremes, the value at its end (one tick a
		// step where a tick is wider than a pixel)
		const auto count = m_trace.words.size();
		juce::Path wave;
		for(int x = 0; x < pixels; ++x)
		{
			const auto from = std::min(count - 1, static_cast<size_t>(static_cast<double>(x) * count / pixels));
			const auto to = std::max(from + 1, std::min(count, static_cast<size_t>(static_cast<double>(x + 1) * count / pixels)));
			const auto [lowest, highest] = std::minmax_element(m_trace.words.begin() + static_cast<std::ptrdiff_t>(from),
				m_trace.words.begin() + static_cast<std::ptrdiff_t>(to));
			const auto px = left + static_cast<float>(x);
			if(x)
				wave.lineTo(px, yOf(m_trace.words[from]));
			else
				wave.startNewSubPath(px, yOf(m_trace.words[from]));
			wave.lineTo(px, yOf(*lowest));
			wave.lineTo(px, yOf(*highest));
			wave.lineTo(px + 1.0f, yOf(m_trace.words[to - 1]));
		}
		_g.setColour(g_accent);
		_g.strokePath(wave, juce::PathStrokeType(2.0f * _dp));
	}
}
