#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Rml
{
	class Element;
	class Event;
}

namespace mdJucePlugin
{
	class Controller;

	// The commands over a JOUER grid, Machinedrum and Monomachine alike, named by the view's prefix (mdPlay,
	// mmPlay): LONGUEUR (<prefix>Length) opens a menu of the lengths, 1 to 64; COPIER VERS (<prefix>Copy) a
	// menu of the slots, A01 to H16, the pattern shown off, to copy it there (Controller::copyPattern): at
	// once onto a slot the library knows empty, otherwise once the button, then REMPLACER B05 ?, is clicked
	// again within ConfirmMilliseconds; TOUT EFFACER (<prefix>Clear), clicked twice within it, clears every
	// trig and lock. Edits are written as a click on a step is. The pattern line (<prefix>Info) tells how a
	// copy of the pattern shown goes.
	class PatternCommands
	{
	public:
		static constexpr double ConfirmMilliseconds = 3000.0;

		// _changed: after an edit, for the view to redraw
		PatternCommands(Controller& _controller, Rml::Element& _document, const std::string& _prefix,
			std::function<void()> _changed);

		// The pattern shown (nullopt: none), its length and its line ("pattern A01 · 16 pas · 3 trigs")
		void show(std::optional<uint8_t> _slot, uint8_t _length, const std::string& _line);
		// Disarms what was not confirmed in time and follows the copy. Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// What a slot chosen in COPIER VERS's menu does
		void copyTo(uint8_t _slot);
		bool isCopyArmed() const { return m_copyArmedTo < 128; }
		bool isClearArmed() const { return m_clearArmedAt >= 0.0; }

	private:
		void openLengthMenu(const Rml::Event& _event);
		void openCopyMenu(const Rml::Event& _event);
		void clear(double _nowMilliseconds);
		void disarmClear();
		void disarmCopy();
		// COPIER VERS as the copy goes, and the pattern line with it
		void renderCopy();

		Controller& m_controller;
		std::function<void()> m_changed;
		Rml::Element* m_info = nullptr;
		Rml::Element* m_length = nullptr;
		Rml::Element* m_copy = nullptr;
		Rml::Element* m_clear = nullptr;
		std::string m_shownInfo;
		std::string m_shownLength;
		std::string m_shownCopyLabel;
		std::optional<uint8_t> m_slot;		// the pattern shown
		uint8_t m_patternLength = 0;
		std::string m_line;					// its line, before what the copy adds
		double m_clearArmedAt = -1.0;
		uint8_t m_copyArmedFrom = 0xff;		// the pattern shown when the menu was used
		uint8_t m_copyArmedTo = 0xff;
		double m_copyArmedAt = -1.0;
		uint64_t m_shownCopy = ~uint64_t{0};	// the copy as last shown: state, slots and serial
	};
}
