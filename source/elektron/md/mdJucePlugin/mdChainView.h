#pragma once

#include "mdChainControl.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class Controller;

	// JOUER (Machinedrum), CHAÎNE tab: the project's pattern chain (ChainControl). The slots
	// mdChainSlot<0..15> show the entries, pattern then passes and length, the chosen one framed and the
	// one playing in orange. mdChainAdd adds the machine's current pattern after the chosen entry; the
	// chosen entry's pattern (its bank mdChainBank<0..7>, A to H, and number mdChainNumber<0..15>),
	// passes (mdChainPassesDown/Up) and place (mdChainMoveLeft/Right) change, mdChainRemove removes it,
	// mdChainClear empties the chain, and mdChainEnable turns it on or off. Without a chosen entry,
	// these act on the last one. mdChainState says what the chain does.
	class ChainView
	{
	public:
		using Entry = ChainControl::Entry;
		static constexpr size_t SlotCount = 16;
		static constexpr uint8_t BankCount = 8;
		static constexpr uint8_t NumberCount = 16;

		ChainView(ChainControl& _chain, Controller& _controller, Rml::Element& _document);

		// Refreshes what changed: the chain, its state, the pass playing, the machine's pattern.
		// Returns true when it changed the DOM.
		bool update();

		// "A01" ... "H16"
		static std::string patternName(uint8_t _pattern);
		// What the chain does, for mdChainState
		static std::string stateLine(ChainControl::State _state, const std::vector<Entry>& _entries,
			const std::optional<md::ChainPlayer::Playing>& _playing);
		// A slot's two lines: "A01 ×2" and "32 pas", the length "…" while unknown
		static std::string slotText(const Entry& _entry, std::optional<uint8_t> _length);

		std::optional<size_t> getChosen() const { return m_chosen; }

	private:
		void bindClick(const char* _id, std::function<void()> _action);
		// Edits the entries; the chosen entry follows _chosen
		void edit(const std::function<bool(std::vector<Entry>&, size_t& _chosen)>& _change);
		bool setText(Rml::Element* _element, std::string& _shown, const std::string& _text);

		ChainControl& m_chain;
		Controller& m_controller;
		Rml::Element* m_document = nullptr;
		Rml::Element* m_root = nullptr;
		Rml::Element* m_state = nullptr;
		Rml::Element* m_enable = nullptr;
		Rml::Element* m_add = nullptr;
		std::array<Rml::Element*, SlotCount> m_slots{};
		std::array<Rml::Element*, BankCount> m_banks{};
		std::array<Rml::Element*, NumberCount> m_numbers{};
		std::array<std::string, SlotCount> m_shownSlots{};
		std::string m_shownState;
		std::string m_shownAdd;
		std::optional<size_t> m_chosen;

		// What the slots show, to refresh only on change
		uint32_t m_shownRevision = ~0u;
		std::optional<md::ChainPlayer::Playing> m_shownPlaying;
		std::optional<size_t> m_shownChosen;
		int m_shownEnabled = -1;
		bool m_dirty = true;
	};
}
