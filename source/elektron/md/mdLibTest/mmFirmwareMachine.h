#pragma once

// The Monomachine of the firmware tests: an md::Device booted from the firmware, that follows MIDI clock
// and transport through md::HostSync as in the plug-in (a front-panel macro on the Monomachine), and
// answers SysEx requests.

#include "sysexPanelDriver.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhostsync.h"
#include "mdLib/mdmidiprotocol.h"
#include "mdLib/mdsysexautomation.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace md::test
{
	inline void requireThat(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	inline automation::sysex::Message wrap(const midiProtocol::SysexBody& _body)
	{
		automation::sysex::Message message{0xf0};
		message.insert(message.end(), _body.begin(), _body.end());
		message.push_back(0xf7);
		return message;
	}

	class Monomachine
	{
	public:
		static constexpr auto Model = MachineModel::Monomachine;
		using Message = automation::sysex::Message;
		using Status = automation::sysex::StatusParameter;

		Monomachine(std::vector<uint8_t> _rom, const char* _path) : m_sync(Model)
		{
			synthLib::DeviceCreateParams params;
			params.romData = std::move(_rom);
			params.romName = _path;
			params.customData = deviceCustomData(Model);
			m_device = std::make_unique<Device>(params);
			advanceFrames(hardware(), g_samplerate * 20);
			requireThat(hardware().isFirmwareMidiReady(), "firmware boot incomplete");
			m_slot = status(Status::Global);
		}

		Hardware& hardware() { return m_device->getHardware(); }
		// The Device around it, for what it does on its rendering (Device::process)
		Device& device() { return *m_device; }

		void send(const Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_bytes.front() == 0xf0)
			{
				event.sysex.assign(_bytes.begin(), _bytes.end());
			}
			else
			{
				event.a = _bytes[0];
				event.b = _bytes.size() > 1 ? _bytes[1] : 0;
				event.c = _bytes.size() > 2 ? _bytes[2] : 0;
			}
			requireThat(hardware().sendMidi(event), "MIDI rejected by the input");
		}

		// Sends _request and returns the first SysEx answering with _command
		Message exchange(const Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware().readMidiOut(events);
			send(_request);
			for(unsigned attempt = 0; attempt < 60; ++attempt)
			{
				advanceFrames(hardware(), g_samplerate / 10);
				events.clear();
				hardware().readMidiOut(events);
				for(const auto& event : events)
				{
					if(event.sysex.size() > 9 && event.sysex[6] == _command)
						return Message(event.sysex.begin(), event.sysex.end());
				}
			}
			throw std::runtime_error("no SysEx answer");
		}

		uint8_t status(const Status _parameter)
		{
			const auto reply = exchange(automation::sysex::statusRequest(Model, _parameter), 0x72);
			const auto parsed = automation::sysex::parseStatusResponse(Model, reply);
			requireThat(parsed && parsed->parameter == _parameter, "no status response");
			return parsed->value;
		}

		// Follows MIDI clock and transport, as HostSync makes it in the plug-in
		void follow()
		{
			m_sync.setTarget(HostSync::Target::Follow);
			for(unsigned i = 0; i < 120; ++i)
			{
				std::vector<synthLib::SMidiEvent> events;
				for(uint32_t frames = 0; frames < g_samplerate / 2; frames += 256)
				{
					hardware().processAudio(256, 0);
					m_frames += 256;
					events.clear();
					hardware().readMidiOut(events);
					for(const auto& event : events)
					{
						const Message bytes(event.sysex.begin(), event.sysex.end());
						const auto dump = automation::sysex::parseGlobalDump(Model, bytes);
						if(dump && dump->slot == m_slot)
							m_sync.onGlobalDump(bytes);
					}
					HostSync::Actions actions;
					actions.sendSysex = [this](const Message& _message) { send(_message); };
					actions.sendPanel = [this](const PanelPacket& _packet)
					{
						return hardware().trySendPanelEvent(_packet.row, _packet.mask);
					};
					actions.requestGlobal = [this] { send(automation::sysex::globalRequest(Model, m_slot)); };
					m_sync.service(m_frames, 1, actions);
				}
				if(m_sync.getState() == HostSync::State::Following)
					return;
				requireThat(m_sync.getState() != HostSync::State::Failed, "host sync failed");
			}
			throw std::runtime_error("the Monomachine does not follow the host");
		}

		void selectPattern(const uint8_t _pattern)
		{
			send(wrap(midiProtocol::selectPattern(Model, _pattern)));
			advanceFrames(hardware(), g_samplerate / 4);
			requireThat(status(Status::Pattern) == _pattern, "pattern not selected");
		}

		Message readPattern(const uint8_t _slot)
		{
			return exchange(automation::sysex::patternRequest(Model, _slot), 0x67);
		}

		// Writes a pattern dump: the Monomachine takes one only in GLOBAL > SYSEX RECV
		void writePattern(const Message& _dump)
		{
			enterMmReceive(hardware(), false);
			send(_dump);
			advanceFrames(hardware(), g_samplerate * 2);
			exitMenus(hardware());
			advanceFrames(hardware(), g_samplerate);
		}

	private:
		std::unique_ptr<Device> m_device;
		HostSync m_sync;
		uint64_t m_frames = 0;
		uint8_t m_slot = 0;
	};
}
