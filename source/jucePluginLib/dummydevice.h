#pragma once

#include "synthLib/device.h"

namespace pluginLib
{
	class DummyDevice : public synthLib::Device
	{
	public:
		explicit DummyDevice(const synthLib::DeviceCreateParams& _params);

		float getSamplerate() const override { return 44100.0f; }
		bool isValid() const override { return true; }
#if !SYNTHLIB_DEMO_MODE
		// The state it was given, kept as it came: a project opened, then saved, while no device could start keeps its
		// sound, and the device made later receives it (synthLib::Plugin::setDevice hands it over)
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override
		{
			if(m_state.empty())
				return false;
			_state.insert(_state.end(), m_state.begin(), m_state.end());
			return true;
		}
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override
		{
			m_state = _state;
			return true;
		}
#endif
		uint32_t getChannelCountIn() override { return 2; }
		uint32_t getChannelCountOut() override { return 2; }

		bool setDspClockPercent(uint32_t _percent) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 100000000; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override {}
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override { return false; }

	private:
		std::vector<uint8_t> m_state;
	};
}
