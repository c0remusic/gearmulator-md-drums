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
	// loading it. CHARGER (mdLibKitLoad) loads the Kit clicked once clicked again within ConfirmMilliseconds
	// (CHARGER KIT 05 ?): the live Kit's values not saved go (Controller::loadKit); on the current Kit,
	// RECHARGER brings back its stored values. SAUVER (mdLibKitSave) saves the live Kit into its slot
	// (Controller::saveKit). PATTERNS: every stored pattern, its length and Kit
	// (mdLibPattern<slot>), the current pattern marked; a click shows it in mdLibPatternDetail. COPIER
	// (mdLibCopy) takes the pattern clicked as the one to copy, COLLER (mdLibPaste) copies it onto the pattern
	// clicked then (Controller::copyPattern): at once onto one the library knows empty, otherwise once COLLER,
	// then REMPLACER B05 ?, is clicked again within ConfirmMilliseconds. CHARGER (mdLibPatternLoad) makes the
	// pattern clicked the machine's, once clicked again (CHARGER B05 ?): its Kit replaces the live one
	// (Controller::loadPattern), at the end of the pattern playing. The library is read
	// (Controller::readLibrary) when BIBLIO first shows, as soon as the firmware takes requests, and again
	// with RELIRE (mdLibRead); what was read stays in the controller while the plug-in is open. mdLibInfo
	// tells what the reading does, then what CHARGER asks to confirm, what a load or a save did.
	class LibraryView
	{
	public:
		static constexpr double ConfirmMilliseconds = 3000.0;
		// How long the line tells what a load or a save did
		static constexpr double NoteMilliseconds = 4000.0;

		LibraryView(Controller& _controller, md::MachineModel _model, Rml::Element& _document);

		// Starts the first reading when BIBLIO shows, and shows the library as last read. Returns true when
		// it changed the DOM.
		bool update(double _nowMilliseconds);

		// "12 BROKEN DUB", "12 —" for a Kit not read
		static std::string kitLabel(uint8_t _slot, bool _read, const std::string& _name);
		// "A01  16 pas · kit 05", "A01  —" for a pattern not read
		static std::string patternLabel(uint8_t _slot, bool _read, uint8_t _length, uint8_t _kit);

		// What CHARGER and SAUVER do, for the tests as for the buttons
		void loadKit();
		void saveKit();
		void loadPattern();
		bool isKitLoadArmed() const { return m_kitLoadArmed != 0xff; }
		bool isPatternLoadArmed() const { return m_patternLoadArmed != 0xff; }

	private:
		void select(uint8_t _slot);
		void selectPattern(uint8_t _slot);
		void paste();
		void note(std::string _text);
		std::string loadText() const;
		void renderLoad(bool _busy);
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
		Rml::Element* m_kitLoad = nullptr;
		Rml::Element* m_kitSave = nullptr;
		Rml::Element* m_patternLoad = nullptr;
		std::string m_shownKitLoadLabel;
		std::string m_shownKitSaveLabel;
		std::string m_shownPatternLoadLabel;
		// CHARGER waiting for its second click: the Kit or pattern, and since when
		uint8_t m_kitLoadArmed = 0xff;
		double m_kitLoadArmedAt = 0.0;
		uint8_t m_patternLoadArmed = 0xff;
		double m_patternLoadArmedAt = 0.0;
		// What the last load or save did, shown until m_noteUntil
		std::string m_note;
		double m_noteUntil = 0.0;
		bool m_shownBusy = false;
		bool m_shownPlaying = false;
		bool m_shownWaitingLong = false;
		// The pattern load the controller waits for, as last seen, and since when
		uint8_t m_shownPatternLoad = 0xff;
		double m_patternLoadSeenAt = 0.0;
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
