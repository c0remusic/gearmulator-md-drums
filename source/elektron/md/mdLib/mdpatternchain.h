#pragma once

#include "mdtypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace md
{
	// A chain of patterns, in order, each played a number of passes, looping at the end.
	// Positions are MIDI clock ticks since the start of the song, as the machine counts them
	// when it follows the host: 6 per step, at the pattern's 1x scale.
	class PatternChain
	{
	public:
		static constexpr uint32_t TicksPerStep = 6;
		static constexpr uint8_t MaxPasses = 64;

		// The machine commits its next pattern this many ticks before the end of the playing
		// one, and a pattern asked for later waits one more pass: 7 on the Machinedrum
		// (patternChainFirmwareTest), 1 on the Monomachine (mmPatternChainFirmwareTest)
		static constexpr uint32_t commitTicks(const MachineModel _model)
		{
			return _model == MachineModel::Monomachine ? 1 : 7;
		}

		struct Entry
		{
			uint8_t pattern = 0;	// 0..127, A01..H16
			uint8_t passes = 1;		// 1..MaxPasses

			bool operator==(const Entry& _other) const { return pattern == _other.pattern && passes == _other.passes; }
			bool operator!=(const Entry& _other) const { return !(*this == _other); }
		};

		// One play of an entry's pattern
		struct Pass
		{
			size_t entry = 0;
			uint8_t pass = 0;		// 0-based, within the entry
			uint64_t start = 0;		// its first tick
			uint32_t ticks = 0;		// its length
			uint8_t pattern = 0;

			uint64_t end() const { return start + ticks; }
		};

		explicit PatternChain(MachineModel _model = MachineModel::Machinedrum) : m_commitTicks(commitTicks(_model)) {}

		// False, and the chain unchanged, for an entry out of range
		bool setEntries(std::vector<Entry> _entries);
		const std::vector<Entry>& getEntries() const { return m_entries; }

		// A pattern's length in steps, 1 to 64, from its dump; 0 forgets it
		void setLength(uint8_t _pattern, uint8_t _steps);
		std::optional<uint8_t> getLength(uint8_t _pattern) const;
		// At least one entry, and the length of every pattern in it known
		bool isPlayable() const;

		// One round of the whole chain, in ticks; 0 when not playable
		uint64_t roundTicks() const;
		// The pass playing at a tick, the chain looping; empty when not playable
		std::optional<Pass> passAt(uint64_t _tick) const;
		// The pass after _pass
		Pass next(const Pass& _pass) const;
		// The last tick of _pass at which the machine still takes a request for the next pass,
		// sent right after that tick's clock
		uint64_t lastRequestTick(const Pass& _pass) const { return _pass.end() - m_commitTicks - 1; }

		bool operator==(const PatternChain& _other) const
		{
			return m_commitTicks == _other.m_commitTicks && m_entries == _other.m_entries && m_lengths == _other.m_lengths;
		}
		bool operator!=(const PatternChain& _other) const { return !(*this == _other); }

	private:
		uint32_t passTicks(uint8_t _pattern) const { return m_lengths[_pattern] * TicksPerStep; }

		uint32_t m_commitTicks;
		std::vector<Entry> m_entries;
		std::array<uint8_t, 128> m_lengths{};
	};
}
