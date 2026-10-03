#include "mdLib/mdautomation.h"
#include "mdLib/mdautomationsync.h"
#include "mdLib/mdhostsync.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdmmpatternwriter.h"
#include "mdLib/mdsysexautomation.h"
#include "synthLib/midiBufferParser.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(_condition)
			return;
		std::cerr << "automationMidiTest: " << _message << '\n';
		std::exit(1);
	}

	void expectMessage(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel,
		const md::automation::ControlChange& _expected)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		require(*encoded == _expected, "parameter encoded to the wrong CC");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void expectRoundTrip(const md::MachineModel _model,
		const md::automation::ParameterChange& _change, const uint8_t _baseChannel)
	{
		const auto encoded = md::automation::encodeParameterChange(_model, _change,
			_baseChannel);
		require(encoded.has_value(), "valid parameter did not encode");
		const auto decoded = md::automation::decodeParameterChange(_model, *encoded,
			_baseChannel);
		require(decoded.has_value(), "encoded CC did not decode");
		require(*decoded == _change, "CC round trip changed its parameter address");
	}

	void testMachinedrum()
	{
		using namespace md::automation;
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 0, 0, 23}, 0, {0xb0, 16, 23});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Effects, 0, 0, 31}, 0, {0xb0, 24, 31});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Routing, 3, 7, 127}, 0, {0xb0, 119, 127});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 4, 0, 64}, 0, {0xb1, 16, 64});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Level, 15, 0, 99}, 0, {0xb3, 11, 99});
		expectMessage(md::MachineModel::Machinedrum,
			{machinedrum::Mute, 9, 0, 1}, 4, {0xb6, 13, 1});

		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t page = machinedrum::Synthesis;
				page <= machinedrum::Routing; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
					expectRoundTrip(md::MachineModel::Machinedrum,
						{page, track, index, static_cast<uint8_t>(track + index)}, 0);
			}

			for(const auto page : {machinedrum::Level, machinedrum::Mute})
			{
				const ParameterChange change{page, track, 0,
					static_cast<uint8_t>(page == machinedrum::Mute ? 1 : track)};
				expectRoundTrip(md::MachineModel::Machinedrum, change, 0);
			}
		}

		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 16, 0, 0}, 0), "accepted MD track 17");
		require(!encodeParameterChange(md::MachineModel::Machinedrum,
			{machinedrum::Synthesis, 15, 0, 0}, 13), "accepted overflowing MD channel span");
	}

	void testMonomachine()
	{
		using namespace md::automation;
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 0, 0, 12}, 0, {0xb0, 48, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Amplification, 0, 0, 12}, 0, {0xb0, 56, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Filter, 0, 0, 12}, 0, {0xb0, 72, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Effects, 0, 3, 12}, 0, {0xb0, 83, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Effects, 0, 4, 12}, 0, {0xb0, 84, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo1, 0, 0, 12}, 0, {0xb0, 88, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo2, 0, 0, 12}, 0, {0xb0, 104, 12});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Lfo3, 5, 7, 126}, 2, {0xb7, 119, 126});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Level, 2, 0, 77}, 0, {0xb2, 7, 77});
		expectMessage(md::MachineModel::Monomachine,
			{monomachine::Mute, 4, 0, 1}, 0, {0xb4, 3, 1});

		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			for(uint8_t page = monomachine::Synthesis;
				page <= monomachine::Lfo3; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
				{
					const ParameterChange change{page, track, index,
						static_cast<uint8_t>(page * 8 + index)};
					expectRoundTrip(md::MachineModel::Monomachine, change, 0);
				}
			}

			for(const auto page : {monomachine::Level, monomachine::Mute})
			{
				const ParameterChange change{page, track, 0,
					static_cast<uint8_t>(page == monomachine::Mute ? 1 : track)};
				expectRoundTrip(md::MachineModel::Monomachine, change, 0);
			}
		}

		require(!encodeParameterChange(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 6, 0, 0}, 0), "accepted MM track 7");
		require(!encodeParameterChange(md::MachineModel::Monomachine,
			{monomachine::Synthesis, 5, 0, 0}, 11), "accepted overflowing MM channel span");
	}

	void finishDump(md::automation::sysex::Message& _message)
	{
		uint32_t checksum = 0;
		for(size_t i = 9; i < _message.size(); ++i)
			checksum += _message[i];
		checksum &= 0x3fff;
		const auto finalSize = _message.size() + 5;
		const auto length = static_cast<uint16_t>(finalSize - 10);
		_message.push_back(static_cast<uint8_t>(checksum >> 7));
		_message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		_message.push_back(static_cast<uint8_t>(length >> 7));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	std::vector<uint8_t> rleEncode(const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> result;
		for(size_t position = 0; position < _decoded.size();)
		{
			const auto value = _decoded[position];
			size_t count = 1;
			while(position + count < _decoded.size()
				&& _decoded[position + count] == value && count < 127)
				++count;
			if(value >= 0x80 || count > 1)
			{
				result.push_back(static_cast<uint8_t>(0x80 | count));
				result.push_back(value);
			}
			else
				result.push_back(value);
			position += count;
		}
		return result;
	}

	std::vector<uint8_t> pack7Bit(const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> result;
		for(size_t position = 0; position < _decoded.size();)
		{
			const auto headerPosition = result.size();
			result.push_back(0);
			for(uint8_t bit = 0; bit < 7 && position < _decoded.size(); ++bit)
			{
				const auto value = _decoded[position++];
				if(value & 0x80)
					result[headerPosition] |= static_cast<uint8_t>(1u << (6u - bit));
				result.push_back(static_cast<uint8_t>(value & 0x7f));
			}
		}
		return result;
	}

	md::automation::sysex::Message makeMmDump(const uint8_t _command,
		const std::vector<uint8_t>& _decoded, const uint8_t _slot = 0x02)
	{
		md::automation::sysex::Message result{
			0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, _command, 0x01, 0x01, _slot};
		const auto rle = rleEncode(_decoded);
		const auto packed = pack7Bit(rle);
		result.insert(result.end(), packed.begin(), packed.end());
		finishDump(result);
		return result;
	}

	void testSysexRequestsAndStatus()
	{
		using namespace md::automation::sysex;
		require(statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x70, 0x01, 0xf7}),
			"wrong MD status request");
		require(globalRequest(md::MachineModel::Monomachine, 7)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x51, 0x07, 0xf7}),
			"wrong MM global request");
		require(kitRequest(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x53, 0x3f, 0xf7}),
			"wrong MD kit request");
		require(kitSave(md::MachineModel::Machinedrum, 63)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x59, 0x3f, 0xf7}),
			"wrong MD kit save");
		require(kitSave(md::MachineModel::Monomachine, 127)
			== Message({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x59, 0x7f, 0xf7}),
			"wrong MM kit save");

		const Message response{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x72, 0x02, 0x37, 0xf7};
		const auto parsed = parseStatusResponse(md::MachineModel::Monomachine,
			response);
		require(parsed && parsed->parameter == StatusParameter::Kit
			&& parsed->value == 0x37, "could not parse status response");
		require(!parseStatusResponse(md::MachineModel::Machinedrum, response),
			"accepted status response for the wrong product");
		const Message setStatus{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x71, 0x02, 0x01, 0xf7};
		const Message strictSetStatus{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00,
			0x71, 0x02, 0x37, 0xf7};
		const auto parsedSetStatus = parseSetStatus(md::MachineModel::Monomachine,
			strictSetStatus);
		require(parsedSetStatus && parsedSetStatus->parameter == StatusParameter::Kit
			&& parsedSetStatus->value == 0x37, "could not parse SET STATUS");
		for(const auto position : {size_t{1}, size_t{2}, size_t{3}, size_t{4},
			size_t{5}, size_t{6}, size_t{9}})
		{
			auto malformed = strictSetStatus;
			malformed[position] ^= 1;
			require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
				"accepted malformed SET STATUS framing");
		}
		auto malformed = strictSetStatus;
		malformed[8] = 0x80;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted non-7-bit SET STATUS value");
		malformed = setStatus;
		malformed[7] = 0x7f;
		require(!parseSetStatus(md::MachineModel::Monomachine, malformed),
			"accepted unknown SET STATUS parameter");
		malformed = setStatus;
		malformed[4] = 0x02;
		malformed[8] = 64;
		require(!parseSetStatus(md::MachineModel::Machinedrum, malformed),
			"accepted out-of-range MD Kit slot");

		for(const auto& request : {
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Global),
			globalRequest(md::MachineModel::Machinedrum, 3),
			kitRequest(md::MachineModel::Machinedrum, 12),
			statusRequest(md::MachineModel::Monomachine, StatusParameter::Pattern),
			globalRequest(md::MachineModel::Monomachine, 7),
			kitRequest(md::MachineModel::Monomachine, 127)})
		{
			const auto model = request[4] == 0x03
				? md::MachineModel::Monomachine
				: md::MachineModel::Machinedrum;
			require(isReadOnlyRequest(model, request),
				"controller query was not classified as read-only");
		}
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, setStatus),
			"state-changing status message was classified as read-only");
		require(!isReadOnlyRequest(md::MachineModel::Monomachine,
			statusRequest(md::MachineModel::Machinedrum, StatusParameter::Kit)),
			"wrong-product query was classified as read-only");
		auto invalidRequest = globalRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 8;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range Global request was classified as read-only");
		invalidRequest = kitRequest(md::MachineModel::Machinedrum, 0);
		invalidRequest[7] = 64;
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"out-of-range MD Kit request was classified as read-only");
		invalidRequest = statusRequest(md::MachineModel::Machinedrum,
			StatusParameter::Global);
		invalidRequest.push_back(0xf7);
		require(!isReadOnlyRequest(md::MachineModel::Machinedrum, invalidRequest),
			"request with trailing data was classified as read-only");
	}

	void testMidiRunningStatus()
	{
		synthLib::MidiBufferParser parser(synthLib::MidiEventSource::Device);
		std::vector<synthLib::SMidiEvent> events;
		parser.write(std::vector<uint8_t>{
			0xb2, 16, 1, 17, 2, 18, 3,
			0xf8,
			19, 4,
			0xf1, 0x05,
			20, 6,
			0x92, 60, 100, 61, 101});
		parser.getEvents(events);
		require(events.size() == 8, "wrong MIDI running-status event count");
		require(events[0].a == 0xb2 && events[0].b == 16 && events[0].c == 1,
			"wrong first running-status CC");
		require(events[1].a == 0xb2 && events[1].b == 17 && events[1].c == 2,
			"running-status CC lost retained status");
		require(events[2].a == 0xb2 && events[2].b == 18 && events[2].c == 3,
			"wrong third running-status CC");
		require(events[3].a == 0xf8,
			"realtime byte was not emitted independently");
		require(events[4].a == 0xb2 && events[4].b == 19 && events[4].c == 4,
			"realtime byte incorrectly cancelled running status");
		require(events[5].a == 0xf1 && events[5].b == 0x05,
			"System Common message was parsed incorrectly");
		require(events[6].a == 0x92 && events[6].b == 60 && events[6].c == 100,
			"orphan data after System Common was not discarded");
		require(events[7].a == 0x92 && events[7].b == 61 && events[7].c == 101,
			"note-on running status was parsed incorrectly");

		parser.write(std::vector<uint8_t>{0xb0, 7});
		parser.discardPartialMessage();
		parser.write(std::vector<uint8_t>{100, 101});
		events.clear();
		parser.getEvents(events);
		require(events.empty(), "discontinuity retained unsafe running status");
	}

	void testMachinedrumDumps()
	{
		using namespace md::automation;
		using namespace md::automation::sysex;
		Message global{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x50, 0x06, 0x01, 0x05};
		global.resize(0xb0, 0);
		global[0xad] = 11;
		// Track routing: 16 raw bytes at 0x0a, 0 to 5 for A to F, 6 for MAIN
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			global[0x0a + track] = static_cast<uint8_t>(track % 7);
		finishDump(global);
		const auto parsedGlobal = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedGlobal && parsedGlobal->slot == 5
			&& parsedGlobal->baseChannel == 11,
			"wrong MD base channel");
		require(parsedGlobal->trackOutputs && (*parsedGlobal->trackOutputs)[0] == TrackOutput::A
			&& (*parsedGlobal->trackOutputs)[6] == TrackOutput::Main && (*parsedGlobal->trackOutputs)[9] == TrackOutput::C,
			"wrong MD track routing");
		{
			auto unknown = global;
			unknown.resize(unknown.size() - 5);
			unknown[0x0a + 3] = 7;
			finishDump(unknown);
			const auto parsed = parseGlobalDump(md::MachineModel::Machinedrum, unknown);
			require(parsed && !parsed->trackOutputs, "MD routing accepted an output the machine does not have");
		}
		require(trackRouting(4, TrackOutput::C) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5c, 4, 2, 0xf7}
			&& trackRouting(15, TrackOutput::Main) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5c, 15, 6, 0xf7},
			"wrong SET TRACK ROUTING");
		require(!trackRouting(16, TrackOutput::A) && !trackRouting(0, static_cast<TrackOutput>(7)),
			"SET TRACK ROUTING accepted a track or output out of range");
		global[0xad] = 0x7f;
		global.resize(global.size() - 5);
		finishDump(global);
		const auto parsedNone = parseGlobalDump(md::MachineModel::Machinedrum, global);
		require(parsedNone && parsedNone->slot == 5
			&& parsedNone->baseChannel == 0x7f,
			"MD MIDI NONE should be a valid persisted setting");
		global[9] = 8;
		global.resize(global.size() - 5);
		finishDump(global);
		require(!parseGlobalDump(md::MachineModel::Machinedrum, global),
			"accepted out-of-range MD Global slot");

		Message kit{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x52, 0x04, 0x01, 0x04};
		kit.resize(1228, 0);
		for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
		{
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
				kit[0x1a + track * 24 + parameter]
					= static_cast<uint8_t>((track * 24 + parameter) & 0x7f);
			kit[0x19a + track] = static_cast<uint8_t>(100 + track);
		}
		// Machine assignments: 16 big-endian 32-bit values in 7-bit groups at 0x1aa.
		// Track 1 is ROM-33 (176, needs the top bit), track 2 TRX-BD with the TONAL
		// flag in its upper bits, the others EFM-BD + track.
		{
			std::vector<uint8_t> raw;
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				const uint32_t value = track == 1 ? 176u : track == 2 ? 0x20000u | 16u : 32u + track;
				raw.push_back(static_cast<uint8_t>(value >> 24));
				raw.push_back(static_cast<uint8_t>(value >> 16));
				raw.push_back(static_cast<uint8_t>(value >> 8));
				raw.push_back(static_cast<uint8_t>(value));
			}
			size_t position = 0x1aa;
			for(size_t group = 0; group < raw.size(); group += 7)
			{
				uint8_t highBits = 0;
				const auto count = std::min<size_t>(7, raw.size() - group);
				for(size_t bit = 0; bit < count; ++bit)
					if(raw[group + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				kit[position++] = highBits;
				for(size_t bit = 0; bit < count; ++bit)
					kit[position++] = raw[group + bit] & 0x7f;
			}
		}
		// The name: 16 raw bytes at 0x0a, here padded with spaces and ended by a zero
		const std::string name = "BROKEN DUB  ";
		std::copy(name.begin(), name.end(), kit.begin() + 0x0a);
		// Master effects: 32 raw bytes at 0x487, reverb, echo, EQ, dynamix (mdEditorFirmwareTest)
		for(uint8_t index = 0; index < 32; ++index)
			kit[0x487 + index] = static_cast<uint8_t>(index + 1);
		finishDump(kit);
		const auto parsedKit = parseKitDump(md::MachineModel::Machinedrum, kit);
		require(parsedKit && parsedKit->slot == 4
			&& parsedKit->parameters.size() == 400, "wrong MD Kit parameter count");
		require(parsedKit->parameters[0]
			== ParameterChange{machinedrum::Synthesis, 0, 0, 0},
			"wrong first MD Kit parameter");
		require(parsedKit->parameters[399]
			== ParameterChange{machinedrum::Level, 15, 0, 115},
			"wrong last MD Kit parameter");
		require(parsedKit->machines.size() == machinedrum::TrackCount, "MD Kit machines missing");
		require(parsedKit->machines[0] == 32 && parsedKit->machines[15] == 47, "wrong MD Kit machine ids");
		require(parsedKit->machines[1] == 176, "MD ROM-33 lost the top bit of its 7-bit group");
		require(parsedKit->machines[2] == 16, "MD TONAL flag leaked into the machine id");
		require(parsedKit->name == "BROKEN DUB", ("wrong MD Kit name: \"" + parsedKit->name + "\"").c_str());
		require(parsedKit->masterEffects.has_value(), "MD Kit master effects missing");
		const auto& effects = *parsedKit->masterEffects;
		require(effects[static_cast<uint8_t>(MasterEffect::Reverb)][0] == 1 && effects[static_cast<uint8_t>(MasterEffect::Echo)][0] == 9
			&& effects[static_cast<uint8_t>(MasterEffect::Eq)][7] == 24 && effects[static_cast<uint8_t>(MasterEffect::Dynamix)][7] == 32,
			"MD master effects not read in the dump's reverb, echo, EQ, dynamix order");
		require(masterEffectChange(MasterEffect::Echo, 0, 5) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5d, 0, 5, 0xf7}
			&& masterEffectChange(MasterEffect::Reverb, 2, 64) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5e, 2, 64, 0xf7}
			&& masterEffectChange(MasterEffect::Dynamix, 7, 127) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x60, 7, 127, 0xf7},
			"wrong master effect messages");
		require(!masterEffectChange(MasterEffect::Eq, 8, 0) && !masterEffectChange(MasterEffect::Eq, 0, 128),
			"master effect message accepted a parameter or value out of range");
		{
			// A shorter dump (older format) holds no master effects
			auto shortKit = kit;
			shortKit.resize(0x4a7);
			finishDump(shortKit);
			const auto parsed = parseKitDump(md::MachineModel::Machinedrum, shortKit);
			require(parsed && !parsed->masterEffects, "master effects read from a dump of another size");
		}
		kit[100] ^= 1;
		require(!parseKitDump(md::MachineModel::Machinedrum, kit),
			"accepted corrupt MD Kit checksum");
	}

	void testMonomachineDumps()
	{
		using namespace md::automation;
		using namespace md::automation::sysex;
		std::vector<uint8_t> globalData(260, 0);
		globalData[0] = 0x91;
		globalData[1] = 5;
		auto global = makeMmDump(0x50, globalData);
		const auto parsedGlobal = parseGlobalDump(md::MachineModel::Monomachine, global);
		require(parsedGlobal && parsedGlobal->slot == 2
			&& parsedGlobal->baseChannel == 5,
			"wrong MM base channel or broken 7-bit/RLE decode");

		std::vector<uint8_t> kitData(698, 0);
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			kitData[0x0b + track] = static_cast<uint8_t>(90 + track);
			for(uint8_t parameter = 0; parameter < 56; ++parameter)
				kitData[0x11 + track * 72 + parameter]
					= static_cast<uint8_t>((track * 56 + parameter) & 0x7f);
		}
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
			kitData[0x11 + 6 * 72 + track] = static_cast<uint8_t>(track == 5 ? 33 : 3 + track);
		// The name fills its 11 bytes: no terminator before the levels
		const std::string name = "NIGHT BUS 2";
		std::copy(name.begin(), name.end(), kitData.begin());
		kitData.back() = 0xe5;
		auto kit = makeMmDump(0x52, kitData);
		const auto parsedKit = parseKitDump(md::MachineModel::Monomachine, kit);
		require(parsedKit && parsedKit->slot == 2
			&& parsedKit->parameters.size() == 342, "wrong MM Kit parameter count");
		require(parsedKit->parameters[0]
			== ParameterChange{monomachine::Synthesis, 0, 0, 0},
			"wrong first MM Kit parameter");
		require(parsedKit->parameters[341]
			== ParameterChange{monomachine::Level, 5, 0, 95},
			"wrong last MM Kit parameter");
		require(parsedKit->machines.size() == monomachine::TrackCount
			&& parsedKit->machines[0] == 3 && parsedKit->machines[4] == 7
			&& parsedKit->machines[5] == 33, "wrong MM Kit machine ids");
		require(parsedKit->name == "NIGHT BUS 2", ("wrong MM Kit name: \"" + parsedKit->name + "\"").c_str());
		kit[12] ^= 1;
		require(!parseKitDump(md::MachineModel::Monomachine, kit),
			"accepted corrupt MM Kit checksum");
	}

	// Appends _data packed in 7-bit groups, as Elektron dumps do.
	void append7Bit(md::automation::sysex::Message& _message, const std::vector<uint8_t>& _data)
	{
		for(size_t group = 0; group < _data.size(); group += 7)
		{
			const auto count = std::min<size_t>(7, _data.size() - group);
			uint8_t highBits = 0;
			for(size_t bit = 0; bit < count; ++bit)
				if(_data[group + bit] & 0x80)
					highBits |= static_cast<uint8_t>(1u << (6u - bit));
			_message.push_back(highBits);
			for(size_t bit = 0; bit < count; ++bit)
				_message.push_back(_data[group + bit] & 0x7f);
		}
	}

	void testMachinedrumPattern()
	{
		using namespace md::automation::sysex;
		require(patternRequest(md::MachineModel::Machinedrum, 17) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x68, 17, 0xf7},
			"wrong MD pattern request");
		require(isReadOnlyRequest(md::MachineModel::Machinedrum, patternRequest(md::MachineModel::Machinedrum, 17)),
			"pattern request not treated as read-only");

		// Track 1: trigs on steps 1, 5 and 32. Track 3: step 2. Locks: track 1
		// parameters 0 and 9, track 3 parameter 23; one value each on a few steps.
		const auto be32 = [](std::vector<uint8_t>& _out, const uint32_t _v)
		{
			_out.insert(_out.end(), {uint8_t(_v >> 24), uint8_t(_v >> 16), uint8_t(_v >> 8), uint8_t(_v)});
		};
		std::vector<uint8_t> trigs, masks;
		for(uint8_t track = 0; track < 16; ++track)
		{
			be32(trigs, track == 0 ? 0x80000011u : track == 2 ? 0x2u : 0u);
			be32(masks, track == 0 ? (1u << 0 | 1u << 9) : track == 2 ? 1u << 23 : 0u);
		}
		std::vector<uint8_t> locks(64 * 32, 0xff);
		locks[0 * 32 + 4] = 100;   // row 0: track 1, parameter 0, step 5
		locks[1 * 32 + 31] = 7;    // row 1: track 1, parameter 9, step 32
		locks[2 * 32 + 1] = 64;    // row 2: track 3, parameter 23, step 2

		Message pattern{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x67, 0x03, 0x01, 18};
		append7Bit(pattern, trigs);
		append7Bit(pattern, masks);
		append7Bit(pattern, std::vector<uint8_t>(16, 0));
		pattern.insert(pattern.end(), {0, 24, 0, 0, 5, 3});
		append7Bit(pattern, locks);
		append7Bit(pattern, std::vector<uint8_t>(204, 0));
		finishDump(pattern);
		require(pattern.size() == 0xacb, "test pattern has the wrong size");

		const auto parsed = parseMdPatternDump(pattern);
		require(parsed && parsed->slot == 18 && parsed->length == 24 && parsed->kit == 5, "wrong MD pattern slot, length or Kit");
		require(parsed->hasTrig(0, 0) && parsed->hasTrig(0, 4) && parsed->hasTrig(0, 31) && !parsed->hasTrig(0, 1)
			&& parsed->hasTrig(2, 1) && !parsed->hasTrig(1, 0), "wrong MD pattern trigs");
		require(parsed->lock(0, 0, 4) == uint8_t{100} && parsed->lock(0, 9, 31) == uint8_t{7}
			&& parsed->lock(2, 23, 1) == uint8_t{64}, "wrong MD pattern lock values");
		require(!parsed->lock(0, 0, 3) && !parsed->lock(0, 1, 4) && !parsed->lock(1, 0, 4),
			"MD pattern reported a lock that is not there");

		pattern[200] ^= 1;
		require(!parseMdPatternDump(pattern), "accepted corrupt MD pattern checksum");
		pattern.resize(100);
		require(!parseMdPatternDump(pattern), "accepted truncated MD pattern");
	}

	// Unpacks _count bytes of a 7-bit run starting at _position, which it advances.
	std::vector<uint8_t> read7Bit(const md::automation::sysex::Message& _message, size_t& _position, const size_t _count)
	{
		std::vector<uint8_t> result;
		while(result.size() < _count)
		{
			const auto highBits = _message.at(_position++);
			for(uint8_t bit = 0; bit < 7 && result.size() < _count; ++bit)
				result.push_back(static_cast<uint8_t>(_message.at(_position++) | ((highBits >> (6 - bit)) & 1u) << 7));
		}
		return result;
	}

	// A Machinedrum pattern dump: _trigs and _masks per track, lock rows as given
	// (the rest empty), and with _extension the 64-step form.
	md::automation::sysex::Message makePattern(const uint8_t _length, const std::array<uint32_t, 16>& _trigs,
		const std::array<uint32_t, 16>& _masks, const std::vector<uint8_t>& _locks, const std::vector<uint8_t>& _extension = {})
	{
		using namespace md::automation::sysex;
		std::vector<uint8_t> trigs, masks;
		for(uint8_t track = 0; track < 16; ++track)
		{
			for(const auto shift : {24, 16, 8, 0})
			{
				trigs.push_back(static_cast<uint8_t>(_trigs[track] >> shift));
				masks.push_back(static_cast<uint8_t>(_masks[track] >> shift));
			}
		}
		auto locks = _locks;
		locks.resize(64 * 32, 0xff);
		uint8_t rows = 0;
		for(const auto mask : _masks)
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
				rows += (mask >> parameter) & 1u;
		Message pattern{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x67, 0x03, 0x01, 18};
		append7Bit(pattern, trigs);
		append7Bit(pattern, masks);
		append7Bit(pattern, std::vector<uint8_t>(16, 0));
		pattern.insert(pattern.end(), {0, _length, 0, 0, 5, rows});
		append7Bit(pattern, locks);
		append7Bit(pattern, std::vector<uint8_t>(204, 0));
		if(!_extension.empty())
			append7Bit(pattern, _extension);
		finishDump(pattern);
		return pattern;
	}

	void testMachinedrumPatternEditing()
	{
		using namespace md::automation::sysex;
		// Track 1: trigs on steps 1, 5 and 32, locks on parameters 0 (step 5) and 9
		// (step 32). Track 3: a trig on step 2, a lock on parameter 23.
		std::array<uint32_t, 16> trigs{}, masks{};
		trigs[0] = 0x80000011u;
		trigs[2] = 0x2u;
		masks[0] = 1u << 0 | 1u << 9;
		masks[2] = 1u << 23;
		std::vector<uint8_t> locks(64 * 32, 0xff);
		locks[0 * 32 + 4] = 100;
		locks[1 * 32 + 31] = 7;
		locks[2 * 32 + 1] = 64;
		const auto pattern = makePattern(32, trigs, masks, locks);
		constexpr size_t rowCountPosition = 0xb6;
		require(pattern.size() == 0xacb && pattern[rowCountPosition] == 3, "test pattern has the wrong layout");

		auto editor = MdPatternEditor::fromDump(pattern);
		require(editor && editor->toDump() == pattern, "MD pattern does not survive a read and write unchanged");

		// A trig, then a lock on a new parameter: its row goes between the two of track 1.
		require(editor->setTrig(0, 1, true), "could not set a trig");
		require(editor->setLock(0, 5, 4, 42), "could not lock parameter 5 on step 5");
		auto dump = editor->toDump();
		auto parsed = parseMdPatternDump(dump);
		require(parsed && parsed->hasTrig(0, 1) && parsed->hasTrig(0, 4), "trig not written");
		require(parsed->lock(0, 5, 4) == uint8_t{42} && parsed->lock(0, 0, 4) == uint8_t{100}
			&& parsed->lock(0, 9, 31) == uint8_t{7} && parsed->lock(2, 23, 1) == uint8_t{64}, "rows not kept in order around a new one");
		require(dump[rowCountPosition] == 4, "row count not updated");
		require(!editor->setLock(0, 0, 2, 5), "lock accepted on a step without a trig");
		require(!editor->setLock(0, 0, 4, 128), "lock value above 127 accepted");
		require(!editor->setTrig(0, 32, true) && !editor->setLock(0, 24, 4, 1), "step or parameter out of range accepted");

		// Clearing the new lock and trig gives the original dump back, byte for byte.
		require(editor->setLock(0, 5, 4, std::nullopt) && editor->setTrig(0, 1, false), "could not clear");
		require(editor->toDump() == pattern, "clearing the edits did not restore the pattern");

		// The length: within the steps the dump holds, trigs and locks past it kept.
		require(editor->setLength(16), "could not shorten the pattern");
		parsed = parseMdPatternDump(editor->toDump());
		require(parsed && parsed->length == 16 && parsed->hasTrig(0, 31) && parsed->lock(0, 9, 31) == uint8_t{7},
			"length not written, or a trig past it lost");
		require(!editor->setTrig(0, 20, true), "trig accepted past the length");
		require(!editor->setLength(0) && !editor->setLength(65), "length out of range accepted");
		require(editor->setLength(32) && editor->toDump() == pattern, "length 32 did not restore the pattern");
		// Over 32 steps the long form, its steps 33 to 64 empty; it stays long back at 32
		auto longer = MdPatternEditor::fromDump(pattern);
		require(longer && longer->setLength(40), "length 40 refused");
		dump = longer->toDump();
		parsed = parseMdPatternDump(dump);
		require(dump.size() == 0x1522 && parsed && parsed->steps == 64 && parsed->length == 40 && parsed->hasTrig(0, 31)
			&& !parsed->hasTrig(0, 32) && !parsed->lock(0, 0, 33) && longer->setTrig(0, 39, true) && !longer->setTrig(0, 40, true),
			"length 40 did not give the long form, empty past step 32");
		require(longer->setLength(32) && longer->toDump().size() == 0x1522 && parseMdPatternDump(longer->toDump())->hasTrig(0, 39),
			"back at 32 steps, the long form or step 40 was lost");

		// Clearing a trig clears its locks; an emptied row goes away.
		require(editor->setTrig(0, 4, false), "could not clear a trig");
		dump = editor->toDump();
		parsed = parseMdPatternDump(dump);
		require(parsed && !parsed->hasTrig(0, 4) && !parsed->lock(0, 0, 4) && parsed->lock(0, 9, 31) == uint8_t{7}
			&& parsed->lock(2, 23, 1) == uint8_t{64} && dump[rowCountPosition] == 2, "clearing a trig kept its lock row");

		// 64 rows at most.
		std::array<uint32_t, 16> allTracks{};
		allTracks.fill(1u);
		auto full = MdPatternEditor::fromDump(makePattern(16, allTracks, {}, {}));
		require(full.has_value(), "could not read the empty pattern");
		for(uint8_t track = 0; track < 16; ++track)
			for(uint8_t parameter = 0; parameter < 4; ++parameter)
				require(full->setLock(track, parameter, 0, static_cast<uint8_t>(track * 4 + parameter)), "could not fill the rows");
		require(!full->setLock(0, 4, 0, 1), "a 65th row was accepted");
		parsed = parseMdPatternDump(full->toDump());
		require(parsed && parsed->lock(15, 3, 0) == uint8_t{63} && parsed->lock(0, 0, 0) == uint8_t{0}, "full rows read back wrong");

		// 64-step form: the second half's rows move with the first half's.
		std::vector<uint8_t> extension(64 + 12 + 64 * 32 + 192, 0);
		std::fill(extension.begin() + 76, extension.begin() + 76 + 64 * 32, 0xff);
		extension[2 * 4 + 3] = 0x02;                  // track 3: trig on step 34
		extension[76 + 2 * 32 + 1] = 77;              // row 2 (track 3, parameter 23): step 34
		const auto longPattern = makePattern(48, trigs, masks, locks, extension);
		require(longPattern.size() == 0x1522, "64-step test pattern has the wrong size");
		// Read in full: steps 33 to 64 of the trigs and of the lock rows
		parsed = parseMdPatternDump(longPattern);
		require(parsed && parsed->steps == 64 && parsed->hasTrig(2, 33) && !parsed->hasTrig(2, 34)
			&& parsed->lock(2, 23, 33) == uint8_t{77} && parsed->lock(2, 23, 1) == uint8_t{64} && !parsed->lock(2, 23, 34),
			"steps 33 to 64 of the long form misread");
		auto longEditor = MdPatternEditor::fromDump(longPattern);
		require(longEditor && longEditor->toDump() == longPattern, "64-step pattern does not survive a read and write");
		require(longEditor->setLock(0, 5, 4, 42), "could not add a row to the 64-step pattern");
		dump = longEditor->toDump();
		size_t position = 0xacb - 5;
		const auto second = read7Bit(dump, position, extension.size());
		require(second[76 + 3 * 32 + 1] == 77 && second[76 + 2 * 32 + 1] == 0xff, "second half of the rows did not move with the first");
		require(longEditor->setLock(2, 23, 33, std::nullopt) && longEditor->setLock(2, 23, 1, 50), "could not edit the second half");
		require(longEditor->setLock(0, 5, 4, std::nullopt), "could not clear the added row");
		require(longEditor->setTrig(2, 33, true) && longEditor->setLock(2, 23, 33, 77) && longEditor->setLock(2, 23, 1, 64),
			"could not restore the second half");
		require(longEditor->toDump() == longPattern, "64-step edits did not restore the pattern");

		// Clearing everything: no trig and no lock in either half, the row count 0, the rest kept
		longEditor->clear();
		dump = longEditor->toDump();
		parsed = parseMdPatternDump(dump);
		require(parsed && parsed->length == 48 && dump[rowCountPosition] == 0
			&& std::all_of(parsed->trigs.begin(), parsed->trigs.end(), [](const auto _trigs) { return _trigs == 0; })
			&& std::all_of(parsed->lockMasks.begin(), parsed->lockMasks.end(), [](const uint32_t _mask) { return _mask == 0; }),
			"clearing the pattern left a trig or a lock");
		auto emptyExtension = extension;
		emptyExtension[2 * 4 + 3] = 0;
		emptyExtension[76 + 2 * 32 + 1] = 0xff;
		require(dump == makePattern(48, {}, {}, {}, emptyExtension), "clearing the pattern changed something else");
		require(longEditor->setLength(64) && !longEditor->setLength(65), "64-step form length range wrong");

		// A copy: another slot's number, the checksum made again, every other byte kept
		auto copy = MdPatternEditor::fromDump(longPattern);
		require(copy && copy->setSlot(37) && !copy->setSlot(128), "slot 37 refused, or 128 accepted");
		dump = copy->toDump();
		parsed = parseMdPatternDump(dump);
		const auto source = parseMdPatternDump(longPattern);
		require(parsed && source && parsed->slot == 37 && parsed->length == source->length && parsed->trigs == source->trigs
			&& parsed->lockRows == source->lockRows && dump.size() == longPattern.size()
			&& std::equal(dump.begin() + 10, dump.end() - 5, longPattern.begin() + 10), "the copy is not the pattern under another number");

		require(!MdPatternEditor::fromDump(Message(pattern.begin(), pattern.begin() + 100)), "editor accepted a truncated pattern");
	}

	// A Monomachine pattern dump ($67) holding _decoded: run-length pass, then 7-bit groups, as the
	// firmware sends it (bit 7 of a run byte: the count of the byte after it)
	md::automation::sysex::Message makeMonomachinePattern(const uint8_t _slot, const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> rle;
		for(size_t position = 0; position < _decoded.size();)
		{
			size_t count = 1;
			while(count < 0x7f && position + count < _decoded.size() && _decoded[position + count] == _decoded[position])
				++count;
			if(count == 1 && _decoded[position] < 0x80)
				rle.push_back(_decoded[position]);
			else
				rle.insert(rle.end(), {static_cast<uint8_t>(0x80 | count), _decoded[position]});
			position += count;
		}
		md::automation::sysex::Message pattern{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x67, 0x05, 0x01, _slot};
		append7Bit(pattern, rle);
		finishDump(pattern);
		return pattern;
	}

	// Decoded Monomachine pattern payload (MCL's MNMPattern): 13 kinds of masks, 6 tracks each, 8 bytes
	// big-endian, the swing amount, the lock masks, the notes, then the length, double tempo and Kit;
	// the count of lock rows at 1366, the 62 rows of 64 steps from 1367; 6520 bytes in all.
	constexpr size_t g_mmTrigKinds[] = {0, 1, 2, 6};		// amp, filter, LFO, the trig itself
	constexpr size_t g_mmLockMasks = 628;
	constexpr size_t g_mmNotes = 676;
	constexpr size_t g_mmRows = 1367;

	void setMmBit(std::vector<uint8_t>& _decoded, const size_t _offset, const uint8_t _bit)
	{
		_decoded[_offset + 7 - _bit / 8] |= static_cast<uint8_t>(1u << (_bit % 8));
	}

	// The length, double tempo and Kit where MCL's MNMPattern puts them, the trigs, notes and lock rows,
	// and the same dump edited: another length, a trig, a lock
	void testMonomachinePattern()
	{
		using namespace md::automation::sysex;
		std::vector<uint8_t> decoded(6520, 0);
		decoded[1060] = 24;
		decoded[1061] = 1;
		decoded[1062] = 5;
		std::fill(decoded.begin() + g_mmNotes, decoded.begin() + g_mmNotes + 6 * 64, 0xff);
		std::fill(decoded.begin() + g_mmRows, decoded.begin() + g_mmRows + 62 * 64, 0xff);
		// Track 2: a trig on step 1 that starts everything, playing A2; one on step 10 that starts nothing
		for(const auto kind : g_mmTrigKinds)
			setMmBit(decoded, kind * 48 + 8, 0);
		setMmBit(decoded, 6 * 48 + 8, 9);
		decoded[g_mmNotes + 64] = 45;
		decoded[g_mmNotes + 64 + 9] = 50;
		// Lock bit 3 of track 1 (row 0, a lock on step 5), lock bit 7 of track 2 (row 1, 99 on step 1)
		setMmBit(decoded, g_mmLockMasks, 3);
		setMmBit(decoded, g_mmLockMasks + 8, 7);
		decoded[1366] = 2;
		decoded[g_mmRows + 4] = 7;
		decoded[g_mmRows + 64] = 99;
		const auto pattern = makeMonomachinePattern(17, decoded);
		const auto parsed = parseMmPatternDump(pattern);
		require(parsed && parsed->slot == 17 && parsed->length == 24 && parsed->doubleTempo && parsed->kit == 5,
			"MM pattern length, double tempo or Kit not where MNMPattern has them");
		require(parsed->ampTrigs[1] == 1 && parsed->filterTrigs[1] == 1 && parsed->lfoTrigs[1] == 1
			&& parsed->trigs[1] == 0x201 && parsed->trigs[0] == 0, "MM trig masks misread");
		require(parsed->hasTrig(1, 0) && parsed->hasTrig(1, 9) && !parsed->hasTrig(1, 1) && !parsed->hasTrig(0, 0),
			"MM trigs misread");
		require(parsed->note(1, 0) == 45 && parsed->note(1, 9) == 50 && !parsed->note(1, 1) && !parsed->note(0, 0),
			"MM notes misread");
		require(parsed->lockRows.size() == 2 && parsed->lock(1, 7, 0) == 99 && parsed->lock(0, 3, 4) == 7
			&& !parsed->lock(1, 7, 1) && !parsed->lock(1, 6, 0) && parsed->lockedSteps(1, 7) == 1,
			"MM lock rows misread");
		require(!parseMdPatternDump(pattern) && !parseMmPatternDump(makePattern(32, {}, {}, {})),
			"a pattern dump of one model read as the other's");
		require(!parseMmPatternDump(makeMonomachinePattern(17, std::vector<uint8_t>(decoded.begin(), decoded.begin() + 6500))),
			"a cut MM pattern accepted");

		const auto shorter = withMmPatternLength(pattern, 8);
		const auto reparsed = shorter ? parseMmPatternDump(*shorter) : std::nullopt;
		require(reparsed && reparsed->length == 8 && reparsed->kit == 5 && reparsed->slot == 17, "MM length not written");
		auto expected = decoded;
		expected[1060] = 8;
		require(*shorter == makeMonomachinePattern(17, expected), "writing the length changed something else");
		require(!withMmPatternLength(pattern, 0) && !withMmPatternLength(pattern, 65), "MM length out of range written");

		// A trig with its note on step 3 of track 1, then a lock there: its row comes after track 1's
		// row 0, track 2's row moves down
		auto editor = MmPatternEditor::fromDump(pattern);
		require(editor && editor->toDump() == pattern, "an MM pattern changed without an edit");
		require(!editor->setTrig(0, 24, 60) && !editor->setTrig(6, 0, 60) && !editor->setTrig(0, 0, 128),
			"an MM trig out of range set");
		require(!editor->setLock(0, 5, 2, 11), "an MM lock set on a step without a trig");
		require(editor->setTrig(0, 2, 60) && editor->setLock(0, 5, 2, 11), "MM trig or lock refused");
		expected = decoded;
		for(const auto kind : g_mmTrigKinds)
			setMmBit(expected, kind * 48, 2);
		expected[g_mmNotes + 2] = 60;
		setMmBit(expected, g_mmLockMasks, 5);
		expected[1366] = 3;
		std::copy_n(decoded.begin() + g_mmRows + 64, 64, expected.begin() + g_mmRows + 128);
		std::fill_n(expected.begin() + g_mmRows + 64, 64, 0xff);
		expected[g_mmRows + 64 + 2] = 11;
		require(editor->toDump() == makeMonomachinePattern(17, expected), "MM trig or lock written wrong");
		const auto edited = parseMmPatternDump(editor->toDump());
		require(edited && edited->note(0, 2) == 60 && edited->lock(0, 5, 2) == 11 && edited->lock(1, 7, 0) == 99,
			"MM edits misread");

		// Clearing the trig clears its lock, and the row goes
		require(editor->setTrig(0, 2, std::nullopt), "MM trig not cleared");
		require(editor->toDump() == pattern, "clearing the MM trig left something behind");

		// 62 rows: no 63rd
		auto full = decoded;
		for(uint8_t bit = 0; bit < 60; ++bit)
			setMmBit(full, g_mmLockMasks + 16, bit);
		full[1366] = 62;
		auto fullEditor = MmPatternEditor::fromDump(makeMonomachinePattern(17, full));
		require(fullEditor && fullEditor->setTrig(0, 2, 60) && !fullEditor->setLock(0, 5, 2, 11)
			&& fullEditor->setLock(0, 3, 2, 11), "a 63rd MM lock row added, or a lock on an existing row refused");

		// Cleared: no step mask of any kind, note, lock mask, row or row count left; the length, Kit and the rest
		// kept. Then a copy: another slot's number, nothing else.
		auto clearing = MmPatternEditor::fromDump(pattern);
		require(clearing.has_value(), "MM pattern not editable");
		clearing->clear();
		expected = decoded;
		std::fill_n(expected.begin(), 13 * 48, uint8_t{0});
		std::fill_n(expected.begin() + g_mmLockMasks, 48, uint8_t{0});
		std::fill_n(expected.begin() + g_mmNotes, 6 * 64, uint8_t{0xff});
		std::fill_n(expected.begin() + g_mmRows, 62 * 64, uint8_t{0xff});
		expected[1366] = 0;
		require(clearing->toDump() == makeMonomachinePattern(17, expected), "clearing the MM pattern left a trig or a lock, or changed the rest");
		require(clearing->setSlot(40) && !clearing->setSlot(128), "MM slot 40 refused, or 128 accepted");
		require(clearing->toDump() == makeMonomachinePattern(40, expected), "the MM copy is not the pattern under another number");

		decoded[1060] = 0;
		require(!parseMmPatternDump(makeMonomachinePattern(17, decoded)), "MM pattern of length 0 accepted");
	}

	void testMachineAssignment()
	{
		using md::automation::sysex::Message;
		using md::automation::sysex::assignMachine;
		const auto md = md::MachineModel::Machinedrum;
		const auto mm = md::MachineModel::Monomachine;
		// Same wire form as the firmware tests: track, id, UW flag.
		require(assignMachine(md, 3, 16) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 3, 16, 0, 0xf7},
			"wrong MD ASSIGN MACHINE for TRX-BD");
		require(assignMachine(md, 0, 128) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 0, 0, 1, 0xf7},
			"wrong MD ASSIGN MACHINE for ROM-01");
		require(assignMachine(md, 15, 191) == Message{0xf0, 0, 0x20, 0x3c, 2, 0, 0x5b, 15, 63, 1, 0xf7},
			"wrong MD ASSIGN MACHINE for ROM-48");
		require(assignMachine(mm, 5, 32) == Message{0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, 5, 32, 0, 0xf7},
			"wrong MM ASSIGN MACHINE for DPRO-DDRW");
		require(!assignMachine(md, 16, 16), "MD accepted track 17");
		require(!assignMachine(mm, 6, 3), "MM accepted track 7");
		require(!assignMachine(md, 0, 6), "MD accepted an id that is no machine");
		require(!assignMachine(md, 0, 4), "MD offered GND-SW, which the manual does not list");
		require(!assignMachine(mm, 0, 18), "MM accepted an id that is no machine");

		// Every family has machines, ids are unique, and lookups agree with the list.
		for(const auto model : {md, mm})
		{
			const auto& list = md::machines::machines(model);
			std::vector<uint16_t> ids;
			for(const auto& machine : list)
			{
				ids.push_back(machine.id);
				require(md::machines::find(model, machine.id) == &machine, "machine lookup mismatch");
				require(machine.family < md::machines::families(model).size(), "machine family out of range");
			}
			std::sort(ids.begin(), ids.end());
			require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "duplicate machine id");
			for(size_t family = 0; family < md::machines::families(model).size(); ++family)
				require(std::any_of(list.begin(), list.end(), [family](const auto& _m) { return _m.family == family; }),
					"machine family without machines");
		}
		require(md::machines::machines(md).size() == 142 && md::machines::machines(mm).size() == 22,
			"unexpected machine count");
		require(md::machines::find(mm, 11)->name == "VO-VO-6" && md::machines::find(md, 176)->name == "ROM-33",
			"wrong machine names");

		// Parameter names from the firmware's machine table: every machine offered has them, as the OS has it
		for(const auto model : {md, mm})
		{
			for(const auto& machine : md::machines::machines(model))
				require(!machine.assignable || md::machines::parameterNames(model, machine.id), "machine offered without parameter names");
		}
		const auto name = [](const md::MachineModel _model, const uint16_t _id, const size_t _index)
		{
			const auto* names = md::machines::parameterNames(_model, _id);
			return names ? std::string((*names)[_index]) : std::string("?");
		};
		require(name(md, 16, 0) == "PTCH" && name(md, 16, 7) == "CLIP" && name(md, 1, 4).empty(), "wrong TRX-BD or GND-SN names");
		require(name(md, 150, 2) == "HOLD" && name(md, 162, 2) == "HOLD" && name(md, 166, 0) == "MLEV" && name(md, 105, 0) == "NOTE",
			"ROM, RAM or MID machines do not share their family's names");
		require(name(mm, 3, 0) == "PW" && name(mm, 4, 3).empty() && name(mm, 18, 0) == "CNTR", "wrong Monomachine names");
		require(!md::machines::parameterNames(md, 86) && !md::machines::parameterNames(md, 4), "names for a machine the OS lacks");
	}

	void testBackupFile(const char* const _path, const md::MachineModel _model)
	{
		std::ifstream file(_path, std::ios::binary);
		require(file.good(), "could not open optional SysEx backup");
		const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
		size_t globalCount = 0;
		size_t kitCount = 0;
		for(size_t begin = 0; begin < bytes.size();)
		{
			begin = static_cast<size_t>(std::find(bytes.begin() + begin, bytes.end(),
				0xf0) - bytes.begin());
			if(begin == bytes.size())
				break;
			const auto endIterator = std::find(bytes.begin() + begin, bytes.end(), 0xf7);
			if(endIterator == bytes.end())
				break;
			const auto end = static_cast<size_t>(endIterator - bytes.begin());
			const md::automation::sysex::Message message(bytes.begin() + begin,
				bytes.begin() + end + 1);
			if(message.size() > 6 && message[6] == 0x50)
			{
				const auto parsed = md::automation::sysex::parseGlobalDump(
					_model, message);
				require(parsed.has_value(), "could not parse real Global dump");
				require(parsed->slot == message[9],
					"real Global dump reported its format version as its slot");
				++globalCount;
			}
			else if(message.size() > 6 && message[6] == 0x52)
			{
				const auto parsed = md::automation::sysex::parseKitDump(_model, message);
				require(parsed.has_value(), "could not parse real Kit dump");
				for(const auto machine : parsed->machines)
					require(md::machines::find(_model, machine) != nullptr, "real Kit dump holds an unknown machine id");
				require(parsed->slot == message[9],
					"real Kit dump reported its format version as its slot");
				++kitCount;
			}
			else if(message.size() > 6 && message[6] == 0x67 && _model == md::MachineModel::Machinedrum)
			{
				const auto parsed = md::automation::sysex::parseMdPatternDump(message);
				require(parsed.has_value(), "could not parse real MD pattern dump");
				require(parsed->slot == message[9], "real pattern dump reported the wrong slot");
			}
			begin = end + 1;
		}
		require(globalCount > 0, "real backup contained no Global dumps");
		require(kitCount > 0, "real backup contained no Kit dumps");
	}

	void testDumpRequestOrdering()
	{
		using Tracker = md::automation::DumpRequestTracker;
		Tracker kit(false);
		require(kit.statusRequestDue(0, 500),
			"new dump tracker did not request status");
		kit.statusRequestSent(0);
		auto status = kit.observeStatus(7);
		require(status.accepted && status.requestDump && status.selectionChanged,
			"initial status did not select a correlated dump");
		kit.dumpRequestSent(10);
		require(!kit.acceptDump(6) && kit.phase() == Tracker::Phase::AwaitingDump,
			"wrong-slot dump satisfied a correlated request");
		require(!kit.recoverTimedOutDump(2009, 2000),
			"dump timed out before its exact deadline");
		require(kit.recoverTimedOutDump(2010, 2000),
			"dump timeout did not enter status-barrier recovery");
		require(!kit.acceptDump(7),
			"late same-slot dump crossed the retry status barrier");

		kit.statusRequestSent(2010);
		status = kit.observeStatus(7);
		require(status.accepted && status.requestDump && !status.selectionChanged,
			"barrier status did not authorize the replacement dump");
		kit.dumpRequestSent(2020);
		require(kit.acceptDump(7) && kit.ready(),
			"replacement same-slot dump did not complete recovery");
		require(!kit.acceptDump(7),
			"late duplicate dump mutated a completed generation");

		kit.statusRequestSent(3000);
		require(!kit.recoverTimedOutStatus(4999, 2000),
			"periodic status request timed out before its exact deadline");
		require(kit.recoverTimedOutStatus(5000, 2000) && kit.canPollStatus(),
			"lost periodic status response permanently disabled polling");
		kit.statusRequestSent(5000);
		status = kit.observeStatus(7);
		require(status.accepted && !status.requestDump,
			"unchanged Kit poll unnecessarily requested a large dump");
		kit.statusRequestSent(4000);
		status = kit.observeStatus(8);
		require(status.requestDump && status.selectionChanged,
			"changed Kit poll did not invalidate the old selection");

		Tracker global(true);
		global.statusRequestSent(0);
		status = global.observeStatus(1);
		require(status.requestDump, "initial Global status did not request a dump");
		global.dumpRequestSent(1);
		require(global.acceptDump(1), "initial Global dump was rejected");
		global.statusRequestSent(5000);
		status = global.observeStatus(1);
		require(status.requestDump && !status.selectionChanged,
			"same-slot Global poll did not refresh its MIDI channel data");
	}

	// Factory-like Globals: Machinedrum OS 1.63 (CTRL IN on, clock internal, both
	// outputs on) and Monomachine OS 1.32b (both inputs off).
	md::automation::sysex::Message machinedrumGlobal()
	{
		md::automation::sysex::Message global{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00,
			0x50, 0x06, 0x01, 0x00};
		global.resize(0xc0, 0x06);
		global[0xad] = 0;
		global[0xb2] = 0x60;
		finishDump(global);
		return global;
	}

	std::vector<uint8_t> monomachineGlobalData()
	{
		std::vector<uint8_t> data(264, 0);
		const uint8_t channels[] = {8, 0, 6, 6, 7};
		std::copy(std::begin(channels), std::end(channels), data.begin());
		data[5] = 0x70;
		data[9] = 2;
		data[10] = data[11] = 1;
		for(uint8_t i = 0; i < 6; ++i)
			data[18 + i] = static_cast<uint8_t>(9 + i);
		data[100] = 0x91;
		data.back() = 0x80;
		return data;
	}

	void testGlobalSync()
	{
		using namespace md::automation::sysex;
		constexpr auto mdModel = md::MachineModel::Machinedrum;
		const auto mdGlobal = machinedrumGlobal();
		require(mdGlobal.size() == 0xc5, "MD Global fixture has the wrong size");
		require(parseGlobalSync(mdModel, mdGlobal) == GlobalSync{false, true},
			"wrong MD factory sync reading");
		const auto mdFollow = withGlobalSync(mdModel, mdGlobal, {true, true});
		require(mdFollow && parseGlobalDump(mdModel, *mdFollow), "MD sync patch is not a valid Global");
		require((*mdFollow)[0xb2] == 0x61, "MD sync patch touched the outputs or kept CTRL IN off");
		for(size_t i = 0; i < 0xc0; ++i)
			require(i == 0xb2 || (*mdFollow)[i] == mdGlobal[i], "MD sync patch changed another byte");
		const auto mdRelease = withGlobalSync(mdModel, *mdFollow, {false, false});
		require(mdRelease && (*mdRelease)[0xb2] == 0x70, "MD CTRL IN off is bit 4 set");
		require(withGlobalSync(mdModel, *mdFollow, {false, true}) == mdGlobal, "MD sync patch is not reversible");
		auto shortGlobal = mdGlobal;
		shortGlobal.erase(shortGlobal.begin() + 0xb0, shortGlobal.end() - 5);
		shortGlobal.resize(shortGlobal.size() - 5);
		finishDump(shortGlobal);
		require(!parseGlobalSync(mdModel, shortGlobal), "accepted an MD Global of another size");

		constexpr auto mmModel = md::MachineModel::Monomachine;
		const auto data = monomachineGlobalData();
		const auto mmGlobal = makeMmDump(0x50, data, 0);
		require(parseGlobalSync(mmModel, mmGlobal) == GlobalSync{false, false},
			"wrong MM factory sync reading");
		require(withGlobalSync(mmModel, mmGlobal, {false, false}) == mmGlobal,
			"MM Global does not re-encode to the same bytes");
		auto following = data;
		following[5] = 0x71;
		following[6] = 1;
		require(withGlobalSync(mmModel, mmGlobal, {true, true}) == makeMmDump(0x50, following, 0),
			"MM sync patch changed more than TEMPO SYNC and TRANSPORT");
	}

	struct HostSyncProbe
	{
		std::vector<md::automation::sysex::Message> sysex;
		std::vector<md::PanelPacket> panel;
		unsigned requests = 0;
		md::HostSync::Actions actions;

		HostSyncProbe()
		{
			actions.sendSysex = [this](const md::automation::sysex::Message& _message) { sysex.push_back(_message); };
			actions.sendPanel = [this](const md::PanelPacket& _packet) { panel.push_back(_packet); return true; };
			actions.requestGlobal = [this] { ++requests; };
		}
	};

	// Services _sync for _seconds of emulated time, a block at a time.
	uint64_t runHostSync(md::HostSync& _sync, HostSyncProbe& _probe, uint64_t _frames, const double _seconds,
		const uint64_t _epoch = 1)
	{
		const auto end = _frames + static_cast<uint64_t>(_seconds * md::g_samplerate);
		for(; _frames < end; _frames += 256)
			_sync.service(_frames, _epoch, _probe.actions);
		return _frames;
	}

	void testHostSync()
	{
		using namespace md::automation::sysex;
		using Target = md::HostSync::Target;
		using State = md::HostSync::State;
		constexpr auto mdModel = md::MachineModel::Machinedrum;
		const auto factory = machinedrumGlobal();
		const auto following = *withGlobalSync(mdModel, factory, {true, true});

		md::HostSync sync(mdModel);
		HostSyncProbe probe;
		uint64_t frames = runHostSync(sync, probe, 0, 1);
		require(probe.sysex.empty() && probe.requests == 0 && sync.getState() == State::Unknown,
			"host sync acted while left alone");

		sync.setTarget(Target::Follow);
		frames = runHostSync(sync, probe, frames, 0.1);
		require(probe.requests == 1 && probe.sysex.empty(), "host sync did not ask for the Global it lacks");
		sync.onGlobalDump(factory);
		frames = runHostSync(sync, probe, frames, 0.1);
		require(probe.sysex.empty() && sync.getState() == State::Applying,
			"host sync changed a machine that just came up");
		while(probe.sysex.empty() && frames < 12 * md::g_samplerate)
			frames = runHostSync(sync, probe, frames, 0.001);
		require(frames >= 10 * md::g_samplerate, "host sync did not let the machine settle first");
		require(probe.sysex.size() == 2 && sync.getState() == State::Applying, "MD Global was not rewritten");
		require(parseGlobalSync(mdModel, probe.sysex[0]) == GlobalSync{true, true}
			&& probe.sysex[1] == globalReload(mdModel, 0), "MD rewrite is not the patched Global and a reload");
		// An answer to a request sent before the change must not count as its result.
		sync.onGlobalDump(factory);
		frames = runHostSync(sync, probe, frames, 0.3);
		require(probe.sysex.size() == 2 && probe.requests == 1, "host sync verified too early");
		frames = runHostSync(sync, probe, frames, 0.3);
		require(probe.requests == 2, "host sync did not ask for a Global to verify its change");
		sync.onGlobalDump(following);
		frames = runHostSync(sync, probe, frames, 0.1);
		require(sync.getState() == State::Following && probe.sysex.size() == 2, "MD did not settle as following");

		sync.setTarget(Target::Release);
		frames = runHostSync(sync, probe, frames, 0.1);
		require(probe.sysex.size() == 4 && parseGlobalSync(mdModel, probe.sysex[2]) == GlobalSync{false, true},
			"release did not restore the MD factory setting");
		frames = runHostSync(sync, probe, frames, 0.6);
		sync.onGlobalDump(factory);
		frames = runHostSync(sync, probe, frames, 0.1);
		require(sync.getState() == State::NotFollowing && sync.getTarget() == Target::Leave,
			"release did not settle and hand back to Leave");

		// A machine that never takes the setting: MaxAttempts rewrites, then Failed.
		sync.setTarget(Target::Follow);
		for(unsigned i = 0; i < 20; ++i)
		{
			frames = runHostSync(sync, probe, frames, 0.5);
			sync.onGlobalDump(factory);
		}
		require(sync.getState() == State::Failed
			&& probe.sysex.size() == 4 + 2 * md::HostSync::MaxAttempts, "host sync did not give up");

		// A replaced machine is read again before anything is sent to it.
		const auto sent = probe.sysex.size();
		const auto requests = probe.requests;
		frames = runHostSync(sync, probe, frames, 0.1, 2);
		require(sync.getState() == State::Unknown && probe.sysex.size() == sent && probe.requests > requests,
			"host sync kept state across machines");

		constexpr auto mmModel = md::MachineModel::Monomachine;
		const auto mmFactory = makeMmDump(0x50, monomachineGlobalData(), 0);
		md::HostSync mm(mmModel);
		HostSyncProbe mmProbe;
		mm.setTarget(Target::Follow);
		mm.onGlobalDump(mmFactory);
		frames = runHostSync(mm, mmProbe, 0, 15);
		const auto macro = md::monomachineSyncMacro({true, true});
		require(mmProbe.panel.size() == macro.size() && mmProbe.sysex.empty(), "MM macro was not played in full");
		for(size_t i = 0; i < macro.size(); ++i)
			require(mmProbe.panel[i] == macro[i].packet, "MM macro packets out of order");
		const auto function = md::panelPacket(mmModel, md::PanelControl::Function);
		const auto kit = md::panelPacket(mmModel, md::PanelControl::Kit);
		require(std::any_of(macro.begin(), macro.end(), [&](const md::PanelMacroStep& _step)
			{
				return _step.packet.row == function->row && _step.packet.mask == (function->mask | kit->mask);
			}), "MM macro does not hold FUNCTION with KIT");
		require(macro.back().packet.mask == 0, "MM macro leaves a button held");
		require(mmProbe.requests >= 1 && mm.getState() == State::Applying, "MM macro was not verified");
		mm.onGlobalDump(*withGlobalSync(mmModel, mmFactory, {true, true}));
		runHostSync(mm, mmProbe, frames, 0.1);
		require(mm.getState() == State::Following, "MM did not settle as following");
	}
}

