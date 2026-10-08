#pragma once

#include "mdDrumsMessages.h"

#include "jucePluginLib/controller.h"

#include <array>
#include <atomic>
#include <vector>

namespace mdDrums
{
	class Processor;

	// MD Drums' host parameters (parameterDescriptions_mddrums.json) over its Device. A change from the host, the
	// editor or MIDI Learn is published to the parameter's atomic slot, from any thread, and sent as the Machinedrum's
	// message (messages::parameterMessage) at the start of the next audio block, without locking or allocating. What
	// the Device changes itself, a new machine's SYN1-8, and a loaded state come back as parameter values without
	// echoing to the Device.
	class Controller final : public pluginLib::Controller
	{
	public:
		static constexpr uint8_t TrackCount = 16;
		static constexpr size_t TrackParameterCount = 34;	// Machine, 24 parameters, 5 LFO, Level, Mute, Solo, Out
		static constexpr size_t MasterParameterCount = 32;
		static constexpr size_t ParameterCount = TrackCount * TrackParameterCount + MasterParameterCount;

		explicit Controller(Processor& _processor);
		~Controller() override;

		uint8_t getPartCount() const override { return TrackCount; }

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value,
			pluginLib::Parameter::Origin _origin) override;
		bool parseSysexMessage(const pluginLib::SysEx& _message, synthLib::MidiEventSource _source) override;
		bool parseControllerMessage(const synthLib::SMidiEvent& _event) override;
		void processRealtimeParameterChanges(size_t _maximum) override;
		void onStateLoaded() override;

		// The Device's state as the plug-in's state holds it (synthLib::Plugin's two header bytes, then the Device's):
		// the Processor hands it over as it loads a state, so that the parameters follow without touching the Device
		void setLoadedState(std::vector<uint8_t> _state);

		// Where a host parameter's value waits to be sent: its slot, or -1 for none
		static int slotOf(uint8_t _page, uint8_t _part, uint8_t _index);

	protected:
		pluginLib::Parameter* createParameter(pluginLib::Controller& _controller, const pluginLib::Description& _description,
			uint8_t _part, int _uid, const pluginLib::Parameter::PartFormatter& _formatter) override;
		// On the message thread: what host automation published without touching juce::Value, for the editor
		void onControllerTimer() override;

	private:
		struct Address
		{
			uint8_t page = 0;
			uint8_t part = 0;
			uint8_t index = 0;
		};

		void applyState(const messages::State& _state);
		bool applyStateBytes(const std::vector<uint8_t>& _state);

		std::array<Address, ParameterCount> m_addresses{};
		std::array<std::atomic<int32_t>, ParameterCount> m_values{};
		std::array<std::atomic<uint64_t>, (ParameterCount + 63) / 64> m_dirty{};
		std::vector<uint8_t> m_loadedState;
	};
}
