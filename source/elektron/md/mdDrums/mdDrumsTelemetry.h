#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mdDrums
{
	// What the editor shows of the sound (ticket 16 of the editor map): the Device writes it on the audio thread, the
	// editor reads it on the message thread, without a lock or an allocation. The Processor owns it and shares it with
	// every Device it creates, so that it outlives any Device; a replaced or dummy Device leaves it as it was.
	class Telemetry
	{
	public:
		static constexpr int OutputCount = 18;	// Main left and right, then each Track's own output
		static constexpr int TrackCount = 16;

		// A Hit's capture (ticket 21): the Track's own output from the sample the Hit sounds, as min/max columns of 8
		// samples (full scale 32767), up to 0.8 s at 44.1 kHz, or until the Track's next Hit
		static constexpr int ColumnSamples = 8;
		static constexpr int CaptureColumns = 4410;
		using Column = std::array<int16_t, 2>;	// min, max

		// A sample (full scale 1.0) as a column holds it
		static int16_t level(const float _sample)
		{
			return static_cast<int16_t>(std::clamp(std::lround(_sample * 32767.0f), -32767L, 32767L));
		}

		struct Capture
		{
			uint32_t id = 0;		// the Hit's number on its Track, from 1; 0: none
			uint8_t velocity = 0;
			int count = 0;			// columns captured so far
			std::vector<Column> columns = std::vector<Column>(CaptureColumns);
		};

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

		// Audio thread: a new capture for a struck Track, in the slot the previous one does not use
		void beginCapture(const int _track, const uint8_t _velocity)
		{
			auto& track = m_captures[static_cast<size_t>(_track)];
			const auto current = track.current.load(std::memory_order_relaxed);
			const auto slotIndex = current < 0 ? 0 : 1 - current;
			auto& slot = track.slots[static_cast<size_t>(slotIndex)];
			// A reader of this slot sees its id change and drops what it copied
			slot.id.store(0, std::memory_order_release);
			slot.count.store(0, std::memory_order_relaxed);
			slot.velocity.store(_velocity, std::memory_order_relaxed);
			slot.id.store(++track.ids, std::memory_order_release);
			track.current.store(slotIndex, std::memory_order_release);
		}

		// Audio thread: the current capture's next column; false once it is full
		bool addColumn(const int _track, const int16_t _min, const int16_t _max)
		{
			auto& track = m_captures[static_cast<size_t>(_track)];
			const auto current = track.current.load(std::memory_order_relaxed);
			if(current < 0)
				return false;
			auto& slot = track.slots[static_cast<size_t>(current)];
			const auto count = slot.count.load(std::memory_order_relaxed);
			if(count >= CaptureColumns)
				return false;
			slot.columns[static_cast<size_t>(count)] = {_min, _max};
			slot.count.store(count + 1, std::memory_order_release);
			return count + 1 < CaptureColumns;
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

		// Editor: a copy of a Track's latest capture (_previous: the one before it) into _out, its columns up to what was
		// captured. Only the columns past _out's own count are copied when it holds the same Hit. False when there is
		// none, or when the Device reused the slot while it was being read (the next read gets it).
		bool readCapture(const int _track, const bool _previous, Capture& _out) const
		{
			const auto& track = m_captures[static_cast<size_t>(_track)];
			const auto current = track.current.load(std::memory_order_acquire);
			if(current < 0)
				return false;
			const auto& slot = track.slots[static_cast<size_t>(_previous ? 1 - current : current)];
			const auto id = slot.id.load(std::memory_order_acquire);
			if(id == 0)
				return false;
			const auto count = slot.count.load(std::memory_order_acquire);
			const auto from = _out.id == id ? std::min(_out.count, count) : 0;
			for(int c = from; c < count; ++c)
				_out.columns[static_cast<size_t>(c)] = slot.columns[static_cast<size_t>(c)];
			const auto velocity = slot.velocity.load(std::memory_order_relaxed);
			if(slot.id.load(std::memory_order_acquire) != id)
				return false;
			_out.id = id;
			_out.count = count;
			_out.velocity = velocity;
			return true;
		}

	private:
		struct Slot
		{
			std::atomic<uint32_t> id{0};
			std::atomic<int> count{0};
			std::atomic<uint8_t> velocity{0};
			std::array<Column, CaptureColumns> columns{};
		};
		struct TrackCaptures
		{
			std::array<Slot, 2> slots;
			std::atomic<int> current{-1};
			uint32_t ids = 0;	// the audio thread's own
		};

		std::array<std::atomic<float>, OutputCount> m_peaks{};
		std::array<std::atomic<uint32_t>, TrackCount> m_hits{};
		std::array<TrackCaptures, TrackCount> m_captures{};
	};
}
