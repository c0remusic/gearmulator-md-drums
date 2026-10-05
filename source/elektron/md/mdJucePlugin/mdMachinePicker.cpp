#include "mdMachinePicker.h"

#include "mdController.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <string>

namespace mdJucePlugin
{
	namespace
	{
		void setVisible(Rml::Element* _element, const bool _visible)
		{
			if(!_element)
				return;
			if(_visible)
				_element->RemoveProperty(Rml::PropertyId::Display);
			else
				_element->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		}

		std::string trackLabel(const uint8_t _part)
		{
			return "piste " + std::to_string(_part + 1);
		}
	}

	MachinePicker::MachinePicker(Controller& _controller, const md::MachineModel _model, Rml::Element& _document)
		: m_controller(_controller)
		, m_model(_model)
	{
		m_family = _document.GetElementById("mdEdMachineFamily");
		m_name = _document.GetElementById("mdEdMachineName");
		m_info = _document.GetElementById("mdEdMachineInfo");
		m_button = _document.GetElementById("mdEdMachineChange");
		m_picker = _document.GetElementById("mdEdMachinePicker");
		m_families = _document.GetElementById("mdEdPickerFamilies");
		m_machines = _document.GetElementById("mdEdPickerMachines");
		m_pickerInfo = _document.GetElementById("mdEdPickerInfo");

		for(size_t i = 0; i < m_synthesis.size(); ++i)
		{
			const auto param = m_model == md::MachineModel::Monomachine
				? "Synthesis" + std::string(1, static_cast<char>('A' + i))
				: "MachineParameter" + std::to_string(i + 1);
			auto& c = m_synthesis[i];
			c.control = _document.GetElementById("mdEdCtl_" + param);
			c.value = _document.GetElementById("mdEdVal_" + param);
			c.name = _document.GetElementById("mdEdName_" + param);
			if(c.name)
				c.generic = c.name->GetInnerRML();
		}

		createPicker();
		if(m_button && m_picker)
		{
			juceRmlUi::EventListener::Add(m_button, Rml::EventId::Click, [this](Rml::Event&)
			{
				setOpen(!m_open);
			});
		}
		setOpen(false);
	}

	bool MachinePicker::update()
	{
		if(!m_name)
			return false;
		const auto part = m_controller.getCurrentPart();
		const auto machine = m_controller.getTrackMachine(part);
		const auto revision = m_controller.getMachineRevision();
		if(part == m_shownPart && machine == m_shownMachine && revision == m_shownRevision)
			return false;
		const bool partChanged = part != m_shownPart;
		m_shownPart = part;
		m_shownMachine = machine;
		m_shownRevision = revision;
		renderHeader();
		// Another track: close the picker rather than show it for the wrong track.
		if(partChanged && m_open)
			setOpen(false);
		else if(m_open)
			renderPicker();
		return true;
	}

	void MachinePicker::setOpen(const bool _open)
	{
		m_open = _open && m_picker;
		if(m_open)
		{
			if(const auto* family = md::machines::familyOf(m_model, m_controller.getTrackMachine(m_controller.getCurrentPart())))
				m_selectedFamily = static_cast<uint8_t>(family - md::machines::families(m_model).data());
			renderPicker();
		}
		setVisible(m_picker, m_open);
		if(m_button)
			juceRmlUi::ElemButton::setChecked(m_button, m_open);
	}

	void MachinePicker::selectFamily(const uint8_t _family)
	{
		if(_family >= md::machines::families(m_model).size())
			return;
		m_selectedFamily = _family;
		renderPicker();
	}

	bool MachinePicker::assign(const uint16_t _machine)
	{
		if(!m_controller.assignMachine(m_controller.getCurrentPart(), _machine))
			return false;
		setOpen(false);
		update();
		return true;
	}

	void MachinePicker::renderHeader()
	{
		const auto* machine = md::machines::find(m_model, m_shownMachine);
		const auto* family = machine ? &md::machines::families(m_model)[machine->family] : nullptr;

		if(m_family)
			m_family->SetInnerRML(family ? std::string(family->name) : "—");
		m_name->SetInnerRML(machine ? std::string(machine->name) : "—");
		renderParameterNames();
		if(!m_info)
			return;
		if(machine)
			m_info->SetInnerRML(std::string(family->synthesis));
		else if(m_shownMachine == md::machines::g_unknown)
			m_info->SetInnerRML("machine inconnue : en attente du kit");
		else
			m_info->SetInnerRML("machine " + std::to_string(m_shownMachine) + " : absente de la table du plug-in");
	}

