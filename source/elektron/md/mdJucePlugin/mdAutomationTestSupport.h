#pragma once

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdsysexautomation.h"

#include "juce_events/juce_events.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mdAutomationTest
{
	constexpr int BlockSize = 128;
	constexpr int SkipReturnCode = 77;

	inline void require(const bool _condition, const std::string& _message)
	{
		if(_condition)
			return;
		throw std::runtime_error(_message);
	}

	inline const char* modelName(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine ? "MM" : "MD";
	}

	inline bool firmwareTestsRequired()
	{
		const auto* const value = std::getenv("MD_AUTOMATION_REQUIRE_FIRMWARE");
		return value != nullptr && std::string(value) == "1";
	}

	inline bool allowMissingFirmware(const char* const _suite,
		const md::MachineModel _model)
	{
		if(firmwareTestsRequired())
			throw std::runtime_error(std::string(_suite) + ": required "
				+ modelName(_model) + " firmware fixture is unavailable");
		std::cout << _suite << ": SKIP " << modelName(_model)
			<< " (firmware unavailable)\n";
		return false;
	}

	struct MidiTelemetry
	{
		uint64_t consumed = 0;
		size_t overflows = 0;
		uint64_t contentionDrops = 0;
		uint64_t capacityDrops = 0;
	};

	class StoppedPlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo result;
			result.setIsPlaying(false);
			result.setIsRecording(false);
			result.setBpm(120.0);
			result.setPpqPosition(0.0);
			return result;
		}
	};

	class Harness
	{
	public:
		explicit Harness(const md::MachineModel _model)
			: model(_model)
			, processor(_model,
				mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{}, false)
			, audioProcessor(static_cast<juce::AudioProcessor&>(processor))
			, controller(dynamic_cast<mdJucePlugin::Controller&>(
				processor.getController()))
			, audio(2, BlockSize)
		{
			audioProcessor.setPlayHead(&playHead);
			audioProcessor.setNonRealtime(true);
		}

		~Harness()
		{
			if(prepared)
				audioProcessor.releaseResources();
		}

		bool hasLocalFirmware()
		{
			return processor.getPlugin().withDeviceLocked(
				[](synthLib::Device* const _device)
				{
					return dynamic_cast<md::Device*>(_device) != nullptr;
				});
		}

		void prepare(const double _sampleRate = 48000.0)
		{
			if(prepared)
				return;
			audioProcessor.prepareToPlay(_sampleRate, BlockSize);
			prepared = true;
		}

		void process(const int _blocks)
		{
			require(prepared, "attempted to process an unprepared instance");
			for(int block = 0; block < _blocks; ++block)
			{
				audio.clear();
				midi.clear();
				audioProcessor.processBlock(audio, midi);
			}
		}

		bool synchronize(const int _maximumBlocks = 12000)
		{
			for(int block = 0; block < _maximumBlocks; ++block)
			{
				process(1);
				if(controller.isAutomationSynchronized())
					return true;
			}
			return false;
		}

		std::string firmwareReadiness()
		{
			std::ostringstream result;
			processor.getPlugin().withDeviceLocked(
				[this, &result](synthLib::Device* const _device)
				{
					auto* const device = dynamic_cast<md::Device*>(_device);
					if(device == nullptr)
					{
						result << "device=nonlocal";
						return;
					}
					auto& hardware = device->getHardware();
					const auto panel = hardware.getFrontPanelSnapshot();
					result << "audio=" << hardware.isAudioReady()
						<< ", mixer=" << hardware.getDspMixer().booted()
						<< ", producer=" << hardware.getDspProducer().booted()
						<< ", pixels=" << panel.countLitPixels()
						<< ", tiles=" << panel.getTileWriteCount()
						<< ", midiQueued=" << hardware.queuedMidiRxBytes()
						<< ", midiConsumed=" << hardware.midiRxConsumedCount()
						<< ", midiOverflows=" << hardware.midiRxOverflowCount()
						<< ", ingressContention="
						<< controller.getRealtimeMidiIngressContentionDropCount()
						<< ", ingressCapacity="
						<< controller.getRealtimeMidiIngressCapacityDropCount();
					result
						<< ", ucCycles=" << hardware.getUC().getCycles()
						<< ", ucPC=" << hardware.getUC().getPC();
				});
			return result.str();
		}

		MidiTelemetry telemetry()
		{
			MidiTelemetry result;
			result.contentionDrops =
				controller.getRealtimeMidiIngressContentionDropCount();
			result.capacityDrops =
				controller.getRealtimeMidiIngressCapacityDropCount();
			processor.getPlugin().withDeviceLocked(
				[&result](synthLib::Device* const _device)
				{
					if(const auto* const device = dynamic_cast<md::Device*>(_device))
					{
						result.consumed =
							device->getHardware().midiRxConsumedCount();
						result.overflows =
							device->getHardware().midiRxOverflowCount();
					}
				});
			return result;
		}

		const md::MachineModel model;
		StoppedPlayHead playHead;
		mdJucePlugin::AudioPluginAudioProcessor processor;
		juce::AudioProcessor& audioProcessor;
		mdJucePlugin::Controller& controller;

	private:
		juce::AudioBuffer<float> audio;
		juce::MidiBuffer midi;
		bool prepared = false;
	};

	inline std::vector<pluginLib::Parameter*> parameters(Harness& _harness,
		const bool _includeMutes = true)
	{
		std::vector<pluginLib::Parameter*> result;
		const auto mutePage = _harness.model == md::MachineModel::Monomachine
			? md::automation::monomachine::Mute
			: md::automation::machinedrum::Mute;
		for(auto* const audioParameter : _harness.audioProcessor.getParameters())
		{
			auto* const parameter =
				dynamic_cast<pluginLib::Parameter*>(audioParameter);
			if(parameter && (_includeMutes
				|| parameter->getDescription().page != mutePage))
				result.push_back(parameter);
		}
		return result;
	}

	inline bool firmwareMidiReady(Harness& _harness)
	{
		bool ready = false;
		_harness.processor.getPlugin().withDeviceLocked(
			[&ready](synthLib::Device* const _device)
			{
				if(const auto* const device = dynamic_cast<md::Device*>(_device))
					ready = device->getHardware().isFirmwareMidiReady();
			});
		return ready;
	}

	inline void hostWrite(pluginLib::Parameter& _parameter, const int _value)
	{
		_parameter.setValue(_parameter.getNormalisableRange().convertTo0to1(
			static_cast<float>(_value)));
	}

	inline void finishDump(md::automation::sysex::Message& _message)
	{
		uint32_t checksum = 0;
		for(size_t index = 9; index < _message.size(); ++index)
			checksum += _message[index];
		checksum &= 0x3fff;
		const auto length = static_cast<uint16_t>(_message.size() - 5);
		_message.push_back(static_cast<uint8_t>(checksum >> 7));
		_message.push_back(static_cast<uint8_t>(checksum & 0x7f));
		_message.push_back(static_cast<uint8_t>(length >> 7));
		_message.push_back(static_cast<uint8_t>(length & 0x7f));
		_message.push_back(0xf7);
	}

	inline std::vector<uint8_t> packMmPayload(const std::vector<uint8_t>& _decoded)
	{
		std::vector<uint8_t> rle;
		for(size_t position = 0; position < _decoded.size();)
		{
			const auto value = _decoded[position];
			size_t count = 1;
			while(position + count < _decoded.size()
				&& _decoded[position + count] == value && count < 127)
				++count;
			if(value >= 0x80 || count > 1)
			{
				rle.push_back(static_cast<uint8_t>(0x80 | count));
				rle.push_back(value);
			}
			else
				rle.push_back(value);
			position += count;
		}

		std::vector<uint8_t> packed;
		for(size_t position = 0; position < rle.size();)
		{
			const auto header = packed.size();
			packed.push_back(0);
			for(uint8_t bit = 0; bit < 7 && position < rle.size(); ++bit)
			{
				const auto value = rle[position++];
				if(value & 0x80)
					packed[header] |= static_cast<uint8_t>(1u << (6u - bit));
				packed.push_back(static_cast<uint8_t>(value & 0x7f));
			}
		}
		return packed;
	}

	inline pluginLib::SysEx makeDump(const md::MachineModel _model,
		const uint8_t _command, const uint8_t _slot,
		const std::vector<uint8_t>& _decoded)
	{
		md::automation::sysex::Message result{
			0xf0, 0x00, 0x20, 0x3c,
			static_cast<uint8_t>(_model == md::MachineModel::Monomachine ? 0x03 : 0x02),
			0x00, _command, 0x01, 0x01, _slot};
		if(_model == md::MachineModel::Monomachine)
		{
			const auto packed = packMmPayload(_decoded);
			result.insert(result.end(), packed.begin(), packed.end());
		}
		else
			result.insert(result.end(), _decoded.begin(), _decoded.end());
		finishDump(result);
		return {result.begin(), result.end()};
	}

	inline pluginLib::SysEx makeGlobalDump(
		const md::MachineModel _model, const uint8_t _slot, const uint8_t _base)
	{
		if(_model == md::MachineModel::Monomachine)
		{
			std::vector<uint8_t> decoded(260, 0);
			decoded[0] = 0x91;
			decoded[1] = _base;
			return makeDump(_model, 0x50, _slot, decoded);
		}
		// makeDump contributes the ten-byte header before this raw payload, so the
		// firmware's absolute base-channel offset 0xad maps to payload offset 0xa3.
		std::vector<uint8_t> decoded(0xa6, 0);
		decoded[0xa3] = _base;
		// Every track on MAIN (6), as in the factory Global; the routing sits at 0x0a
		std::fill_n(decoded.begin(), md::automation::machinedrum::TrackCount, uint8_t{6});
		return makeDump(_model, 0x50, _slot, decoded);
	}

	// A 32-step Machinedrum pattern dump: _trigs[track] as bits, one lock value
	// _lock for track 1 parameter 0 on step 1 when given, everything else empty.
	// The same over 64 steps: a length over 32 gives the long form, steps 33 to 64 after the 32-step
	// sections (MCL's MDPattern), their trigs from the high halves of _trigs, without locks
	inline pluginLib::SysEx makeMdPatternDump(const uint8_t _slot, const uint8_t _length,
		const std::array<uint64_t, 16>& _trigs, const int _lock = -1);

	inline pluginLib::SysEx makeMdPatternDump(const uint8_t _slot, const uint8_t _length,
		const std::array<uint32_t, 16>& _trigs, const int _lock = -1)
	{
		std::array<uint64_t, 16> trigs{};
		std::copy(_trigs.begin(), _trigs.end(), trigs.begin());
		return makeMdPatternDump(_slot, _length, trigs, _lock);
	}

	inline pluginLib::SysEx makeMdPatternDump(const uint8_t _slot, const uint8_t _length,
		const std::array<uint64_t, 16>& _trigs, const int _lock)
	{
		const auto pack = [](md::automation::sysex::Message& _out, const std::vector<uint8_t>& _data)
		{
			for(size_t group = 0; group < _data.size(); group += 7)
			{
				const auto count = std::min<size_t>(7, _data.size() - group);
				uint8_t highBits = 0;
				for(size_t bit = 0; bit < count; ++bit)
					if(_data[group + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				_out.push_back(highBits);
				for(size_t bit = 0; bit < count; ++bit)
					_out.push_back(_data[group + bit] & 0x7f);
			}
		};
		std::vector<uint8_t> trigs, masks, highTrigs;
		for(size_t track = 0; track < 16; ++track)
		{
			for(const auto shift : {24, 16, 8, 0})
			{
				trigs.push_back(static_cast<uint8_t>(_trigs[track] >> shift));
				highTrigs.push_back(static_cast<uint8_t>(_trigs[track] >> (32 + shift)));
				masks.push_back(static_cast<uint8_t>(track == 0 && _lock >= 0 && shift == 0 ? 1 : 0));
			}
		}
		std::vector<uint8_t> locks(64 * 32, 0xff);
		if(_lock >= 0)
			locks[0] = static_cast<uint8_t>(_lock);
		md::automation::sysex::Message result{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x67, 0x03, 0x01, _slot};
		pack(result, trigs);
		pack(result, masks);
		pack(result, std::vector<uint8_t>(16, 0));
		result.insert(result.end(), {0, _length, 0, 0, 0, static_cast<uint8_t>(_lock >= 0 ? 1 : 0)});
		pack(result, locks);
		pack(result, std::vector<uint8_t>(204, 0));
		if(_length > 32)
		{
			// Trigs, accent, slide and swing, lock rows and per-track patterns of steps 33 to 64
			auto extension = highTrigs;
			extension.resize(extension.size() + 12, 0);
			extension.resize(extension.size() + 64 * 32, 0xff);
			extension.resize(extension.size() + 192, 0);
			pack(result, extension);
		}
		finishDump(result);
		return {result.begin(), result.end()};
	}

	// An empty Monomachine pattern dump of _length steps, laid out as MCL's MNMPattern: no trig, no
	// note (from byte 676), no lock row (from 1367); the length at 1060. Edit it with MmPatternEditor.
	inline pluginLib::SysEx makeMmPatternDump(const uint8_t _slot, const uint8_t _length)
	{
		std::vector<uint8_t> decoded(6520, 0);
		std::fill_n(decoded.begin() + 676, 6 * 64, uint8_t{0xff});
		decoded[1060] = _length;
		std::fill_n(decoded.begin() + 1367, 62 * 64, uint8_t{0xff});
		return makeDump(md::MachineModel::Monomachine, 0x67, _slot, decoded);
	}

	// _machines, when given, holds one machine id per track; _name is the Kit's name.
	inline pluginLib::SysEx makeKitDump(
		const md::MachineModel _model, const uint8_t _slot, const uint8_t _value,
		const std::vector<uint16_t>& _machines = {}, const std::string& _name = {})
	{
		if(_model == md::MachineModel::Monomachine)
		{
			std::vector<uint8_t> decoded(698, 0);
			// The name: the first 11 decoded bytes
			std::copy_n(_name.begin(), std::min<size_t>(_name.size(), 11), decoded.begin());
			for(uint8_t track = 0; track < md::automation::monomachine::TrackCount;
				++track)
			{
				decoded[0x0b + track] = _value;
				for(uint8_t parameter = 0; parameter < 56; ++parameter)
					decoded[0x11 + track * 72 + parameter] = _value;
				if(track < _machines.size())
					decoded[0x11 + 6 * 72 + track] = static_cast<uint8_t>(_machines[track]);
			}
			return makeDump(_model, 0x52, _slot, decoded);
		}
		std::vector<uint8_t> decoded(1218, 0);
		// The name: 16 bytes right after the ten-byte header makeDump adds (0x0a of the message)
		std::copy_n(_name.begin(), std::min<size_t>(_name.size(), 16), decoded.begin());
		// Master effects, 32 bytes at 0x487 of the message (reverb, echo, EQ, dynamix): _value + n
		for(uint8_t index = 0; index < 32; ++index)
			decoded[0x487 - 0x0a + index] = static_cast<uint8_t>((_value + index) & 0x7f);
		for(uint8_t track = 0; track < md::automation::machinedrum::TrackCount;
			++track)
		{
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
				decoded[0x10 + track * 24 + parameter] = _value;
			decoded[0x190 + track] = _value;
		}
		if(!_machines.empty())
		{
			// 16 big-endian 32-bit values in 7-bit groups, at 0x1aa of the message.
			std::vector<uint8_t> raw;
			for(uint8_t track = 0; track < md::automation::machinedrum::TrackCount; ++track)
			{
				raw.insert(raw.end(), {0, 0, 0});
				raw.push_back(track < _machines.size() ? static_cast<uint8_t>(_machines[track]) : 0);
			}
			size_t position = 0x1a0;
			for(size_t group = 0; group < raw.size(); group += 7)
			{
				const auto count = std::min<size_t>(7, raw.size() - group);
				uint8_t highBits = 0;
				for(size_t bit = 0; bit < count; ++bit)
					if(raw[group + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				decoded[position++] = highBits;
				for(size_t bit = 0; bit < count; ++bit)
					decoded[position++] = raw[group + bit] & 0x7f;
			}
		}
		return makeDump(_model, 0x52, _slot, decoded);
	}
}
