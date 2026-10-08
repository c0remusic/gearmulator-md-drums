#include "mdDrumsLfo.h"

#include <algorithm>
#include <cmath>

namespace mdDrums
{
	namespace
	{
		// The 68k's 32-bit multiply, which keeps the product's low long
		int32_t muls(const int32_t _a, const int32_t _b)
		{
			return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(_a) * _b));
		}

		// One shape for one of the two slots ($2523ee's table: 6 and 7 are the triangle again); _hit is the struct's
		// trigger flag as the waveform routine read it
		void shape(Lfo::State& _s, const uint8_t _shape, const int _slot, const bool _hit, const uint8_t _type)
		{
			const auto p = _s.phase;
			auto& out = _s.current[static_cast<size_t>(_slot)];
			switch(_shape)
			{
			case Lfo::Saw:			// $204e0a: from +1 to -1 twice a period
				out = (p <= 0x3fff ? 0x4000 : 0xc000) - 2 * p;
				return;
			case Lfo::Square:		// $204e7a
				out = p <= 0x3fff ? 0x4000 : -0x4000;
				return;
			case Lfo::Ramp:			// $204eda, $204f06: full at a Hit; the oscillator makes them fall
			case Lfo::Exponential:
				if(_hit)
					out = 0x4000;
				return;
			case Lfo::Random:		// $204f32: a new value each eighth of the period, and at a Hit with TRIG
				{
					const auto eighth = static_cast<int8_t>(p >> 12);
					auto& drawn = _s.eighth[static_cast<size_t>(_slot)];
					if(!((_type & Lfo::Trig) && _hit) && drawn == eighth)
						return;
					drawn = eighth;
					const auto next = static_cast<int32_t>(static_cast<uint32_t>(_s.random[0]) + static_cast<uint32_t>(_s.random[1]));
					out = (next & 0x3fff) - 0x2000;
					_s.random[1] = _s.random[0];
					_s.random[0] = next;
					return;
				}
			default:				// $204d78, the triangle: 0 up to +1, down to -1, back to 0
				out = p <= 0x1fff ? 2 * p : p <= 0x5fff ? 0x8000 - 2 * p : 2 * p - 0x10000;
				return;
			}
		}

		// The oscillator's fall of a RMP or an EXP, after the waveform routine
		void fall(int32_t& _out, const uint8_t _shape, const int32_t _increment)
		{
			if(!_out)
				return;
			int32_t next;
			if(_shape == Lfo::Ramp)
				next = _out - (_increment >> 3);
			else if(_shape == Lfo::Exponential)
				next = muls(0x8000 - (_increment >> 1), _out) >> 15;
			else
				return;
			_out = next < 0 ? 0 : next;
		}
	}

	int32_t Lfo::increment(const uint16_t _speed, const int32_t _tempo)
	{
		const int32_t v = _speed;
		if(v <= 0x1fff)
			return (muls((v << 4) + 16, _tempo) + 0x3c8fe) / 0x791fd;
		// Above, a quartic
		auto d = muls(v, v) >> 13;
		d = muls(d, v);
		if(d < 0)
			d += 0x1ffff;
		d = muls(d >> 17, v) >> 10;
		return muls(d, _tempo) / 0x4099;
	}

	void Lfo::wave(State& _s) const
	{
		const bool hit = _s.trigger;
		if((type & Trig) && hit)
			_s.phase = 0;
		if(shape1 <= 7)
			shape(_s, shape1, 0, hit, type);
		if(shape2 <= 7)
			shape(_s, shape2, 1, hit, type);
		if(hit)
		{
			_s.held = _s.current;
			_s.trigger = false;
		}
		else if(!(type & Hold))
		{
			_s.held = _s.current;
		}
	}

	void Lfo::tick(State& _s, const int32_t _increment) const
	{
		_s.phase = (_s.phase + _increment) & (PhaseCount - 1);
		wave(_s);
		fall(_s.current[0], shape1, _increment);
		fall(_s.current[1], shape2, _increment);
	}

	int32_t Lfo::mix(const State& _s, const uint16_t _mix)
	{
		const int32_t m = _mix;
		return (muls(0x3f80 - m, _s.held[0]) - muls(m, _s.held[1])) >> 14;
	}

	uint16_t Lfo::apply(const uint16_t _value, const int32_t _lfo, const uint16_t _depth)
	{
		const auto moved = (muls(_lfo, _depth) >> 14) + _value;
		return static_cast<uint16_t>(std::clamp(moved, 0, static_cast<int32_t>(MaxWord)));
	}

	uint16_t Lfo::settled(const int _value)
	{
		const auto word = std::clamp(_value, 0, 127) << 7;
		return static_cast<uint16_t>(word ? word - 3 : 0);
	}

	Lfo::Trace Lfo::trace(const int _speed, const int _depth, const int _mix, const int _value, const double _bpm,
		const int _ticks, const uint32_t _seed) const
	{
		Trace result;
		const Lfo free{shape1, shape2, static_cast<uint8_t>(type & ~Hold)};
		const auto tempo = static_cast<int32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0));
		result.increment = increment(settled(_speed), tempo);
		const auto depth = settled(_depth), mixWord = settled(_mix), value = settled(_value);

		State s;
		s.random = {static_cast<int32_t>(_seed), static_cast<int32_t>(_seed * 0x9e3779b9u + 1u)};
		s.eighth = {-1, -1};	// so that RND draws its first value at once
		s.trigger = true;
		const auto count = std::max(1, _ticks);
		result.words.reserve(static_cast<size_t>(count));
		// The Hit: the waveform routine and the apply at once ($10001e8), then a tick at a time
		free.wave(s);
		result.words.push_back(apply(value, mix(s, mixWord), depth));
		for(int i = 1; i < count; ++i)
		{
			free.tick(s, result.increment);
			result.words.push_back(apply(value, mix(s, mixWord), depth));
		}
		const auto [lowest, highest] = std::minmax_element(result.words.begin(), result.words.end());
		result.lowest = *lowest;
		result.highest = *highest;
		return result;
	}

	int Lfo::window(const int _speed, const int _mix, const double _bpm) const
	{
		const auto tempo = static_cast<int32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 24.0));
		const auto step = increment(settled(_speed), tempo);
		if(step <= 0)
			return static_cast<int>(TickRate * 2.0);	// a phase that does not move: two seconds of a still wave
		auto periods = 1.0 + std::clamp(_speed, 0, 127) * 5.0 / 127.0;
		const auto uses = [&](const uint8_t _shape)
		{
			return (shape1 == _shape && _mix < 127) || (shape2 == _shape && _mix > 0);
		};
		if(uses(Ramp))
			periods = std::max(periods, 5.0);	// it falls for 4, then rests
		if(uses(Exponential))
			periods = std::max(periods, 8.0);	// 4 of its time constants: under 2 % left
		return std::max(2, static_cast<int>(std::ceil(periods * PhaseCount / step)));
	}
}
