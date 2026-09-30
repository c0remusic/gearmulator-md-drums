#pragma once

#include <cstdint>
#include <vector>

#include "mdLib/mdmachines.h"
#include "mdLib/mdtypes.h"

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// The MACHINE block of the editor's SON page: family, name and synthesis type
	// of the edited track's machine, and the picker that assigns another one.
	// Families and machines come from md::machines; the element ids from the skin
	// (mdEdMachine*, mdEdPicker*). Does nothing when the skin has no MACHINE block.
	class MachinePicker
	{
	public:
		MachinePicker(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Refreshes the block when the edited track or its machine changed.
		// Returns true when it changed the DOM.
		bool update();

		void setOpen(bool _open);
		bool isOpen() const { return m_open; }
		void selectFamily(uint8_t _family);
		bool assign(uint16_t _machine);

	private:
		void createPicker();
		void renderHeader();
		void renderPicker();

		Controller& m_controller;
		const md::MachineModel m_model;

		Rml::Element* m_family = nullptr;
		Rml::Element* m_name = nullptr;
		Rml::Element* m_info = nullptr;
		Rml::Element* m_button = nullptr;
		Rml::Element* m_picker = nullptr;
		Rml::Element* m_families = nullptr;
		Rml::Element* m_machines = nullptr;
		Rml::Element* m_pickerInfo = nullptr;

		// Created once and only shown, hidden or highlighted afterwards: removing an
		// element from inside its own click handler would destroy it mid-dispatch.
		struct FamilyTab
		{
			Rml::Element* element;
			uint8_t family;
		};
		std::vector<FamilyTab> m_familyTabs;
		struct Tile
		{
			Rml::Element* element;
			const md::machines::Machine* machine;
		};
		std::vector<Tile> m_tiles;

		bool m_open = false;
		uint8_t m_selectedFamily = 0;
		uint8_t m_shownPart = 0xff;
		uint16_t m_shownMachine = md::machines::g_unknown;
		uint64_t m_shownRevision = ~uint64_t{0};
	};
}
