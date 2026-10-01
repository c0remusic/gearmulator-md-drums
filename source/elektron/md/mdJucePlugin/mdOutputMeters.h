#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace mdJucePlugin
{
	// Levels of the plug-in's output channels (three stereo buses, A/B, C/D, E/F),
	// measured on the audio thread after each block and read by the editor's meters.
	// Lock-free: the audio thread raises a maximum, the reader takes and clears it.
	class OutputMeters
	{
	public:
		static constexpr size_t ChannelCount = 6;

		struct Level
		{
			float peak = 0.0f;     // highest absolute sample
			float rms = 0.0f;      // highest block RMS
		};

		// Audio thread: one block of one channel. Bounded, no allocation.
		void measure(const size_t _channel, const float* _samples, const int _count)
		{
			if(_channel >= ChannelCount || !_samples || _count <= 0)
				return;
			float peak = 0.0f;
			double squares = 0.0;
			for(int index = 0; index < _count; ++index)
			{
				const auto sample = _samples[index];
				peak = std::max(peak, std::abs(sample));
				squares += static_cast<double>(sample) * sample;
			}
			raise(m_peaks[_channel], peak);
			raise(m_rms[_channel], static_cast<float>(std::sqrt(squares / _count)));
		}

		// Reader: the highest levels since the last call, which clears them.
		Level take(const size_t _channel)
		{
			if(_channel >= ChannelCount)
				return {};
			return {m_peaks[_channel].exchange(0.0f, std::memory_order_acq_rel),
				m_rms[_channel].exchange(0.0f, std::memory_order_acq_rel)};
		}

	private:
		static void raise(std::atomic<float>& _value, const float _level)
		{
			// A few attempts at most: under contention a reading may be lost, never time on the audio thread
			auto current = _value.load(std::memory_order_relaxed);
			for(int attempt = 0; attempt < 4 && _level > current; ++attempt)
			{
				if(_value.compare_exchange_weak(current, _level, std::memory_order_acq_rel, std::memory_order_relaxed))
					return;
			}
		}

		std::array<std::atomic<float>, ChannelCount> m_peaks{};
		std::array<std::atomic<float>, ChannelCount> m_rms{};
	};
}
