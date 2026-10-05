#include "mdLib/mdhardware.h"
#include "mdLib/mdsysexfile.h"
#include "sysexPanelDriver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;

	Bytes load(const char* _path)
	{
		std::ifstream stream(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
	}

	void require(const bool _condition, const char* _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void boot(md::Hardware& _hardware)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		uint32_t frames = 0;
		while(!_hardware.isFirmwareMidiReady() || !_hardware.isAudioReady())
		{
			_hardware.advance(64);
			frames += 64;
			if(std::chrono::steady_clock::now() >= deadline)
				throw std::runtime_error("MM firmware boot timed out");
		}
		// MIDI-ready becomes true before all firmware startup tasks have settled.
		// Match the established MM workflow harness before requesting user data.
		while(frames < md::g_samplerate * 20)
		{
			_hardware.advance(64);
			frames += 64;
		}
	}

	Bytes findMessage(const Bytes& _wire, const uint8_t _command, const uint8_t _slot)
	{
		for(auto begin = _wire.begin(); begin != _wire.end();)
		{
			begin = std::find(begin, _wire.end(), uint8_t{0xf0});
			if(begin == _wire.end()) break;
			const auto end = std::find(begin, _wire.end(), uint8_t{0xf7});
			if(end == _wire.end()) break;
			Bytes message(begin, end + 1);
			if(message.size() > 14 && message[4] == 3 && message[6] == _command
				&& message[9] == _slot)
				return message;
			begin = end + 1;
		}
		return {};
	}

	std::vector<Bytes> splitMessages(const Bytes& _wire)
	{
		std::vector<Bytes> result;
		for(auto begin = _wire.begin(); begin != _wire.end();)
		{
			begin = std::find(begin, _wire.end(), uint8_t{0xf0});
			if(begin == _wire.end()) break;
			const auto end = std::find(begin, _wire.end(), uint8_t{0xf7});
			if(end == _wire.end()) break;
			result.emplace_back(begin, end + 1);
			begin = end + 1;
		}
		return result;
	}

	void testDump(md::Hardware& _hardware, const uint8_t _command,
		const uint8_t _slot, const char* _name)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hardware.readMidiOut(events);
		Bytes raw;
		_hardware.getUC().setMidiTransmitTap([&raw](const uint8_t _byte) { raw.push_back(_byte); });
		const auto overflowBefore = _hardware.getUC().midiTxOverflowCount();

		synthLib::SMidiEvent request(synthLib::MidiEventSource::Host);
		request.sysex = {0xf0, 0, 0x20, 0x3c, 3, 0,
			static_cast<uint8_t>(_command + 1), _slot, 0xf7};
		require(_hardware.sendMidi(request), "dump request was rejected");

		Bytes host;
		for(size_t attempt = 0; attempt < md::g_samplerate * 10 / 64 && host.empty(); ++attempt)
		{
			_hardware.advance(64);
			events.clear();
			_hardware.readMidiOut(events);
			for(const auto& event : events)
				if(event.sysex.size() > 14 && event.sysex[4] == 3
					&& event.sysex[6] == _command && event.sysex[9] == _slot)
					host.assign(event.sysex.begin(), event.sysex.end());
		}
		_hardware.getUC().setMidiTransmitTap({});
		const auto rawMessage = findMessage(raw, _command, _slot);
		const auto overflow = _hardware.getUC().midiTxOverflowCount() - overflowBefore;
		const auto rawValidation = md::validateMidiSysexStream(rawMessage,
			md::MachineModel::Monomachine);
		const auto hostValidation = md::validateMidiSysexStream(host,
			md::MachineModel::Monomachine);
		std::printf("MM EXPORT type=%s command=%02x slot=%u raw=%zu host=%zu overflow=%llu rawValid=%u hostValid=%u\n",
			_name, _command, _slot, rawMessage.size(), host.size(),
			static_cast<unsigned long long>(overflow),
			rawValidation == md::MidiSysexStreamValidation::Valid,
			hostValidation == md::MidiSysexStreamValidation::Valid);
		require(!rawMessage.empty(), "firmware did not produce requested dump");
		require(rawValidation == md::MidiSysexStreamValidation::Valid,
			"raw firmware dump is invalid");
		require(overflow == 0, "host MIDI output buffer overflowed");
		require(host == rawMessage, "host-visible dump differs from raw firmware output");
	}

	void enterSendAllMenu(md::Hardware& _hardware)
	{
		using namespace md::test;
		panelChord(_hardware, md::PanelControl::Kit);
		panelTap(_hardware, md::PanelControl::Enter);
		panelTap(_hardware, md::PanelControl::Down);
		panelTap(_hardware, md::PanelControl::Down);
		panelTap(_hardware, md::PanelControl::Right);
		panelTap(_hardware, md::PanelControl::Enter);
	}

	void testFullExport(md::Hardware& _hardware)
	{
		using namespace md::test;
		const uint32_t blockSize = []
		{
			const auto* value = std::getenv("MM_SYSEX_EXPORT_BLOCK");
			return value ? static_cast<uint32_t>(std::stoul(value)) : 64u;
		}();
		require(blockSize > 0 && blockSize <= 8192, "invalid export block size");
		enterSendAllMenu(_hardware);
		std::vector<synthLib::SMidiEvent> events;
		_hardware.readMidiOut(events);
		Bytes raw;
		Bytes host;
		_hardware.getUC().setMidiTransmitTap([&raw](const uint8_t _byte) { raw.push_back(_byte); });
		const auto overflowBefore = _hardware.getUC().midiTxOverflowCount();
		uint32_t idleFrames = 0;
		size_t completed = 0;
		size_t maximumRawBurst = 0;
		const auto drainBlock = [&]
		{
			const auto before = raw.size();
			_hardware.advance(blockSize);
			maximumRawBurst = std::max(maximumRawBurst, raw.size() - before);
			events.clear();
			_hardware.readMidiOut(events);
			bool progress = false;
			for(const auto& event : events)
				if(!event.sysex.empty())
				{
					host.insert(host.end(), event.sysex.begin(), event.sysex.end());
					++completed;
					progress = true;
				}
			idleFrames = progress ? 0 : idleFrames + blockSize;
		};
		const auto enter = panelPacket(_hardware.getModel(), md::PanelControl::Enter);
		require(enter.has_value(), "missing MM enter control");
		require(_hardware.trySendPanelEvent(enter->row, enter->mask), "send-all press rejected");
		for(uint32_t frames = 0; frames < 2048; frames += blockSize) drainBlock();
		require(_hardware.trySendPanelEvent(enter->row, 0), "send-all release rejected");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(std::chrono::steady_clock::now() < deadline)
		{
			drainBlock();
			if(completed >= 128 && idleFrames >= md::g_samplerate * 5)
				break;
		}
		_hardware.getUC().setMidiTransmitTap({});
		const auto rawMessages = splitMessages(raw);
		const auto hostMessages = splitMessages(host);
		std::array<size_t, 256> counts{};
		for(const auto& message : hostMessages)
			if(message.size() > 6) ++counts[message[6]];
		const auto overflow = _hardware.getUC().midiTxOverflowCount() - overflowBefore;
		std::printf("MM FULL EXPORT block=%u rawMessages=%zu hostMessages=%zu kits=%zu patterns=%zu songs=%zu globals=%zu rawBytes=%zu hostBytes=%zu maxRawPerBlock=%zu overflow=%llu\n",
			blockSize, rawMessages.size(), hostMessages.size(), counts[0x52], counts[0x67],
			counts[0x69], counts[0x50], raw.size(), host.size(), maximumRawBurst,
			static_cast<unsigned long long>(overflow));
		require(rawMessages.size() == hostMessages.size(), "raw/host message counts differ");
		require(overflow == 0, "host MIDI output buffer overflowed during full export");
		for(size_t i = 0; i < hostMessages.size(); ++i)
		{
			require(rawMessages[i] == hostMessages[i], "host message differs from raw firmware output");
			require(md::validateMidiSysexStream(hostMessages[i], md::MachineModel::Monomachine)
				== md::MidiSysexStreamValidation::Valid, "full export contains invalid message");
		}
		require(hostMessages.size() == 288 && counts[0x52] == 128
			&& counts[0x67] == 128 && counts[0x69] == 24 && counts[0x50] == 8,
			"full export did not contain every user-data dump");
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	if(argc != 3 && argc != 4)
	{
		std::puts("usage: mmSysexExportFirmwareTest <MM-ROM> <1MiB-patch-ram> [full]");
		return 2;
	}
	try
	{
		require(argc == 3 || std::string(argv[3]) == "full", "unknown mode");
		const auto rom = load(argv[1]);
		const auto patchRam = load(argv[2]);
		require(patchRam.size() == 0x100000, "MM patch RAM must be exactly 1 MiB");
		// On the heap: a Hardware outgrows the default 1 MB stack of Windows' main thread
		auto hardwareStorage = std::make_unique<md::Hardware>(rom, argv[1], md::MachineModel::Monomachine, patchRam);
		auto& hardware = *hardwareStorage;
		require(hardware.isValid(), "firmware did not construct a valid machine");
		boot(hardware);
		if(argc == 4 && std::string(argv[3]) == "full")
		{
			testFullExport(hardware);
			return 0;
		}
		testDump(hardware, 0x52, 0, "kit");
		testDump(hardware, 0x67, 0, "pattern");
		testDump(hardware, 0x69, 0, "song");
		testDump(hardware, 0x50, 0, "global");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::fprintf(stderr, "MM EXPORT FAIL: %s\n", _error.what());
		return 1;
	}
}
