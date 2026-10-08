#include "mdDrumsTrackView.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsLfoView.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace mdDrums
{
	namespace
	{
		constexpr int g_window = 1296;
		constexpr const char* g_listenKey = "listenWhileChoosing";

		std::string twoDigits(const int _value)
		{
			char text[8];
			(void)std::snprintf(text, sizeof(text), "%02d", _value);
			return text;
		}

		std::string dp(const int _value)
		{
			return std::to_string(_value) + "dp";
		}

		// Column c (1-16) starts at 16 + 80 (c - 1)
		int column(const int _c)
		{
			return 16 + 80 * (_c - 1);
		}

		// What a family is, as the head band and the browser say it (the mockup's words; mdmachines' are md-mm's)
		const char* familyText(const std::string_view _code)
		{
			static const std::map<std::string_view, const char*> texts{{"GND", "generator"}, {"TRX", "modelled analog"},
				{"EFM", "FM synthesis"}, {"E12", "built-in 12-bit sample"}, {"P-I", "physical modelling"},
				{"ROM", "UW bank sample"}, {"RAM", "RAM sample"}, {"INP", "external input"}, {"MID", "MIDI"},
				{"CTR", "control"}, {"NFX", "effect"}};
			const auto it = texts.find(_code);
			return it != texts.end() ? it->second : "";
		}

		const char* familyKind(const std::string_view _code)
		{
			static const std::map<std::string_view, const char*> kinds{{"GND", "generators"}, {"TRX", "analog"},
				{"EFM", "FM"}, {"E12", "factory samples"}, {"P-I", "physical"}, {"ROM", "UW samples"}};
			const auto it = kinds.find(_code);
			return it != kinds.end() ? it->second : "";
		}

		// The Track parameters 8-23 by their names, as the bands and the LFO's menu show them
		constexpr std::array<const char*, 16> g_effectNames{"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
	}

	TrackView::TrackView(Editor& _editor) : m_editor(_editor)
	{
		auto& controller = m_editor.getProcessor().getController();

		// The shown Track: the list's rows, the Mix strips' and the sends' numbers and machines, ‹ ›
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			for(const auto& id : {"row" + n + "_num", "row" + n + "_name", "strip" + n + "_num", "strip" + n + "_name",
				"send" + n + "_num", "send" + n + "_name"})
				m_editor.addClick(id, [this, t](Rml::Event&) { showTrack(t); });
		}
		m_editor.addClick("track_prev", [this](Rml::Event&) { stepTrack(-1); });
		m_editor.addClick("track_next", [this](Rml::Event&) { stepTrack(1); });
		m_editor.addClick("size", [this](Rml::Event&) { stepSize(); });

		// The overlays: a tab closes them; the machine's name, the LFO's target and the kit row open and close theirs
		for(const auto* tab : {"tab_track", "tab_mix", "tab_master"})
			m_editor.addClick(tab, [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("machine", [this](Rml::Event&)
		{
			setOverlay(m_overlay == Overlay::Browser ? Overlay::None : Overlay::Browser);
		});
		m_editor.addClick("browser_keep", [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("browser_cancel", [this](Rml::Event&) { escape(); });
		// A machine chosen (its cell has set Machine by then: its own listener came first) plays the Track when listening
		for(int id = 0; id < 256; ++id)
			m_editor.addClick("mach_" + std::to_string(id), [this](Rml::Event&)
			{
				if(isListening())
					static_cast<Controller&>(m_editor.getProcessor().getController()).audition(m_part, ListenVelocity);
			});
		m_editor.addClick("preview_listen", [this](Rml::Event&) { setListening(!isListening()); });
		setListening(isListening());
		m_editor.addClick("lfo_target", [this](Rml::Event&)
		{
			setOverlay(m_overlay == Overlay::LfoMenu ? Overlay::None : Overlay::LfoMenu);
		});
		m_editor.addClick("lfomenu_done", [this](Rml::Event&) { setOverlay(Overlay::None); });
		// Choosing the parameter closes the menu, after the button has set it
		for(int p = 1; p <= 24; ++p)
			m_editor.addClick("lfomenu_param" + std::to_string(p), [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("kit", [this](Rml::Event&) { setOverlay(m_overlay == Overlay::Kits ? Overlay::None : Overlay::Kits); });
		m_editor.addClick("kits_done", [this](Rml::Event&) { setOverlay(Overlay::None); });

		m_onPartChanged.set(controller.onCurrentPartChanged, [this](const uint8_t& _part) { onPartChanged(_part); });
		onPartChanged(controller.getCurrentPart());
		updateSize();
	}

	TrackView::~TrackView() = default;

	void TrackView::showTrack(const uint8_t _track) const
	{
		if(_track < Controller::TrackCount)
			m_editor.setCurrentPart(_track);
	}

	void TrackView::stepTrack(const int _delta) const
	{
		showTrack(static_cast<uint8_t>((m_part + Controller::TrackCount + _delta) % Controller::TrackCount));
	}

	void TrackView::stepSize() const
	{
		auto& config = m_editor.getProcessor().getConfig();
		const auto current = juce::roundToInt(config.getDoubleValue("scale", 100));
		const auto next = std::upper_bound(Scales.begin(), Scales.end(), current);
		const auto scale = next == Scales.end() ? Scales.front() : *next;
		config.setValue("scale", scale);
		// As TUS's own settings page does: the window resizes and keeps the setting
		if(const auto* state = m_editor.getProcessor().getEditorState())
			state->evSetGuiScale(scale);
		updateSize();
	}

	bool TrackView::escape()
	{
		if(m_overlay == Overlay::None)
			return false;
		if(m_overlay == Overlay::Browser)
		{
			// Back to the machine the browser opened on
			auto* machine = m_editor.getProcessor().getController().getParameter("Machine", m_part);
			if(machine && m_machineBeforeBrowser >= 0 && machine->getUnnormalizedValue() != m_machineBeforeBrowser)
				machine->setUnnormalizedValueNotifyingHost(m_machineBeforeBrowser, pluginLib::Parameter::Origin::Ui);
		}
		setOverlay(Overlay::None);
		return true;
	}

	bool TrackView::isListening() const
	{
		return m_editor.getProcessor().getConfig().getBoolValue(g_listenKey, true);
	}

	void TrackView::setListening(const bool _on) const
	{
		m_editor.getProcessor().getConfig().setValue(g_listenKey, _on);
		if(auto* box = find("preview_listen"))
			box->SetClass("on", _on);
	}

	void TrackView::setOverlay(const Overlay _overlay)
	{
		// An overlay takes ASSIGN's place
		if(_overlay != Overlay::None)
			if(auto* lfo = m_editor.getLfoView())
				lfo->disarm();
		if(_overlay == Overlay::Browser && m_overlay != Overlay::Browser)
		{
			const auto* machine = m_editor.getProcessor().getController().getParameter("Machine", m_part);
			m_machineBeforeBrowser = machine ? machine->getUnnormalizedValue() : -1;
		}
		m_overlay = _overlay;

		// The Kits take the Track tab under the kit row; the browser takes the bands' place; the menu, the LFO screen's
		show("track_main", _overlay != Overlay::Kits);
		show("kits", _overlay == Overlay::Kits);
		show("track_bands", _overlay != Overlay::Browser);
		show("browser", _overlay == Overlay::Browser);
		show("lfomenu", _overlay == Overlay::LfoMenu);
		if(_overlay == Overlay::LfoMenu)
			updateLfoMenu();
	}

	void TrackView::onPartChanged(const uint8_t _part)
	{
		auto& controller = m_editor.getProcessor().getController();
		m_part = _part;
		if(m_overlay == Overlay::Browser)
			setOverlay(Overlay::None);

		// The shown row: lit to the editing area, its mark at the window's edge, its number and machine in ink
		const auto top = ListTop + RowHeight * _part;
		if(auto* bg = find("row_sel_bg"))
			bg->SetProperty("top", dp(top));
		if(auto* mark = find("row_sel_mark"))
			mark->SetProperty("top", dp(top + 8));

		// The shown strip, lit to the gutters' middles (the window's edges for the first and the last), its bar on top
		const auto from = _part == 0 ? 0 : column(_part + 1) - 8;
		const auto to = _part == Controller::TrackCount - 1 ? g_window : column(_part + 1) + 72;
		if(auto* bg = find("strip_sel_bg"))
		{
			bg->SetProperty("left", dp(from));
			bg->SetProperty("width", dp(to - from));
		}
		if(auto* bar = find("strip_sel_bar"))
			bar->SetProperty("left", dp(column(_part + 1)));

		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			for(const auto& id : {"row" + n + "_num", "row" + n + "_name"})
				if(auto* e = find(id))
					e->SetClass("tsel", t == _part);
			for(const auto& id : {"strip" + n + "_num", "send" + n + "_num"})
				if(auto* e = find(id))
					e->SetClass("on", t == _part);
		}
		if(auto* caption = find("caption"))
			caption->SetInnerRML("Track " + twoDigits(_part + 1));
		if(auto* forTrack = find("browser_for"))
			forTrack->SetInnerRML("for track " + twoDigits(_part + 1));

		m_onMachineChanged.reset();
		m_onOutChanged.reset();
		m_onLfoTrackChanged.reset();
		if(auto* machine = controller.getParameter("Machine", _part))
			m_onMachineChanged.set(machine->onValueChanged, [this](pluginLib::Parameter* const&) { updateMachine(); });
		if(auto* out = controller.getParameter("Out", _part))
			m_onOutChanged.set(out->onValueChanged, [this](pluginLib::Parameter* const&) { updateOutput(); });
		if(auto* lfoTrack = controller.getParameter("LfoTrack", _part))
			m_onLfoTrackChanged.set(lfoTrack->onValueChanged, [this](pluginLib::Parameter* const&) { updateLfoMenu(); });
		updateMachine();
		updateOutput();
		updateLfoMenu();
	}

	void TrackView::updateMachine() const
	{
		const auto* parameter = m_editor.getProcessor().getController().getParameter("Machine", m_part);
		if(!parameter)
			return;
		const auto id = static_cast<uint16_t>(parameter->getUnnormalizedValue());

		const auto* family = md::machines::familyOf(md::MachineModel::Machinedrum, id);
		if(auto* code = find("family_code"))
			code->SetInnerRML(family ? std::string(family->name) : std::string());
		if(auto* text = find("family_text"))
			text->SetInnerRML(family ? familyText(family->name) : "");
		if(auto* kind = find("preview_kind"))
			kind->SetInnerRML(family ? familyKind(family->name) : "");

		// SYN1-8 by the machine's own names; a slot it does not use reads "—", dimmed, without a value
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum, id);
		for(size_t i = 0; i < 8; ++i)
		{
			const auto suffix = "_SYN" + std::to_string(i + 1);
			const auto name = names ? std::string((*names)[i]) : "SYN" + std::to_string(i + 1);
			const auto unused = name.empty();
			if(auto* label = find("n" + suffix))
			{
				// In a span, as the other names: an LFO's underline runs under the name only (LfoView)
				label->SetInnerRML("<span>" + (unused ? std::string("—") : name) + "</span>");
				label->SetClass("unused", unused);
			}
			for(const auto& prefix : {"k", "v"})
				if(auto* e = find(prefix + suffix))
				{
					e->SetClass("unused", unused);
					e->SetProperty("visibility", unused && prefix == std::string("v") ? "hidden" : "visible");
				}
		}
		updateLfoMenu();
	}

	void TrackView::updateOutput() const
	{
		const auto* out = m_editor.getProcessor().getController().getParameter("Out", m_part);
		if(auto* text = find("output_text"))
			text->SetInnerRML(out && out->getUnnormalizedValue() ? "Out " + twoDigits(m_part + 1) : std::string("Main"));
	}

	void TrackView::updateLfoMenu() const
	{
		auto& controller = m_editor.getProcessor().getController();
		const auto* lfoTrack = controller.getParameter("LfoTrack", m_part);
		const auto target = static_cast<uint8_t>(lfoTrack ? lfoTrack->getUnnormalizedValue() : m_part);
		const auto* machine = controller.getParameter("Machine", target);
		const auto id = static_cast<uint16_t>(machine ? machine->getUnnormalizedValue() : 0);

		if(auto* label = find("lfomenu_label"))
			label->SetInnerRML("LFO " + twoDigits(m_part + 1) + " modulates");
		if(auto* hint = find("lfomenu_hint"))
		{
			const auto* m = md::machines::find(md::MachineModel::Machinedrum, id);
			hint->SetInnerRML("track " + twoDigits(target + 1) + (m ? ", " + std::string(m->name) : std::string()));
		}

		// The target Track's parameters by its machine's names: "—", dimmed, for a SYN slot it does not use
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum, id);
		for(size_t i = 0; i < 24; ++i)
		{
			auto* cell = find("lfomenu_param" + std::to_string(i + 1));
			if(!cell)
				continue;
			std::string name = i < 8 ? (names ? std::string((*names)[i]) : "SYN" + std::to_string(i + 1)) : g_effectNames[i - 8];
			cell->SetClass("off", name.empty());
			cell->SetInnerRML(name.empty() ? "—" : name);
		}
	}

	void TrackView::updateSize() const
	{
		const auto scale = juce::roundToInt(m_editor.getProcessor().getConfig().getDoubleValue("scale", 100));
		if(auto* value = find("size_value"))
			value->SetInnerRML(std::to_string(scale) + "%");
	}

	void TrackView::show(const std::string& _id, const bool _visible) const
	{
		if(auto* e = find(_id))
			e->SetProperty("display", _visible ? "block" : "none");
	}

	Rml::Element* TrackView::find(const std::string& _id) const
	{
		return m_editor.findChild(_id, false);
	}
}
