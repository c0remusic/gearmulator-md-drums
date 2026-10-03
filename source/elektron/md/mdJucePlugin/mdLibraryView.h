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

	// BIBLIO (board 8), two tabs. KITS: every stored Kit of the machine, number and name (mdLibKit<slot>),
	// the current Kit marked; a click on a Kit shows its machines (mdLibDetail, mdLibMachine<track>) without
	// loading it. PATTERNS: every stored pattern, its length and Kit (mdLibPattern<slot>), the current
	// pattern marked; a click shows it in mdLibPatternDetail. COPIER (mdLibCopy) takes the pattern clicked
	// as the one to copy, COLLER (mdLibPaste) copies it onto the pattern clicked then
	// (Controller::copyPattern): at once onto one the library knows empty, otherwise once COLLER, then
	// REMPLACER B05 ?, is clicked again within ConfirmMilliseconds. The library is read
	// (Controller::readLibrary) when BIBLIO first shows, as soon as the firmware takes requests, and again
	// with RELIRE (mdLibRead); what was read stays in the controller while the plug-in is open. mdLibInfo
	// tells what the reading does.
	class LibraryView
	{
	public:
		static constexpr double ConfirmMilliseconds = 3000.0;

		LibraryView(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Starts the first reading when BIBLIO shows, and shows the library as last read. Returns true when
		// it changed the DOM.
		bool update(double _nowMilliseconds);

		// "12 BROKEN DUB", "12 —" for a Kit not read
		static std::string kitLabel(uint8_t _slot, bool _read, const std::string& _name);
		// "A01  16 pas · kit 05", "A01  —" for a pattern not read
		static std::string patternLabel(uint8_t _slot, bool _read, uint8_t _length, uint8_t _kit);

	private:
		void select(uint8_t _slot);
		void selectPattern(uint8_t _slot);
		void paste();
		void startReading(double _nowMilliseconds);
		void renderDetail();
		void renderPatternDetail();
		void renderCopy();

		Controller& m_controller;
		const md::MachineModel m_model;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_info = nullptr;
		Rml::Element* m_read = nullptr;
		Rml::Element* m_detail = nullptr;
		Rml::Element* m_patternDetail = nullptr;
		std::vector<Rml::Element*> m_kits;
		std::vector<std::string> m_shownLabels;
		std::vector<Rml::Element*> m_patterns;
		std::vector<std::string> m_shownPatternLabels;
		std::vector<Rml::Element*> m_machines;
		uint8_t m_selected = 0xff;
		uint8_t m_selectedPattern = 0xff;
		Rml::Element* m_copy = nullptr;
		Rml::Element* m_paste = nullptr;
		std::string m_shownPasteLabel;
		uint8_t m_copySource = 0xff;		// COPIER's pattern
		uint32_t m_copySerial = 0;			// the copy COLLER started last: the line tells how it goes
		uint8_t m_pasteArmedTo = 0xff;		// REMPLACER waiting for its second click
		double m_pasteArmedAt = 0.0;
		double m_now = 0.0;
		uint64_t m_shownCopy = ~uint64_t{0};	// the copy as last shown: state, slots and serial
		// A reading asked for (first showing, or RELIRE) while the firmware does not take requests yet
		bool m_readingWanted = true;
		bool m_waitingForMachine = false;
		double m_lastAttempt = -1.0e9;
		uint64_t m_shownRevision = ~uint64_t{0};
		uint8_t m_shownCurrent = 0xfe;
		uint8_t m_shownCurrentPattern = 0xfe;
		bool m_shownReading = false;
		bool m_shownWaiting = false;
	};
}