	void MachinePicker::renderParameterNames()
	{
		// An unknown machine keeps the skin's generic names; a parameter the machine does not use is
		// dimmed and does not take the pointer, as turning it changes nothing
		const auto* names = md::machines::parameterNames(m_model, m_shownMachine);
		for(size_t i = 0; i < m_synthesis.size(); ++i)
		{
			const auto& c = m_synthesis[i];
			if(!c.name)
				continue;
			const bool unused = names && (*names)[i].empty();
			c.name->SetInnerRML(!names ? c.generic : unused ? std::string("—") : std::string((*names)[i]));
			for(auto* element : {c.control, c.value, c.name})
			{
				if(element)
					element->SetClass("mdEdUnused", unused);
			}
		}
	}

	void MachinePicker::createPicker()
	{
		if(!m_families || !m_machines)
			return;
		auto* document = m_families->GetOwnerDocument();
		if(!document)
			return;

		const auto& families = md::machines::families(m_model);
		const auto& machines = md::machines::machines(m_model);
		for(size_t i = 0; i < families.size(); ++i)
		{
			// A family with nothing to offer (NFX) gets no tab.
			if(std::none_of(machines.begin(), machines.end(), [i](const md::machines::Machine& _m) { return _m.family == i && _m.assignable; }))
				continue;
			auto tab = document->CreateElement("div");
			tab->SetId("mdEdFamily" + std::to_string(i));
			tab->SetClass("mdEdPickerFamily", true);
			tab->SetInnerRML(std::string(families[i].name));
			auto* element = m_families->AppendChild(std::move(tab));
			juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [this, i](Rml::Event&)
			{
				selectFamily(static_cast<uint8_t>(i));
			});
			m_familyTabs.push_back({element, static_cast<uint8_t>(i)});
		}

		for(const auto& machine : md::machines::machines(m_model))
		{
			if(!machine.assignable)
				continue;
			auto tile = document->CreateElement("div");
			tile->SetId("mdEdMachine" + std::to_string(machine.id));
			tile->SetClass("mdEdPickerMachine", true);
			tile->SetInnerRML(std::string(machine.name));
			auto* element = m_machines->AppendChild(std::move(tile));
			const auto id = machine.id;
			juceRmlUi::EventListener::Add(element, Rml::EventId::Click, [this, id](Rml::Event&)
			{
				assign(id);
			});
			m_tiles.push_back({element, &machine});
		}
	}

	void MachinePicker::renderPicker()
	{
		const auto& families = md::machines::families(m_model);
		const auto part = m_controller.getCurrentPart();
		const auto current = m_controller.getTrackMachine(part);

		for(const auto& tab : m_familyTabs)
			tab.element->SetClass("mdEdSelected", tab.family == m_selectedFamily);
		for(const auto& tile : m_tiles)
		{
			setVisible(tile.element, tile.machine->family == m_selectedFamily);
			tile.element->SetClass("mdEdSelected", tile.machine->id == current);
		}

		if(m_pickerInfo)
		{
			const auto& family = families[m_selectedFamily];
			// What the firmware does with the track's values (Controller::assignMachine)
			const std::string values = m_model == md::MachineModel::Monomachine
				? " ; ses réglages sont conservés, sauf ceux de synthèse que la machine adapte"
				: ", avec ses réglages par défaut (synthèse, effets, routage)";
			m_pickerInfo->SetInnerRML(std::string(family.name) + " · " + std::string(family.synthesis)
				+ ". Un clic assigne la machine à la " + trackLabel(part) + values
				+ ". Le plug-in ne peut pas relire ces valeurs : elles restent grisées (—) jusqu'au prochain chargement"
				" de kit ou jusqu'à ce qu'un contrôle les fixe. Une machine changée sur la face avant n'apparaît"
				" qu'au prochain chargement de kit.");
		}
	}
}
