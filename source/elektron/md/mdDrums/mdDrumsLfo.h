#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace mdDrums
{
	// One Track's LFO as OS 1.63 runs it, ported from its routines: the oscillator ($1000088), the waveform routine
	// ($204c94) and its six shapes, and the apply ($1000332, and $10001e8 at a Hit). mdDrumsScreensTest proves it bit-exact
	// against those routines running in the engine. The words are the OS's: a parameter as its smoothed 14-bit word
	// (value << 7), a shape's output within +-$4000, the phase from 0 to $7fff, which the oscillator moves once a tick.
	struct Lfo
	{
		enum Shape : uint8_t { Triangle, Saw, Square, Ramp, Exponential, Random, ShapeCount };
		// The type's bits: TRIG restarts the phase at a Hit of the LFO's Track; HOLD changes the output at its Hits only
		static constexpr uint8_t Trig = 1, Hold = 2;
		static constexpr int32_t FullScale = 0x4000;
		static constexpr int32_t PhaseCount = 0x8000;
		static constexpr uint16_t MaxWord = 0x3fff;

		// The running part of the OS's 36-byte LFO struct
		struct State
		{
			int32_t phase = 0;					// +$20
			std::array<int32_t, 2> current{};	// +8, +$c: each shape's output
			std::array<int32_t, 2> held{};		// +$10, +$14: what the apply reads
			std::array<int32_t, 2> random{};	// +$18, +$1c: RND's generator, which both shapes share
			std::array<int8_t, 2> eighth{};		// +6, +7: the eighth of the period each RND shape drew in
			bool trigger = false;				// +5: a Hit waits for the waveform routine
		};

		uint8_t shape1 = Triangle;
		uint8_t shape2 = Triangle;
		uint8_t type = 0;

		// The phase step of a tick for LFOS's word _speed, the tempo _tempo being BPM x 24
		static int32_t increment(uint16_t _speed, int32_t _tempo);
		// The waveform routine: TRIG's restart at a Hit, both shapes, and the held outputs (at a Hit, else unless HOLD)
		void wave(State& _s) const;
		// The oscillator's work for this LFO, once a tick: the phase, the waveform routine, then RMP's and EXP's fall
		void tick(State& _s, int32_t _increment) const;
		// The held shapes mixed by LFOM's word: shape 1 at 0, shape 2 inverted at $3f80
		static int32_t mix(const State& _s, uint16_t _mix);
		// The destination's word _value moved by _lfo at LFOD's word _depth, within 0 to $3fff
		static uint16_t apply(uint16_t _value, int32_t _lfo, uint16_t _depth);
		// A parameter's smoothed word once its smoothing has settled from below (from above it settles on value << 7)
		static uint16_t settled(int _value);

		// What the LFO screen draws (Design/HANDOFF.md, "LFO"): the destination's words, one a tick, from a Hit of the
		// LFO's Track at tick 0, running free after it (HOLD would hold the Hit's value: the screen shows what the Hits
		// sample). _speed, _depth, _mix and _value are 0-127; _ticks the window. _seed gives RND a generator: the engine's
		// own is the Kit's.
		struct Trace
		{
			std::vector<uint16_t> words;
			int32_t increment = 0;
			uint16_t lowest = 0;
			uint16_t highest = 0;
		};
		Trace trace(int _speed, int _depth, int _mix, int _value, double _bpm, int _ticks, uint32_t _seed = 0x2c1b3c6d) const;
		// The window the screen shows, in ticks: 1 to 6 periods as LFOS rises (the mockup's), and the whole fall of a RMP
		// or an EXP that LFOM lets through (5 periods for RMP, which falls for 4; 8 for EXP, whose time constant is 2)
		int window(int _speed, int _mix, double _bpm) const;

		static constexpr double TickRate = 44100.0 / (32.0 * 11.0);	// mdEngine's tick: every 11 blocks of 32 samples
	};
}
