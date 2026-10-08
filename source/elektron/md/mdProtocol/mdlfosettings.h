#pragma once

#include <cstdint>

namespace md
{
	// A Machinedrum track's LFO as its Kit holds it, the fields SET LFO PARAM ($62) sets: the track and the
	// parameter it modulates (0 to 15, 0 to 23), its two shapes and how a trig restarts it. Speed, depth and
	// the mix of the shapes are the track's parameters LFOS, LFOD and LFOM.
	struct LfoSettings
	{
		// What mdPlayheadProbe --lfo traced on the firmware
		enum Shape : uint8_t { Triangle, Saw, Square, Ramp, Exponential, Random, ShapeCount };	// ramp and exponential fall once
		enum Update : uint8_t { Free, Trig, Hold, UpdateCount };

		uint8_t track = 0;
		uint8_t parameter = 0;
		uint8_t shape1 = 0;
		uint8_t shape2 = 0;
		uint8_t update = 0;

		bool operator==(const LfoSettings& _other) const
		{
			return track == _other.track && parameter == _other.parameter && shape1 == _other.shape1
				&& shape2 == _other.shape2 && update == _other.update;
		}
		bool operator!=(const LfoSettings& _other) const { return !(*this == _other); }
	};
}
