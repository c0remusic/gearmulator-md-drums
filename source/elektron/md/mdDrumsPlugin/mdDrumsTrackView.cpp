#include "mdDrumsTrackView.h"

#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsEngine.h"
#include "mdDrumsFrame.h"
#include "mdDrumsKitsView.h"
#include "mdDrumsLfoView.h"
#include "mdDrumsMessages.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace mdDrums
{
	namespace
	{
		constexpr int g_window = 1296;
		constexpr const char* g_listenKey = "listenWhileChoosing";
		constexpr const char* g_velocityKey = "playVelocity";

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
	}

	TrackView::TrackView(Editor& _editor) : m_editor(_editor)
	{
		auto& controller = m_editor.getProcessor().getController();
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			m_machineParameters[t] = controller.getParameter("Machine", t);
			m_outParameters[t] = controller.getParameter("Out", t);
			m_lfoTrackParameters[t] = controller.getParameter("LfoTrack", t);
		}
		// Nothing drawn yet: the first frame draws it all
		m_outs.fill(-1);
		m_menuKey.fill(-1);

		// The shown Track: the list's rows, the Mix strips' and the sends' numbers and machines, ‹ ›
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto n = std::to_string(t + 1);
			const auto show = [this, t](Rml::Event&) { showTrack(t); };
			m_shownMarks[t] = {m_editor.addClick("row" + n + "_num", show),
				m_editor.addClick("row" + n + "_name", show), m_editor.addClick("strip" + n + "_num", show),
				m_editor.addClick("send" + n + "_num", show)};
			m_editor.addClick("strip" + n + "_name", show);
			m_editor.addClick("send" + n + "_name", show);
		}
		// The Mix strips' Outs
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			auto* e = m_editor.addClick("strip" + std::to_string(t + 1) + "_out", [this, t](Rml::Event&)
			{
				setStripOut(t, value(m_outParameters[t]) ? 0 : naturalOut(t));
			});
			if(e)
				juceRmlUi::EventListener::Add(e, Rml::EventId::Mousescroll, [this, t](Rml::Event& _event)
				{
					if(const auto sign = wheelSign(_event))
						setStripOut(t, value(m_outParameters[t]) + sign);
					_event.StopPropagation();
				});
		}
		m_editor.addClick("track_prev", [this](Rml::Event&) { stepTrack(-1); });
		m_editor.addClick("track_next", [this](Rml::Event&) { stepTrack(1); });
		m_editor.addClick("size", [this](Rml::Event&) { stepSize(); });

		// The overlays: a tab closes them; the machine's name, the LFO's target and the kit row open and close theirs
		for(const auto* tab : {"tab_track", "tab_mix", "tab_master"})
			m_editor.addClick(tab, [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("machine", [this](Rml::Event&) { toggleOverlay(Overlay::Browser); });
		m_editor.addClick("browser_keep", [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("browser_cancel", [this](Rml::Event&) { escape(); });
		// A machine chosen (its cell has set Machine by then: its own listener came first) shows in the head band and
		// plays the Track when listening
		for(const auto& machine : md::machines::machines(md::MachineModel::Machinedrum))
			if(m_editor.addClick("mach_" + std::to_string(machine.id), [this](Rml::Event&)
			{
				followMachine();
				if(isListening())
					m_editor.controller().audition(m_part, ListenVelocity);
			}))
				m_machines.push_back(machine.id);
		// The table goes family by family: the browser's order is the ids'
		std::sort(m_machines.begin(), m_machines.end());
		m_editor.addClick("preview_listen", [this](Rml::Event&) { setListening(!isListening()); });
		setListening(isListening());

		// The play key plays as it goes down; the velocity follows a vertical drag, 1.5 px a step
		if(auto* key = find("play"))
			juceRmlUi::EventListener::Add(key, Rml::EventId::Mousedown, [this](Rml::Event&) { play(); });
		if(auto* velocity = find("velocity"))
		{
			juceRmlUi::EventListener::Add(velocity, Rml::EventId::Mousedown, [this](Rml::Event& _event)
			{
				m_dragY = juceRmlUi::helper::getMousePos(_event).y;
				m_dragFrom = getPlayVelocity();
			});
			juceRmlUi::EventListener::Add(velocity, Rml::EventId::Drag, [this, velocity](Rml::Event& _event)
			{
				const auto* context = velocity->GetContext();
				const auto dp = context ? context->GetDensityIndependentPixelRatio() : 1.0f;
				const auto steps = (m_dragY - juceRmlUi::helper::getMousePos(_event).y) / dp / PixelsPerVelocityStep;
				setPlayVelocity(m_dragFrom + static_cast<int>(std::lround(steps)));
			});
		}
		setPlayVelocity(getPlayVelocity());
		// The wheel on the machine's name steps it
		if(auto* machine = find("machine"))
			juceRmlUi::EventListener::Add(machine, Rml::EventId::Mousescroll, [this](Rml::Event& _event)
			{
				if(const auto sign = wheelSign(_event))
					stepMachine(sign);
				_event.StopPropagation();
			});
		m_editor.addClick("lfo_target", [this](Rml::Event&) { toggleOverlay(Overlay::LfoMenu); });
		m_editor.addClick("lfomenu_done", [this](Rml::Event&) { setOverlay(Overlay::None); });
		// Choosing the parameter closes the menu, after the button has set it
		for(int p = 1; p <= Engine::ParamCount; ++p)
			m_editor.addClick("lfomenu_param" + std::to_string(p), [this](Rml::Event&) { setOverlay(Overlay::None); });
		m_editor.addClick("kit", [this](Rml::Event&) { toggleOverlay(Overlay::Kits); });
		m_editor.addClick("kits_done", [this](Rml::Event&) { setOverlay(Overlay::None); });

		updateSize();
	}

	TrackView::~TrackView() = default;

	int TrackView::value(const pluginLib::Parameter* _parameter)
	{
		return _parameter ? _parameter->getUnnormalizedValue() : 0;
	}

	bool TrackView::step(const Frame& _frame)
	{
		bool wrote = false;
		if(_frame.shownChanged)
		{
			showPart(_frame.shown);
			wrote = true;
		}
		wrote |= followMachine();
		wrote |= followOuts();
		wrote |= followLfoMenu();
		return wrote;
	}

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
			auto* machine = m_machineParameters[m_part];
			if(machine && m_machineBeforeBrowser >= 0 && machine->getUnnormalizedValue() != m_machineBeforeBrowser)
				machine->setUnnormalizedValueNotifyingHost(m_machineBeforeBrowser, pluginLib::Parameter::Origin::Ui);
			followMachine();
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

	int TrackView::getPlayVelocity() const
	{
		return std::clamp(m_editor.getProcessor().getConfig().getIntValue(g_velocityKey, 100), 1, 127);
	}

	void TrackView::setPlayVelocity(const int _velocity) const
	{
		const auto velocity = std::clamp(_velocity, 1, 127);
		m_editor.getProcessor().getConfig().setValue(g_velocityKey, velocity);
		setText(find("velocity"), std::to_string(velocity));
		// The bar: 64 px at 127, as the mockup rounds it
		setDp(find("velocity_fill"), Rml::PropertyId::Width, static_cast<float>(std::lround(velocity * 64.0 / 127.0)));
	}

	void TrackView::play() const
	{
		m_editor.controller().audition(m_part, static_cast<uint8_t>(getPlayVelocity()));
	}

	void TrackView::choose(const int _machine)
	{
		auto* machine = m_machineParameters[m_part];
		if(!machine)
			return;
		machine->setUnnormalizedValueNotifyingHost(_machine, pluginLib::Parameter::Origin::Ui);
		followMachine();
		if(isListening())
			m_editor.controller().audition(m_part, ListenVelocity);
	}

	void TrackView::stepMachine(const int _delta)
	{
		if(m_machines.empty())
			return;
		const auto current = value(m_machineParameters[m_part]);
		const auto n = static_cast<int>(m_machines.size());
		// A machine the browser does not offer steps from where it would sit among them
		const auto at = static_cast<int>(std::lower_bound(m_machines.begin(), m_machines.end(), current) - m_machines.begin());
		const auto offered = at < n && m_machines[static_cast<size_t>(at)] == current;
		const auto next = _delta > 0 ? (offered ? at + 1 : at) : at - 1;
		choose(m_machines[static_cast<size_t>((next % n + n) % n)]);
	}

	void TrackView::stepFamily(const int _delta)
	{
		if(m_machines.empty())
			return;
		const auto familyName = [](const int _id)
		{
			const auto* family = md::machines::familyOf(md::MachineModel::Machinedrum, static_cast<uint16_t>(_id));
			return family ? std::string(family->name) : std::string();
		};
		// The browser's families in its order, each by its first machine
		std::vector<std::pair<std::string, int>> families;
		for(const auto id : m_machines)
			if(families.empty() || families.back().first != familyName(id))
				families.emplace_back(familyName(id), id);
		const auto current = familyName(value(m_machineParameters[m_part]));
		const auto n = static_cast<int>(families.size());
		int at = 0;
		while(at < n && families[static_cast<size_t>(at)].first != current)
			++at;
		const auto next = at < n ? at + _delta : (_delta > 0 ? 0 : n - 1);
		choose(families[static_cast<size_t>((next % n + n) % n)].second);
	}

	bool TrackView::key(Rml::Event& _event)
	{
		const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
		auto* context = m_editor.getRmlComponent() ? m_editor.getRmlComponent()->getContext() : nullptr;
		const auto* focus = context ? context->GetFocusElement() : nullptr;
		if(key == Rml::Input::KI_SPACE)
		{
			// Every element takes the focus when clicked: only a text field keeps Space for itself
			if(focus && (focus->GetTagName() == "input" || focus->GetTagName() == "textarea"))
				return false;
			play();
			return true;
		}
		const bool up = key == Rml::Input::KI_UP, down = key == Rml::Input::KI_DOWN;
		if(m_overlay == Overlay::Browser)
		{
			if(up || down)
				stepMachine(down ? 1 : -1);
			else if(key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT)
				stepFamily(key == Rml::Input::KI_RIGHT ? 1 : -1);
			else if(key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER)
				setOverlay(Overlay::None);
			else
				return false;
			return true;
		}
		const auto* machine = find("machine");
		if((up || down) && focus && machine && (focus == machine || juceRmlUi::helper::isChildOf(machine, focus)))
		{
			stepMachine(down ? 1 : -1);
			return true;
		}
		return false;
	}

	void TrackView::setOverlay(const Overlay _overlay)
	{
		// An overlay takes ASSIGN's place
		if(_overlay != Overlay::None)
			if(auto* lfo = m_editor.getLfoView())
				lfo->disarm();
		if(_overlay == Overlay::Browser && m_overlay != Overlay::Browser)
		{
			const auto* machine = m_machineParameters[m_part];
			m_machineBeforeBrowser = machine ? machine->getUnnormalizedValue() : -1;
		}
		m_overlay = _overlay;
		if(auto* kits = m_editor.getKitsView())
			kits->setOpen(_overlay == Overlay::Kits);

		// The Kits take the Track tab under the kit row; the browser takes the bands' place; the menu, the LFO screen's
		setDisplay(find("track_main"), _overlay != Overlay::Kits);
		setDisplay(find("kits"), _overlay == Overlay::Kits);
		setDisplay(find("track_bands"), _overlay != Overlay::Browser);
		setDisplay(find("browser"), _overlay == Overlay::Browser);
		setDisplay(find("lfomenu"), _overlay == Overlay::LfoMenu);
		setDisplay(find("linksmenu"), _overlay == Overlay::Links);
		followLfoMenu();
	}

	void TrackView::showPart(const uint8_t _part)
	{
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
			for(size_t i = 0; i < m_shownMarks[t].size(); ++i)
				if(auto* e = m_shownMarks[t][i])
					e->SetClass(i < 2 ? "tsel" : "on", t == _part);
		if(auto* caption = find("caption"))
			caption->SetInnerRML("Track " + twoDigits(_part + 1));
		if(auto* forTrack = find("browser_for"))
			forTrack->SetInnerRML("for track " + twoDigits(_part + 1));

		// Another Track's machine, Out and LFO menu: drawn by the follow functions that come next
		m_machineShown = -1;
		m_outShown = -1;
	}

	bool TrackView::followMachine()
	{
		const auto machine = value(m_machineParameters[m_part]);
		if(machine == m_machineShown)
			return false;
		m_machineShown = machine;
		updateMachine();
		return true;
	}

	bool TrackView::followOuts()
	{
		bool wrote = false;
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto out = value(m_outParameters[t]);
			if(out == m_outs[t])
				continue;
			m_outs[t] = out;
			updateStripOut(t);
			wrote = true;
		}
		if(m_outs[m_part] == m_outShown)
			return wrote;
		m_outShown = m_outs[m_part];
		updateOutput();
		return true;
	}

	bool TrackView::followLfoMenu()
	{
		// Hidden, the menu waits: drawn anew as it opens
		if(m_overlay != Overlay::LfoMenu)
			return false;
		const auto target = std::clamp(value(m_lfoTrackParameters[m_part]), 0, Controller::TrackCount - 1);
		const std::array<int, 3> key{m_part, target, value(m_machineParameters[target])};
		if(key == m_menuKey)
			return false;
		m_menuKey = key;
		updateLfoMenu();
		return true;
	}

	void TrackView::updateMachine() const
	{
		const auto id = static_cast<uint16_t>(m_machineShown);

		const auto* family = md::machines::familyOf(md::MachineModel::Machinedrum, id);
		if(auto* code = find("family_code"))
			code->SetInnerRML(family ? std::string(family->name) : std::string());
		if(auto* text = find("family_text"))
			text->SetInnerRML(family ? familyText(family->name) : "");
		if(auto* kind = find("preview_kind"))
			kind->SetInnerRML(family ? familyKind(family->name) : "");

		// SYN1-8 by the machine's own names; a slot it does not use reads "—", dimmed, without a value
		for(size_t i = 0; i < 8; ++i)
		{
			const auto suffix = std::string("_") + Engine::paramNames()[i];
			const auto name = std::string(Engine::parameterName(id, i));
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
	}

	void TrackView::updateOutput() const
	{
		if(auto* text = find("output_text"))
			text->SetInnerRML(m_outShown ? "Out " + twoDigits(m_outShown) : std::string("Main"));
	}

	void TrackView::updateStripOut(const uint8_t _track) const
	{
		const auto out = m_outs[_track];
		if(auto* e = find("strip" + std::to_string(_track + 1) + "_out"))
		{
			// On an Out: the Out it plays on; on Main: the one a click sends it to
			e->SetInnerRML("Out " + twoDigits(out ? out : naturalOut(_track)));
			e->SetClass("on", out != 0);
		}
	}

	uint8_t TrackView::naturalOut(const uint8_t _track)
	{
		return static_cast<uint8_t>(std::min<int>(_track + 1, mdDrums::messages::OutCount));
	}

	void TrackView::setStripOut(const uint8_t _track, const int _out)
	{
		auto* out = m_outParameters[_track];
		if(!out)
			return;
		out->setUnnormalizedValueNotifyingHost(std::clamp(_out, 0, static_cast<int>(mdDrums::messages::OutCount)),
			pluginLib::Parameter::Origin::Ui);
		followOuts();
	}

	void TrackView::updateLfoMenu() const
	{
		const auto target = static_cast<uint8_t>(m_menuKey[1]);
		const auto id = static_cast<uint16_t>(m_menuKey[2]);

		if(auto* label = find("lfomenu_label"))
			label->SetInnerRML("LFO " + twoDigits(m_part + 1) + " modulates");
		if(auto* hint = find("lfomenu_hint"))
		{
			const auto* m = md::machines::find(md::MachineModel::Machinedrum, id);
			hint->SetInnerRML("track " + twoDigits(target + 1) + (m ? ", " + std::string(m->name) : std::string()));
		}

		// The target Track's parameters by its machine's names: "—", dimmed, for a SYN slot it does not use
		for(size_t i = 0; i < Engine::paramNames().size(); ++i)
		{
			auto* cell = find("lfomenu_param" + std::to_string(i + 1));
			if(!cell)
				continue;
			const auto name = std::string(Engine::parameterName(id, i));
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

	Rml::Element* TrackView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}
}
