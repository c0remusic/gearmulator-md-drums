// Every output channel synthLib carries goes through Plugin and the resampler
// together: an impulse rendered on all of them at a note's native sample must
// come out on the same host sample everywhere, where the reported latency says.

#include "synthLib/device.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace synthLib;

	constexpr uint32_t Channels = MaxAudioOutputs;
	constexpr float DeviceRate = 44100.0f;

	float amplitude(const size_t _channel)
	{
		return 0.5f + 0.025f * static_cast<float>(_channel);
	}

	class ImpulseDevice final : public Device
	{
	public:
		ImpulseDevice() : Device({}) {}

		float getSamplerate() const override { return DeviceRate; }
		bool isValid() const override { return true; }
#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>&, StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, StateType) override { return false; }
#endif
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return Channels; }
		uint32_t getDefaultLatencyBlocks() const override { return 0; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 100000000; }

		// The base class hands every event over before rendering the block;
		// an impulse needs the event's native offset
		void process(const TAudioInputs&, const TAudioOutputs& _outputs, const size_t _size,
			const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut) override
		{
			_midiOut.clear();
			for(uint32_t c = 0; c < Channels; ++c)
				std::fill_n(_outputs[c], _size, 0.0f);
			for(const auto& event : _midiIn)
			{
				if((event.a & 0xf0) != M_NOTEON || event.offset >= _size)
					continue;
				for(uint32_t c = 0; c < Channels; ++c)
					_outputs[c][event.offset] = amplitude(c);
			}
		}

	private:
		void readMidiOut(std::vector<SMidiEvent>&) override {}
		bool sendMidi(const SMidiEvent&, std::vector<SMidiEvent>&) override { return true; }
		void processAudio(const TAudioInputs&, const TAudioOutputs&, size_t) override {}
	};

	const char* modeName(const Resampler::Mode _mode)
	{
		switch(_mode)
		{
		case Resampler::Mode::Legacy:	return "Legacy";
		case Resampler::Mode::MameHq:	return "MameHq";
		case Resampler::Mode::MameLofi:	return "MameLofi";
		default:						return "?";
		}
	}

	void impulses(const float _rate, const Resampler::Mode _mode, const bool _variable)
	{
		ImpulseDevice device;
		Plugin plugin(&device, [](Device*) {});
		plugin.setHostSamplerate(_rate, DeviceRate);
		plugin.setResamplerMode(_mode);
		plugin.setBlockSize(512);

		const auto reported = plugin.getLatencyMidiToOutput();

		constexpr std::array<uint32_t, 4> positions{1003, 8009, 16007, 32003};
		constexpr std::array<uint32_t, 10> blocks{0, 1, 7, 31, 63, 64, 127, 257, 511, 512};
		constexpr uint32_t Length = 36000;

		std::array<std::array<float, 512>, Channels> block{};
		std::array<std::vector<float>, Channels> recorded;
		TAudioInputs inputs{};
		TAudioOutputs outputs{};
		for(uint32_t c = 0; c < Channels; ++c)
		{
			outputs[c] = block[c].data();
			recorded[c].reserve(Length + 512);
		}

		for(uint32_t host = 0, index = 0; host < Length; ++index)
		{
			const auto size = _variable ? blocks[index % blocks.size()] : 512u;
			for(const auto position : positions)
				if(position >= host && position < host + size)
					plugin.addMidiEvent(SMidiEvent(MidiEventSource::Host, M_NOTEON, 36, 100, position - host));
			plugin.process(inputs, outputs, size, 120.0, 0.0, false);
			for(uint32_t c = 0; c < Channels; ++c)
				recorded[c].insert(recorded[c].end(), block[c].begin(), block[c].begin() + size);
			host += size;
		}

		// A host note lands on the next native sample, which can come out up to
		// one host sample late per native sample when the host runs faster; the
		// filter's delay is fractional and reported rounded up, so the peak's
		// nearest sample can also come one early
		const auto late = static_cast<long>(std::ceil(_rate / DeviceRate));
		constexpr long early = 1;

		std::string peaks;
		for(const auto position : positions)
		{
			long first = -1;
			for(uint32_t c = 0; c < Channels; ++c)
			{
				const auto& signal = recorded[c];
				const auto begin = signal.begin() + position;
				const auto found = std::max_element(begin, begin + 2000,
					[](const float _a, const float _b) { return std::abs(_a) < std::abs(_b); });
				if(std::abs(*found) < 0.25f * amplitude(c))
					throw std::runtime_error("channel " + std::to_string(c + 1) + " lost an impulse");
				const auto delay = static_cast<long>(std::distance(begin, found));
				if(first < 0)
					first = delay;
				else if(delay != first)
					throw std::runtime_error("channel " + std::to_string(c + 1) + " came out "
						+ std::to_string(delay - first) + " samples away from channel 1");
			}
			peaks += " " + std::to_string(first);
			const auto error = first - static_cast<long>(reported);
			if(error < -early || error > late)
				throw std::runtime_error("impulse at " + std::to_string(first) + " samples, reported latency "
					+ std::to_string(reported));
		}

		std::printf("rate=%g mode=%s variable=%d reported=%u peaks=%s passed\n",
			_rate, modeName(_mode), _variable ? 1 : 0, reported, peaks.c_str());
	}

	// Time to resample ten seconds of host audio on every channel, the device rendering silence
	void cost(const float _rate, const Resampler::Mode _mode)
	{
		ImpulseDevice device;
		Plugin plugin(&device, [](Device*) {});
		plugin.setHostSamplerate(_rate, DeviceRate);
		plugin.setResamplerMode(_mode);
		plugin.setBlockSize(512);

		std::array<std::array<float, 512>, Channels> block{};
		TAudioInputs inputs{};
		TAudioOutputs outputs{};
		for(uint32_t c = 0; c < Channels; ++c)
			outputs[c] = block[c].data();

		const auto seconds = 10u;
		const auto blocks = static_cast<uint32_t>(_rate) * seconds / 512;
		const auto start = std::chrono::steady_clock::now();
		for(uint32_t i = 0; i < blocks; ++i)
			plugin.process(inputs, outputs, 512, 120.0, 0.0, false);
		const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

		std::printf("cost rate=%g mode=%s channels=%u: %.1f ms per second of audio (%.2f %% of one core)\n",
			_rate, modeName(_mode), Channels, elapsed / seconds, elapsed / seconds / 10.0);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	bool failed = false;
	for(const auto mode : {Resampler::Mode::Legacy, Resampler::Mode::MameHq, Resampler::Mode::MameLofi})
		for(const float rate : {44100.0f, 48000.0f, 88200.0f, 96000.0f})
			for(const bool variable : {false, true})
			{
				try { impulses(rate, mode, variable); }
				catch(const std::exception& _error)
				{
					std::printf("rate=%g mode=%s variable=%d: %s\n", rate, modeName(mode), variable ? 1 : 0,
						_error.what());
					failed = true;
				}
			}

	for(const float rate : {44100.0f, 48000.0f, 96000.0f})
		for(const auto mode : {Resampler::Mode::Legacy, Resampler::Mode::MameHq})
			cost(rate, mode);

	std::printf("synthLibOutputChannelsTest: %s\n", failed ? "FAIL" : "PASS");
	return failed ? 1 : 0;
}
