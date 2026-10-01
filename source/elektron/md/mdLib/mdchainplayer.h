#pragma once

#include "mdpatternchain.h"
#include "mdtypes.h"

#include "synthLib/midiTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace md
{
	// Plays a PatternChain on the Machinedrum from the transport the plug-in sends it: MIDI
	// clock, START, SONG POSITION and CONTINUE, STOP (synthLib::MidiClock), with the machine
	// following that transport (HostSync). The machine switches pattern by itself at the end of
	// the playing one, when the next was asked for in time (patternChainFirmwareTest):
	// - before START, and with SONG POSITION, it selects the pattern the chain plays at that
	//   position, unless START plays it already: selecting the pattern the machine has loads
	//   its Kit again, which drops the Kit's unsaved changes;
	// - it moves SONG POSITION into that pattern, as the machine takes it modulo its own length;
	// - on the first clock of each pass, or the first one after a start, it asks for the next
	//   pass's pattern when the machine would not play it anyway, up to the machine's commit.
	// A start in the last ticks of a pass, after the commit, lets the machine play that pattern
	// once more; the chain catches up at its next change of pattern.
	//
	// The device runs process() on every event it forwards to the machine (Device::sendMidi) and
	// observe() on what the machine sends (Device::readMidiOut), on its processing thread.
	// setChain() and getPlaying() may be called from any thread.
	class ChainPlayer
	{
	public:
		// The chain's pass while it plays
		struct Playing
		{
			size_t entry = 0;
			uint8_t pass = 0;
		};

		explicit ChainPlayer(MachineModel _model) : m_model(_model) {}

		// A chain to follow, or none. A chain that is not playable is not followed.
		void setChain(std::shared_ptr<const PatternChain> _chain);
		std::shared_ptr<const PatternChain> getChain() const;

		// Appends to _out what to forward instead of _event, in order
		void process(const synthLib::SMidiEvent& _event, std::vector<synthLib::SMidiEvent>& _out);
		// A message from the machine. Its CURRENT PATTERN status is, while stopped with nothing
		// asked for, the pattern START plays.
		void observe(const synthLib::SMidiEvent& _event);

		std::optional<Playing> getPlaying() const;

		// For tests, on the processing thread: the pattern START plays, when known
		std::optional<uint8_t> getStartPattern() const { return m_startPattern; }

	private:
		void select(uint8_t _pattern, const synthLib::SMidiEvent& _at, std::vector<synthLib::SMidiEvent>& _out);
		// Before START or CONTINUE from _tick: the chain's pattern there, selected if START would not play it
		std::optional<PatternChain::Pass> prepareStart(const PatternChain* _chain, uint64_t _tick,
			const synthLib::SMidiEvent& _at, std::vector<synthLib::SMidiEvent>& _out);
		void started(uint64_t _tick);
		void clock(const PatternChain* _chain, const synthLib::SMidiEvent& _event, std::vector<synthLib::SMidiEvent>& _out);
		// A pattern selected by someone else: SET STATUS (the pattern) or a Program Change (unknown)
		void selectedElsewhere(std::optional<uint8_t> _pattern);
		void publish(const std::optional<PatternChain::Pass>& _pass);

		const MachineModel m_model;
		std::shared_ptr<const PatternChain> m_chain;

		bool m_playing = false;
		uint64_t m_songPosition = 0;	// ticks, from the last SONG POSITION
		uint64_t m_nextTick = 0;		// the tick the next clock plays

		// What the machine plays: while stopped, the pattern START plays; while playing, the
		// pattern playing and the one asked for, which takes over at m_queuedFrom
		std::optional<uint8_t> m_startPattern;
		bool m_startFromQueue = false;	// START plays a pattern asked for before STOP, which the status does not show
		std::optional<uint8_t> m_current;
		std::optional<uint8_t> m_queued;
		uint64_t m_queuedFrom = 0;

		// The pass for getPlaying(): entry + 1 and pass, 0 when not playing
		std::atomic<uint32_t> m_published{0};
	};
}
