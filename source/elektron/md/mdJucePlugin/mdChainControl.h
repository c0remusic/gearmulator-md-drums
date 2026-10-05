#pragma once

#include "mdLib/mdchainplayer.h"
#include "mdLib/mdpatternchain.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace baseLib
{
	class BinaryStream;
}

namespace mdJucePlugin
{
	// The project's pattern chain: what the editor edits and the project saves, handed to
	// the md::ChainPlayer the device runs. It plays when it is on, the length of every pattern in it is
	// known, and the machine follows the host's transport (HostSync): the chain counts the host's clock.
	// Any thread: the host may save or load the project from its own.
	class ChainControl
	{
	public:
		using Entry = md::PatternChain::Entry;

		enum class State
		{
			Off,			// turned off
			Empty,			// no entry
			ReadingLengths,	// the length of a pattern in it is not known yet
			NotFollowing,	// the machine does not follow the host
			Playing			// handed to the player
		};

		explicit ChainControl(md::MachineModel _model);

		const std::shared_ptr<md::ChainPlayer>& getPlayer() const { return m_player; }

		bool isEnabled() const;
		void setEnabled(bool _enabled);
		std::vector<Entry> getEntries() const;
		// False, and the chain unchanged, for an entry out of range
		bool setEntries(std::vector<Entry> _entries);

		// A pattern's length in steps, from any pattern dump the controller sees
		void setLength(uint8_t _pattern, uint8_t _steps);
		std::optional<uint8_t> getLength(uint8_t _pattern) const;
		// The first pattern of the chain whose length is not known
		std::optional<uint8_t> getMissingLength() const;

		// Hands the chain to the player, or takes it back, as the state says. _following: the
		// machine follows the host.
		State update(bool _following);
		State getState() const;
		// Changes with the entries, the switch, a length and the state
		uint32_t getRevision() const;

		std::optional<md::ChainPlayer::Playing> getPlaying() const { return m_player->getPlaying(); }

		// The switch, the entries and their lengths
		void save(baseLib::BinaryStream& _stream) const;
		// False, and the chain unchanged, for data that is not a chain
		bool load(baseLib::BinaryStream& _stream);

	private:
		md::PatternChain makeChain() const;
		State updateLocked(bool _following);

		const md::MachineModel m_model;
		const std::shared_ptr<md::ChainPlayer> m_player;
		std::array<std::atomic<uint8_t>, 128> m_lengths{};
		std::atomic<uint32_t> m_lengthsChanged{0};

		mutable std::mutex m_mutex;
		bool m_enabled = false;
		std::vector<Entry> m_entries;
		uint32_t m_lengthsSeen = 0;
		bool m_following = false;
		State m_state = State::Off;
		uint32_t m_revision = 0;
		std::optional<md::PatternChain> m_handed;	// what the player has
	};
}
