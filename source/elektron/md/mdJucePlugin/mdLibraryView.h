#pragma once

#include "mdLib/mdtypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// BIBLIO, KITS (board 8): every stored Kit of the machine, number and name
	// (mdLibKit<slot>), read with Controller::readKitLibrary when LIRE LES KITS
	// (mdLibRead) is clicked, the current Kit marked; a click on a Kit shows its
	// machines (mdLibDetail, mdLibMachine<track>) without loading it.
	class LibraryView
	{
	public:
		LibraryView(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Shows the library as last read. Returns true when it changed the DOM.
		bool update();

		// "12 BROKEN DUB", "12 —" for a Kit not read
		static std::string kitLabel(uint8_t _slot, bool _read, const std::string& _name);

	private:
		void select(uint8_t _slot);
		void renderDetail();

		Controller& m_controller;
		const md::MachineModel m_model;
		Rml::Element* m_info = nullptr;
		Rml::Element* m_read = nullptr;
		Rml::Element* m_detail = nullptr;
		std::vector<Rml::Element*> m_kits;
		std::vector<std::string> m_shownLabels;
		std::vector<Rml::Element*> m_machines;
		uint8_t m_selected = 0xff;
		uint64_t m_shownRevision = ~uint64_t{0};
		uint8_t m_shownCurrent = 0xfe;
		bool m_shownReading = false;
	};
}
