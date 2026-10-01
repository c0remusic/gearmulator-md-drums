#include "mdLibraryView.h"

#include "mdController.h"

#include "mdLib/mdmachines.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

namespace mdJucePlugin
{
	namespace
	{
		std::string number(const unsigned _value)
		{
			return (_value < 10 ? "0" : "") + std::to_string(_value);
		}
	}

	LibraryView::LibraryView(Controller& _controller, const md::MachineModel _model, Rml::Element& _document)
		: m_controller(_controller)
		, m_model(_model)
	{
		m_info = _document.GetElementById("mdLibInfo");
		m_read = _document.GetElementById("mdLibRead");
		m_detail = _document.GetElementById("mdLibDetail");
		const auto kits = m_controller.getKitLibrarySize();
		for(uint8_t slot = 0; slot < kits; ++slot)
		{
			auto* kit = _document.GetElementById("mdLibKit" + std::to_string(slot));
			m_kits.push_back(kit);
			if(kit)
				juceRmlUi::EventListener::Add(kit, Rml::EventId::Click, [this, slot](Rml::Event&) { select(slot); });
		}
		const auto tracks = _model == md::MachineModel::Monomachine ? 6 : 16;
		for(int track = 0; track < tracks; ++track)
			m_machines.push_back(_document.GetElementById("mdLibMachine" + std::to_string(track)));
		if(m_read)
		{
			juceRmlUi::EventListener::Add(m_read, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_controller.readKitLibrary();
				update();
			});
		}
	}

	std::string LibraryView::kitLabel(const uint8_t _slot, const bool _read, const std::string& _name)
	{
		return number(_slot + 1u) + "  " + (_read ? (_name.empty() ? std::string("(sans nom)") : _name) : std::string("—"));
	}

	void LibraryView::select(const uint8_t _slot)
	{
		m_selected = _slot;
		m_shownRevision = ~uint64_t{0};
		update();
	}

	bool LibraryView::update()
	{
		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getKitLibraryRevision();
		const auto current = m_controller.getCurrentKit();
		const auto reading = m_controller.isReadingKitLibrary();
		if(revision == m_shownRevision && current == m_shownCurrent && reading == m_shownReading)
			return false;
		m_shownRevision = revision;
		m_shownCurrent = current;
		m_shownReading = reading;

		size_t read = 0;
		m_shownLabels.resize(m_kits.size());
		for(uint8_t slot = 0; slot < m_kits.size(); ++slot)
		{
			const auto kit = m_controller.getLibraryKit(slot);
			read += kit && kit->read ? 1 : 0;
			auto* element = m_kits[slot];
			if(!element)
				continue;
			// Only the cells that changed: a reading brings one Kit at a time
			auto label = kitLabel(slot, kit && kit->read, kit ? kit->name : std::string());
			if(label != m_shownLabels[slot])
			{
				element->SetInnerRML(Rml::StringUtilities::EncodeRml(label));
				m_shownLabels[slot] = std::move(label);
			}
			element->SetClass("mdLibCurrent", slot == current);
			element->SetClass("mdEdSelected", slot == m_selected);
			element->SetClass("mdEdUnread", !(kit && kit->read));
		}
		if(m_info)
		{
			const auto total = m_kits.size();
			std::string text;
			if(reading)
				text = "lecture des kits : " + std::to_string(m_controller.getKitLibraryProgress()) + " / " + std::to_string(total) + "…";
			else if(read == 0)
				text = "kits non lus : LIRE LES KITS les demande à la machine, un par un (lecture seule)";
			else
				text = std::to_string(read) + " kits lus sur " + std::to_string(total) + " · en ambre : le kit chargé · clic : ses machines, sans le charger";
			m_info->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}
		if(m_read)
			m_read->SetInnerRML(reading ? "LECTURE…" : "LIRE LES KITS");
		renderDetail();
		return true;
	}

	void LibraryView::renderDetail()
	{
		const auto kit = m_selected < m_kits.size() ? m_controller.getLibraryKit(m_selected) : std::nullopt;
		if(m_detail)
		{
			std::string title = "KIT —";
			if(m_selected < m_kits.size())
			{
				title = "KIT " + number(m_selected + 1u);
				if(kit && kit->read)
					title += " · " + (kit->name.empty() ? std::string("(sans nom)") : kit->name);
				if(m_selected == m_controller.getCurrentKit())
					title += " · chargé";
			}
			m_detail->SetInnerRML(Rml::StringUtilities::EncodeRml(title));
		}
		for(size_t track = 0; track < m_machines.size(); ++track)
		{
			auto* element = m_machines[track];
			if(!element)
				continue;
			std::string text = number(static_cast<unsigned>(track + 1)) + "  ";
			if(kit && kit->read && track < kit->machines.size())
			{
				const auto* machine = md::machines::find(m_model, kit->machines[track]);
				text += machine ? std::string(machine->name) : "machine " + std::to_string(kit->machines[track]);
			}
			else
				text += "—";
			element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}
	}
}
