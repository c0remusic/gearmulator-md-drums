#include "mdDrumsLfoView.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsEngine.h"
#include "mdDrumsKnob.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlElemCanvas.h"

#include "mdProtocol/mdmachines.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>

namespace mdDrums
{
	namespace
	{
		const juce::Colour g_accent(0xff6fd1c4);	// HANDOFF.md, "Colour"

		// The plot, in the screen's canvas (which starts after the screen's 1 px left rule): from the chips' column (x 964)
		// to the screen's right inset (x 1280), from 48 px under the screen's top to the first row of chips less 8 (y 712)
		constexpr float g_left = 67.0f, g_right = 383.0f, g_top = 48.0f, g_bottom = 104.0f;

		constexpr double g_defaultBpm = 125.0;	// the engine's until the host gives its tempo
	}

	LfoView::LfoView(Editor& _editor) : m_editor(_editor)
	{
		m_screen = find("screen_lfo");
		if(m_screen)
		{
			m_canvas = juceRmlUi::ElemCanvas::create(m_screen);
			m_canvas->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
			m_canvas->setPixelAligned(true);
			m_canvas->setClearEveryFrame(true);
			m_canvas->setRepaintGraphicsCallback([this](const juce::Image&, juce::Graphics& _g) { paint(_g); });
		}

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

		// Every Track's LFO target and depth, for the names it underlines
		auto& controller = m_editor.getProcessor().getController();
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
			for(const auto* name : {"LfoTrack", "LfoParam", "LFOD"})
				if(auto* p = controller.getParameter(name, t))
					m_onAnyLfo.emplace_back(p->onValueChanged, [this](pluginLib::Parameter* const&) { updateUnderlines(); });

		m_onPartChanged.set(controller.onCurrentPartChanged, [this](const uint8_t& _part) { onPartChanged(_part); });
		onPartChanged(controller.getCurrentPart());
		updateTitle();
	}

	LfoView::~LfoView() = default;

	Rml::Element* LfoView::find(const std::string& _id) const
	{
		return m_editor.findChild(_id, false);
	}

	pluginLib::Parameter* LfoView::parameter(const char* _name, const uint8_t _part) const
	{
		return m_editor.getProcessor().getController().getParameter(_name, _part);
	}

	int LfoView::value(const char* _name, const uint8_t _part) const
	{
		const auto* p = parameter(_name, _part);
		return p ? p->getUnnormalizedValue() : 0;
	}

	void LfoView::onPartChanged(const uint8_t _part)
	{
		m_part = _part;
		m_onShown.clear();
		for(const auto* name : {"LfoShape1", "LfoShape2", "LfoMode", "LFOS", "LFOD", "LFOM"})
			if(auto* p = parameter(name, _part))
				m_onShown.emplace_back(p->onValueChanged, [this](pluginLib::Parameter* const&) { update(); });
		for(const auto* name : {"LfoTrack", "LfoParam"})
			if(auto* p = parameter(name, _part))
				m_onShown.emplace_back(p->onValueChanged, [this](pluginLib::Parameter* const&)
				{
					listenToTarget();
					update();
				});
		if(auto* p = parameter("Machine", _part))
			m_onShown.emplace_back(p->onValueChanged, [this](pluginLib::Parameter* const&) { updateTargets(); });
		listenToTarget();
		update();
		updateUnderlines();
		updateTargets();
	}

	void LfoView::refresh()
	{
		listenToTarget();
		update();
		updateUnderlines();
		updateTargets();
	}

	void LfoView::listenToTarget()
	{
		const auto track = static_cast<uint8_t>(std::clamp(value("LfoTrack", m_part), 0, Controller::TrackCount - 1));
		const auto param = std::clamp(value("LfoParam", m_part), 0, Engine::ParamCount - 1);
		m_onTarget.reset();
		if(auto* p = parameter(Engine::paramNames()[static_cast<size_t>(param)], track))
			m_onTarget.set(p->onValueChanged, [this](pluginLib::Parameter* const&) { update(); });
	}

	void LfoView::update()
	{
		const Lfo lfo{static_cast<uint8_t>(value("LfoShape1", m_part)), static_cast<uint8_t>(value("LfoShape2", m_part)),
			static_cast<uint8_t>(value("LfoMode", m_part))};
		const auto track = static_cast<uint8_t>(std::clamp(value("LfoTrack", m_part), 0, Controller::TrackCount - 1));
		const auto param = std::clamp(value("LfoParam", m_part), 0, Engine::ParamCount - 1);
		const auto speed = value("LFOS", m_part), mix = value("LFOM", m_part);
		const auto bpm = static_cast<Processor&>(m_editor.getProcessor()).getBpm();
		const auto tempo = bpm > 0.0f ? static_cast<double>(bpm) : g_defaultBpm;
		m_depth = value("LFOD", m_part);
		m_trace = lfo.trace(speed, m_depth, mix, value(Engine::paramNames()[static_cast<size_t>(param)], track), tempo,
			lfo.window(speed, mix, tempo));

		// The values it reaches, in the label column at the plot's top and foot; without depth, why there is no wave
		const auto show = [&](const char* _id, const bool _visible, const std::string& _text)
		{
			if(auto* e = find(_id))
			{
				e->SetInnerRML(_text);
				e->SetProperty("display", _visible ? "block" : "none");
			}
		};
		// In the knob's steps: a word's nearest, $3f80 to $3fff being 127
		const auto steps = [](const uint16_t _word) { return std::to_string(std::min(127L, std::lround(_word / 128.0))); };
		const bool moves = m_depth > 0;
		show("lfo_high", moves, steps(m_trace.highest));
		show("lfo_low", moves, steps(m_trace.lowest));
		show("lfo_none", !moves, "LFOD 0: no modulation");
		if(m_canvas)
			m_canvas->repaint();
	}

	void LfoView::updateUnderlines() const
	{
		// A name is underlined when an LFO, any Track's, aims at it with some depth
		const auto& names = Engine::paramNames();
		std::array<bool, Engine::ParamCount> modulated{};
		for(uint8_t k = 0; k < Controller::TrackCount; ++k)
		{
			const auto param = value("LfoParam", k);
			if(value("LfoTrack", k) == m_part && value("LFOD", k) > 0 && param >= 0 && param < Engine::ParamCount)
				modulated[static_cast<size_t>(param)] = true;
		}
		for(size_t i = 0; i < names.size(); ++i)
			if(auto* label = find(std::string("n_") + names[i]))
				label->SetClass("lfo", modulated[i]);
	}

	void LfoView::updateTargets()
	{
		// Every knob of the bands, but SYN slots the shown Track's machine does not use
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum,
			static_cast<uint16_t>(value("Machine", m_part)));
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
		if(auto* p = parameter("LfoTrack", owner))
			p->setUnnormalizedValueNotifyingHost(shown, pluginLib::Parameter::Origin::Ui);
		if(auto* p = parameter("LfoParam", owner))
			p->setUnnormalizedValueNotifyingHost(_param, pluginLib::Parameter::Origin::Ui);
		m_editor.setCurrentPart(owner);
	}

	void LfoView::paint(juce::Graphics& _g) const
	{
		if(m_depth <= 0 || m_trace.words.empty())
			return;
		const auto* context = m_screen->GetContext();
		const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
		const auto left = g_left * dp, top = g_top * dp, bottom = g_bottom * dp;
		const auto pixels = static_cast<int>(std::round((g_right - g_left) * dp));
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
		_g.strokePath(wave, juce::PathStrokeType(2.0f * dp));
	}
}
