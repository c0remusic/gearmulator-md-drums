#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace mdDrums
{
	// What the editor shows of the sound (ticket 16 of the editor map): the Device writes it on the audio thread, the
	// editor reads it on the message thread, without a lock or an allocation. The Processor owns it and hands it to the
	// Device it creates, so that it outlives any Device; a replaced or dummy Device leaves it as it was.
	class Telemetry
	{
	public:
		static constexpr int OutputCount = 18;	// Main left and right, then each Track's own output
		static constexpr int TrackCount = 16;

		// Audio thread: an output's peak over the samples just rendered (full scale 1.0)
		void raisePeak(const int _output, const float _peak)
		{
			auto& slot = m_peaks[static_cast<size_t>(_output)];
			auto current = slot.load(std::memory_order_relaxed);
			// A reader taking the value meanwhile only makes this one land in its next read
			for(int attempt = 0; attempt < 4 && _peak > current; ++attempt)
				if(slot.compare_exchange_weak(current, _peak, std::memory_order_acq_rel, std::memory_order_relaxed))
					return;
		}

		// Audio thread: a Track struck (its own Hit or a Link's)
		void hit(const int _track)
		{
			m_hits[static_cast<size_t>(_track)].fetch_add(1, std::memory_order_release);
		}

		// Editor: the highest peak since the last take, which starts again from 0
		float takePeak(const int _output)
		{
			return m_peaks[static_cast<size_t>(_output)].exchange(0.0f, std::memory_order_acq_rel);
		}

		// Editor: how many times a Track was struck so far; a change is a new Hit
		uint32_t hits(const int _track) const
		{
			return m_hits[static_cast<size_t>(_track)].load(std::memory_order_acquire);
		}

	private:
		std::array<std::atomic<float>, OutputCount> m_peaks{};
		std::array<std::atomic<uint32_t>, TrackCount> m_hits{};
	};
}
