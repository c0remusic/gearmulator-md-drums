#pragma once

#include "mdDrumsMessages.h"

#include "jucePluginLib/controller.h"

#include <array>
#include <atomic>
#include <string>
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
		// The relays for Push 2 (ticket 11 of the editor map), after them: Track, Machine, Level, Mute, Solo, Out, LFO
		// shapes 1 and 2, SYN1-8, the Machinedrum's EFFECTS and ROUTING pages, on the Track shown
		static constexpr size_t RelayCount = 32;

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

		// Plays a Track from the editor, from any thread: its note goes to the Device at the next audio block, after the
		// parameter changes published before it (a machine just chosen sounds, not the one before)
		void audition(uint8_t _track, uint8_t _velocity);
		uint32_t pendingAuditions() const { return m_auditions.load(std::memory_order_relaxed); }	// bit n: Track n + 1
		uint8_t auditionVelocity(const uint8_t _track) const
		{
			return _track < TrackCount ? m_auditionVelocity[_track].load(std::memory_order_relaxed) : 0;
		}
		static constexpr uint8_t FirstNote = 36;

		// The Kit played (ticket 10 of the editor map), on the message thread: the one loaded last (its name, Links and
		// Chokes, its LFOs' running state), with the host parameters' values as they are now
		md::automation::sysex::MdKit playedKit() const;
		// The Bank's Slot it came from or was saved to (0-63), which the plug-in's state keeps
		int playedSlot() const { return m_slot; }
		void setPlayedSlot(int _slot);
		// Plays a Kit: one dump to the Device at the next audio block; the parameters follow as a preset's do (the host
		// told once, without gestures) and changes still waiting for the Device are dropped. _slot becomes the one played.
		void loadKit(const md::automation::sysex::MdKit& _kit, int _slot);
		// Names the Kit played, 16 characters at most (the Device keeps the name with its Kit)
		void renameKit(const std::string& _name);
		// The Kit played's Links and Chokes (ticket 29 of the editor map), on the message thread: the Track a Track's Hit
		// also strikes, and the one it silences, MdKit::Off for none. Values of the Kit, not host parameters: a change
		// goes to the Device as SET TRIG GROUP or SET MUTE GROUP ($65, $66) at the next audio block. A Track aimed at
		// itself does nothing on the Machinedrum, and is taken as Off.
		uint8_t link(const uint8_t _track) const { return _track < TrackCount ? m_kitBase.links[_track] : Off; }
		uint8_t choke(const uint8_t _track) const { return _track < TrackCount ? m_kitBase.chokes[_track] : Off; }
		void setLink(uint8_t _track, uint8_t _target);
		void setChoke(uint8_t _track, uint8_t _target);
		static constexpr uint8_t Off = md::automation::sysex::MdKit::Off;

		// The relays, on the message thread (the Controller's timer does it): the Track the relay Track asked for shown;
		// what a relay was set to given to its parameter of the Track shown; the relays set to what the Track shown holds.
		// The host is told without a gesture.
		void syncRelays();
		// For Live in Configure mode, which adds a parameter the plug-in moves: the 64 of the Push list in order (the
		// relays, then the master effects), each moved to its own value in a gesture
		void preparePushList();
		pluginLib::Parameter* relay(size_t _index) const { return _index < RelayCount ? m_relays[_index] : nullptr; }

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
		// Any thread: a relay set to _value, given to its parameter of the Track shown
		void relayChanged(uint8_t _index, int _value);
		// A SysEx message for the Device, at the next audio block
		void sendToDevice(const std::vector<uint8_t>& _sysex);

		std::array<Address, ParameterCount> m_addresses{};
		std::array<pluginLib::Parameter*, ParameterCount> m_parameters{};
		std::array<std::atomic<int32_t>, ParameterCount> m_values{};
		std::array<std::atomic<uint64_t>, (ParameterCount + 63) / 64> m_dirty{};
		std::atomic<uint32_t> m_auditions{0};	// bit n: Track n + 1 waits to be played
		std::array<std::atomic<uint8_t>, TrackCount> m_auditionVelocity{};
		std::vector<uint8_t> m_loadedState;
		md::automation::sysex::MdKit m_kitBase;
		int m_slot = 0;

		std::array<pluginLib::Parameter*, RelayCount> m_relays{};
		std::atomic<uint8_t> m_shownPart{0};
		std::atomic<int> m_pendingPart{-1};			// the Track the relay Track asked for, -1 none
		std::atomic<uint32_t> m_relayPending{0};		// bit n: relay n was set and its parameter waits for the value
		std::array<std::atomic<int>, RelayCount> m_relaySlot{};	// the slot of the parameter relay n was set for
		std::array<int, RelayCount> m_relayShown{};		// what each relay was last given (message thread)
	};
}
