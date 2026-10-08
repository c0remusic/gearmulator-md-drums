#include "mdDrumsTrackView.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlEventListener.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace mdDrums
{
	namespace
	{
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

		// What a family is, as the head band says it under the machine (the mockup's words; mdmachines' are md-mm's)
		std::string familyText(const std::string_view _code)
		{
			static const std::map<std::string_view, const char*> texts{{"GND", "generator"}, {"TRX", "modelled analog"},
				{"EFM", "FM synthesis"}, {"E12", "built-in 12-bit sample"}, {"P-I", "physical modelling"},
				{"ROM", "UW bank sample"}, {"RAM", "RAM sample"}, {"INP", "external input"}, {"MID", "MIDI"},
				{"CTR", "control"}, {"NFX", "effect"}};
			const auto it = texts.find(_code);
			return it != texts.end() ? it->second : "";
		}
	}

	TrackView::TrackView(Editor& _editor) : m_editor(_editor)
	{
		auto& controller = m_editor.getProcessor().getController();

		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto row = "row" + std::to_string(t + 1);
			for(const auto& id : {row + "_num", row + "_name"})
				m_editor.addClick(id, [this, t](Rml::Event&) { showTrack(t); });
		}
		m_editor.addClick("track_prev", [this](Rml::Event&) { stepTrack(-1); });
		m_editor.addClick("track_next", [this](Rml::Event&) { stepTrack(1); });
		m_editor.addClick("size", [this](Rml::Event&) { stepSize(); });

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

	void TrackView::onPartChanged(const uint8_t _part)
	{
		auto& controller = m_editor.getProcessor().getController();
		m_part = _part;

		// The shown row: lit to the gutter's middle, its mark at the window's edge, its number and machine in ink
		const auto top = ListTop + RowHeight * _part;
		if(auto* bg = find("row_sel_bg"))
			bg->SetProperty("top", dp(top));
		if(auto* mark = find("row_sel_mark"))
			mark->SetProperty("top", dp(top + 8));
		for(uint8_t t = 0; t < Controller::TrackCount; ++t)
		{
			const auto row = "row" + std::to_string(t + 1);
			for(const auto& id : {row + "_num", row + "_name"})
				if(auto* e = find(id))
					e->SetClass("tsel", t == _part);
		}
		if(auto* caption = find("caption"))
			caption->SetInnerRML("Track " + twoDigits(_part + 1));

		m_onMachineChanged.reset();
		m_onOutChanged.reset();
		if(auto* machine = controller.getParameter("Machine", _part))
			m_onMachineChanged.set(machine->onValueChanged, [this](pluginLib::Parameter* const&) { updateMachine(); });
		if(auto* out = controller.getParameter("Out", _part))
			m_onOutChanged.set(out->onValueChanged, [this](pluginLib::Parameter* const&) { updateOutput(); });
		updateMachine();
		updateOutput();
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
			text->SetInnerRML(family ? familyText(family->name) : std::string());

		// SYN1-8 by the machine's own names; a slot it does not use reads "—", dimmed, without a value
		const auto* names = md::machines::parameterNames(md::MachineModel::Machinedrum, id);
		for(size_t i = 0; i < 8; ++i)
		{
			const auto suffix = "_SYN" + std::to_string(i + 1);
			const auto name = names ? std::string((*names)[i]) : "SYN" + std::to_string(i + 1);
			const auto unused = name.empty();
			if(auto* label = find("n" + suffix))
			{
				label->SetInnerRML(unused ? "—" : name);
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
		const auto* out = m_editor.getProcessor().getController().getParameter("Out", m_part);
		if(auto* text = find("output_text"))
			text->SetInnerRML(out && out->getUnnormalizedValue() ? "Out " + twoDigits(m_part + 1) : std::string("Main"));
	}

	void TrackView::updateSize() const
	{
		const auto scale = juce::roundToInt(m_editor.getProcessor().getConfig().getDoubleValue("scale", 100));
		if(auto* value = find("size_value"))
			value->SetInnerRML(std::to_string(scale) + "%");
	}

	Rml::Element* TrackView::find(const std::string& _id) const
	{
		return m_editor.findChild(_id, false);
	}
}
