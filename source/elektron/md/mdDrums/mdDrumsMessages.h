#pragma once

#include "mdProtocol/mdkit.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// MD Drums' own messages, for what the Machinedrum has no message for (ADR 0002): F0 7D 4D 44 44 <command> ... F7,
// under the non-commercial manufacturer id followed by "MDD"; and the host parameters as the Machinedrum's messages.
// Every message but a Kit dump fits SMidiEvent's 64 inline SysEx bytes.
namespace mdDrums::messages
{
	using Message = md::automation::sysex::Message;
	using MessageView = md::automation::sysex::MessageView;

	enum class Command : uint8_t
	{
		Solo = 0x01,		// track, 0 or 1
		Out = 0x02,			// track, 0 (Main) or 1 (its own Out)
		Tempo = 0x03,		// BPM x 100, three 7-bit bytes, high first
		Mixer = 0x04,		// the mute, solo and Out masks, three 7-bit bytes each, high first (bit n = track n + 1)
	};

	struct Mixer
	{
		uint16_t mute = 0;
		uint16_t solo = 0;
		uint16_t out = 0;

		bool operator==(const Mixer& _other) const
		{
			return mute == _other.mute && solo == _other.solo && out == _other.out;
		}
	};

	// A message built without allocating, for the audio thread: a CC in its first three bytes, or SysEx
	struct Raw
	{
		std::array<uint8_t, 16> bytes{};
		uint8_t size = 0;

		Message message() const { return Message(bytes.begin(), bytes.begin() + size); }
	};

	Raw solo(uint8_t _track, bool _on);
	Raw out(uint8_t _track, bool _on);
	Raw tempo(double _bpm);		// 30 to 300, as the engine takes it
	Raw mixer(const Mixer& _mixer);

	struct Parsed
	{
		Command command;
		uint8_t track = 0;
		bool on = false;
		double bpm = 0;
		Mixer mixer;
	};
	// Empty for anything but one of these messages, well formed
	std::optional<Parsed> parse(MessageView _message);

	// The Device's state: its Kit as a Kit dump ($52, slot 0), then its mixer message
	struct State
	{
		md::automation::sysex::MdKit kit;
		Mixer mixer;
	};
	std::vector<uint8_t> writeState(const State& _state);
	std::optional<State> parseState(const uint8_t* _data, size_t _size);

	// Kit 1 as a Machinedrum UW boots with OS 1.63's factory patch memory: the working Kit of the factory image (TRX UW)
	std::optional<md::automation::sysex::MdKit> factoryKit(const std::vector<uint8_t>& _flashImage);

	// The host parameters' pages (parameterDescriptions_mddrums.json): 0 to 4 are md::automation::machinedrum's, so
	// that a parameter's (page, track, index) is its CC
	namespace pages
	{
		constexpr uint8_t Synthesis = 0;	// SYN1-8
		constexpr uint8_t Effects = 1;		// AMD AMF EQF EQG FLTF FLTW FLTQ SRR
		constexpr uint8_t Routing = 2;		// DIST VOL PAN DEL REV LFOS LFOD LFOM
		constexpr uint8_t Level = 3;
		constexpr uint8_t Mute = 4;
		constexpr uint8_t Machine = 5;		// the machine id, 0 to 191
		constexpr uint8_t Lfo = 6;			// destination track, destination parameter, shape 1, shape 2, mode
		constexpr uint8_t Mixer = 7;		// Solo at index 0, Out at 1
		constexpr uint8_t Master = 8;		// echo 0-7, reverb 8-15, EQ 16-23, dynamix 24-31; on track 0
	}

	// What a host parameter sends the Device at _value, built without allocating: a CC, ASSIGN MACHINE, SET LFO PARAM,
	// a master effect message or one of MD Drums' own. Empty for a parameter MD Drums does not have, and for a machine
	// id the Machinedrum does not offer.
	std::optional<Raw> parameterMessage(uint8_t _page, uint8_t _track, uint8_t _index, int _value);
	// A host parameter's value in a Device state; empty for a parameter MD Drums does not have
	std::optional<int> parameterValue(const State& _state, uint8_t _page, uint8_t _track, uint8_t _index);
}
