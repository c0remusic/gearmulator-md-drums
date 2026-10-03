#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

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

	// The Kit the machine plays, as its RAM holds it (Hardware::readLiveKit): what a parameter's CC,
	// ASSIGN MACHINE ($5B) and the front panel change at once, where a Kit request answers with the Kit
	// as stored. Values are indexed like the host parameters (md::automation): page * 8 + index, the
	// level at index 0 of its own page (machinedrum::Level, monomachine::Level).
	struct LiveKit
	{
		static constexpr uint8_t MaxTracks = 16;
		using TrackValues = std::array<uint8_t, 64>;

		uint64_t frame = 0;		// the machine's emulated frame when it was read (Hardware::getEmulatedFrames)
		uint8_t tracks = 0;
		std::array<uint8_t, MaxTracks> machines{};	// md::machines id per track
		std::array<TrackValues, MaxTracks> values{};
		// The Machinedrum's only: each track's LFO, and the master effects as the Kit dump lays them out
		// (reverb, echo, EQ, dynamix, 8 values each)
		bool machinedrum = false;
		std::array<LfoSettings, MaxTracks> lfos{};
		std::array<uint8_t, 32> masterEffects{};

		uint8_t value(const uint8_t _track, const uint8_t _page, const uint8_t _index) const
		{
			return values[_track][_page * 8u + _index];
		}

		// Whether a track holds the same machine, and the same values on its first _pages pages, in both
		bool sameTrack(const LiveKit& _other, const uint8_t _track, const uint8_t _pages) const
		{
			const auto& a = values[_track];
			const auto& b = _other.values[_track];
			return machines[_track] == _other.machines[_track] && std::equal(a.begin(), a.begin() + _pages * 8, b.begin());
		}
	};

	// The last LiveKit from the Device's rendering thread, for any other thread. The rendering thread
	// never waits: it skips a kit while a reader copies the previous one.
	class LiveKitSnapshot
	{
	public:
		void publish(const LiveKit& _kit)
		{
			const std::unique_lock lock(m_mutex, std::try_to_lock);
			if(lock.owns_lock())
				m_kit = _kit;
		}

		// None before the machine's first kit
		std::optional<LiveKit> read() const
		{
			const std::lock_guard lock(m_mutex);
			return m_kit;
		}

	private:
		mutable std::mutex m_mutex;
		std::optional<LiveKit> m_kit;
	};
}