// The Monomachine pattern writer against a stand-in machine: its keys, its dumps and its screen. A screen
// that changes once a dump is off the MIDI line (RECV n MSG.) has it sent once, the request after it
// last; a screen that never changes (a dropped dump) has it sent three times, then the menus are left all
// the same; nothing is done in the machine's first ten seconds; another machine abandons a write.
void testMonomachinePatternWriter()
{
	using Message = md::automation::sysex::Message;
	md::MmPatternWriter writer;
	std::vector<Message> sent;
	size_t keys = 0;
	uint64_t screen = 1;
	uint64_t frames = 0;
	uint64_t dumpSentAt = 0;
	const Message dump(100, 0x10);
	const Message request{0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x68, 0x01, 0xf7};
	md::MmPatternWriter::Actions actions;
	actions.sendSysex = [&](const Message& _message)
	{
		if(_message == dump)
			dumpSentAt = frames;
		sent.push_back(_message);
	};
	actions.sendPanel = [&](const md::PanelPacket&) { ++keys; return true; };
	actions.screenDigest = [&] { return screen; };
	// The emulated frames go on from one write to the next, a block at a time
	const auto run = [&](const uint32_t _id, const bool _taken)
	{
		for(const auto end = frames + md::g_samplerate * 10; frames < end && writer.getDone() != _id; frames += 256)
		{
			writer.service(frames, 1, actions);
			if(_taken && dumpSentAt && frames >= dumpSentAt + md::MmPatternWriter::lineFrames(dump.size()))
				screen = 2;
		}
		return writer.getDone() == _id;
	};

	require(writer.start({1, {dump}, {request}}) && writer.isBusy() && !writer.start({9, {dump}, {}}), "the writer took a write while busy");
	writer.service(md::g_samplerate * 5, 1, actions);
	require(keys == 0 && sent.empty(), "the writer drove the panel behind the boot logo");
	frames = md::g_samplerate * 10;
	require(run(1, true) && writer.getSends() == 1 && sent.size() == 2 && sent.front() == dump && sent.back() == request
		&& keys > 40 && !writer.isBusy(), "a dump taken was not sent once, the request after it");

	sent.clear();
	dumpSentAt = 0;
	screen = 3;
	require(writer.start({2, {dump}, {request}}) && run(2, false) && writer.getSends() == 3
		&& std::count(sent.begin(), sent.end(), dump) == 3 && sent.back() == request, "a dropped dump was not sent three times");

	// Another machine (a state restore) halfway: the write goes with the old one
	sent.clear();
	require(writer.start({3, {dump}, {request}}), "the writer refused a write");
	writer.service(frames, 1, actions);
	require(writer.isBusy() && writer.getDone() == 2, "a write did not start");
	writer.service(frames + 256, 2, actions);
	require(writer.getDone() == 3 && !writer.isBusy() && sent.empty(), "a write was not abandoned with its machine");
}

int main(const int _argc, const char* const* _argv)
{
	testMachinedrum();
	testMonomachine();
	testSysexRequestsAndStatus();
	testMidiRunningStatus();
	testMachinedrumDumps();
	testMonomachineDumps();
	testMachineAssignment();
	testMachinedrumPattern();
	testMachinedrumPatternEditing();
	testMonomachinePattern();
	testMonomachinePatternWriter();
	testDumpRequestOrdering();
	testGlobalSync();
	testHostSync();
	if(_argc > 1)
		testBackupFile(_argv[1], md::MachineModel::Machinedrum);
	if(_argc > 2)
		testBackupFile(_argv[2], md::MachineModel::Monomachine);
	std::cout << "automationMidiTest: PASS\n";
	return 0;
}
