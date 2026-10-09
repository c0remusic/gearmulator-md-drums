#include "mdDrumsMessages.h"

#include "Firmware.h"

#include "mdProtocol/mdautomation.h"
#include "mdProtocol/mdmachines.h"

#include <algorithm>
#include <cmath>

namespace mdDrums::messages
{
	namespace
	{
		constexpr uint8_t g_header[] = {0xf0, 0x7d, 0x4d, 0x44, 0x44};
		constexpr size_t g_commandAt = sizeof(g_header);
		constexpr size_t g_workingKit = 0x0a;	// in the factory patch image
		constexpr size_t g_storedKits = 0x8ca;	// the 16 stored Kits, a record each
		constexpr size_t g_factoryKitCount = 16;

		constexpr uint8_t g_machinedrum[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00};
		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_masterEffect = 0x5d;	// to $60: echo, reverb, EQ, dynamix
		constexpr uint8_t g_lfoChange = 0x62;

		// The Kit dump keeps the master effects in this order: reverb, echo, EQ, dynamix
		constexpr uint8_t g_masterBlock[] = {1, 0, 2, 3};

		struct Writer
		{
			Raw raw;

			Writer& add(const uint8_t _byte)
			{
				if(raw.size < raw.bytes.size())
					raw.bytes[raw.size++] = _byte;
				return *this;
			}
			Writer& add21(const uint32_t _value)
			{
				return add(static_cast<uint8_t>((_value >> 14) & 0x7f)).add(static_cast<uint8_t>((_value >> 7) & 0x7f))
					.add(static_cast<uint8_t>(_value & 0x7f));
			}
			static Writer own(const Command _command)
			{
				Writer w;
				for(const auto byte : g_header)
					w.add(byte);
				w.add(static_cast<uint8_t>(_command));
				return w;
			}
			static Writer machinedrum(const uint8_t _command)
			{
				Writer w;
				for(const auto byte : g_machinedrum)
					w.add(byte);
				w.add(_command);
				return w;
			}
		};

		uint32_t read21(const MessageView _message, const size_t _at)
		{
			return static_cast<uint32_t>(_message[_at]) << 14 | static_cast<uint32_t>(_message[_at + 1]) << 7
				| _message[_at + 2];
		}

		Raw trackFlag(const Command _command, const uint8_t _track, const bool _on)
		{
			return Writer::own(_command).add(static_cast<uint8_t>(_track & 0x0f)).add(_on ? 1 : 0).add(0xf7).raw;
		}

		bool bit(const uint16_t _mask, const uint8_t _track)
		{
			return (_mask >> _track) & 1;
		}
	}

	Raw solo(const uint8_t _track, const bool _on)
	{
		return trackFlag(Command::Solo, _track, _on);
	}

	Raw out(const uint8_t _track, const bool _on)
	{
		return trackFlag(Command::Out, _track, _on);
	}

	Raw tempo(const double _bpm)
	{
		return Writer::own(Command::Tempo).add21(static_cast<uint32_t>(std::lround(std::clamp(_bpm, 30.0, 300.0) * 100.0)))
			.add(0xf7).raw;
	}

	Raw mixer(const Mixer& _mixer)
	{
		return Writer::own(Command::Mixer).add21(_mixer.mute).add21(_mixer.solo).add21(_mixer.out).add(0xf7).raw;
	}

	Raw kitName(const std::array<uint8_t, md::automation::sysex::MdKit::NameSize>& _name)
	{
		auto writer = Writer::own(Command::KitName);
		for(const auto byte : _name)
			writer.add(static_cast<uint8_t>(byte & 0x7f));
		return writer.add(0xf7).raw;
	}

	std::optional<Parsed> parse(const MessageView _message)
	{
		if(_message.size() < g_commandAt + 2 || !std::equal(std::begin(g_header), std::end(g_header), _message.begin())
			|| _message.back() != 0xf7)
			return std::nullopt;
		if(std::any_of(_message.begin() + 1, _message.end() - 1, [](const uint8_t _byte) { return _byte > 0x7f; }))
			return std::nullopt;
		Parsed parsed;
		parsed.command = static_cast<Command>(_message[g_commandAt]);
		const auto data = g_commandAt + 1;
		const auto length = _message.size() - data - 1;
		switch(parsed.command)
		{
		case Command::Solo:
		case Command::Out:
			if(length != 2 || _message[data] > 15 || _message[data + 1] > 1)
				return std::nullopt;
			parsed.track = _message[data];
			parsed.on = _message[data + 1] != 0;
			return parsed;
		case Command::Tempo:
			if(length != 3)
				return std::nullopt;
			parsed.bpm = read21(_message, data) / 100.0;
			return parsed;
		case Command::Mixer:
			if(length != 9)
				return std::nullopt;
			parsed.mixer.mute = static_cast<uint16_t>(read21(_message, data));
			parsed.mixer.solo = static_cast<uint16_t>(read21(_message, data + 3));
			parsed.mixer.out = static_cast<uint16_t>(read21(_message, data + 6));
			return parsed;
		case Command::KitName:
			if(length != parsed.name.size())
				return std::nullopt;
			std::copy_n(_message.begin() + data, parsed.name.size(), parsed.name.begin());
			return parsed;
		}
		return std::nullopt;
	}

	std::vector<uint8_t> writeState(const State& _state)
	{
		auto bytes = md::automation::sysex::mdKitDump(_state.kit, 0);
		const auto masks = mixer(_state.mixer);
		bytes.insert(bytes.end(), masks.bytes.begin(), masks.bytes.begin() + masks.size);
		return bytes;
	}

	std::optional<State> parseState(const uint8_t* _data, const size_t _size)
	{
		constexpr auto dumpSize = md::automation::sysex::MdKitDumpSize;
		if(!_data || _size <= dumpSize)
			return std::nullopt;
		const std::vector<uint8_t> dump(_data, _data + dumpSize);
		const std::vector<uint8_t> masks(_data + dumpSize, _data + _size);
		auto kit = md::automation::sysex::parseMdKit(dump);
		const auto mixerMessage = parse(masks);
		if(!kit || !mixerMessage || mixerMessage->command != Command::Mixer)
			return std::nullopt;
		return State{*kit, mixerMessage->mixer};
	}

	std::optional<md::automation::sysex::MdKit> factoryKit(const std::vector<uint8_t>& _flashImage)
	{
		const auto image = md::fw::loadPatchImageFromFlash(_flashImage);
		if(image.size() < g_workingKit + md::automation::sysex::MdKit::RecordSize)
			return std::nullopt;
		return md::automation::sysex::MdKit::fromRecord(&image[g_workingKit]);
	}

	std::vector<md::automation::sysex::MdKit> factoryKits(const std::vector<uint8_t>& _flashImage)
	{
		using md::automation::sysex::MdKit;
		const auto image = md::fw::loadPatchImageFromFlash(_flashImage);
		std::vector<MdKit> kits;
		for(size_t k = 0; k < g_factoryKitCount; ++k)
		{
			const auto at = g_storedKits + k * MdKit::RecordSize;
			if(image.size() < at + MdKit::RecordSize)
				break;
			kits.push_back(MdKit::fromRecord(&image[at]));
		}
		return kits;
	}

	std::optional<Raw> parameterMessage(const uint8_t _page, const uint8_t _track, const uint8_t _index, const int _value)
	{
		if(_track >= md::automation::machinedrum::TrackCount)
			return std::nullopt;
		const auto value = static_cast<uint8_t>(std::clamp(_value, 0, 127));
		switch(_page)
		{
		case pages::Synthesis:
		case pages::Effects:
		case pages::Routing:
		case pages::Level:
		case pages::Mute:
		{
			const auto cc = md::automation::encodeParameterChange(md::MachineModel::Machinedrum,
				{_page, _track, _index, value}, 0);
			if(!cc)
				return std::nullopt;
			Raw raw;
			raw.bytes = {(*cc)[0], (*cc)[1], (*cc)[2]};
			raw.size = 3;
			return raw;
		}
		case pages::Machine:
		{
			const auto id = static_cast<uint16_t>(std::clamp(_value, 0, 191));
			const auto* machine = md::machines::find(md::MachineModel::Machinedrum, id);
			if(_index != 0 || !machine || !machine->assignable)
				return std::nullopt;
			// Machinedrum ids 128 and up go out as id - 128 with the UW flag
			return Writer::machinedrum(g_assignMachine).add(_track).add(static_cast<uint8_t>(id & 0x7f))
				.add(id >= 128 ? 1 : 0).add(0xf7).raw;
		}
		case pages::Lfo:
		{
			constexpr uint8_t maximum[] = {15, 23, md::LfoSettings::ShapeCount - 1, md::LfoSettings::ShapeCount - 1,
				md::LfoSettings::UpdateCount - 1};
			if(_index >= std::size(maximum))
				return std::nullopt;
			return Writer::machinedrum(g_lfoChange).add(static_cast<uint8_t>(_track << 3 | _index))
				.add(std::min(value, maximum[_index])).add(0xf7).raw;
		}
		case pages::Mixer:
			if(_index > 1)
				return std::nullopt;
			return _index == 0 ? solo(_track, value != 0) : out(_track, value != 0);
		case pages::Master:
			if(_index >= 32)
				return std::nullopt;
			return Writer::machinedrum(static_cast<uint8_t>(g_masterEffect + _index / 8)).add(_index % 8).add(value)
				.add(0xf7).raw;
		default:
			return std::nullopt;
		}
	}

	std::optional<int> parameterValue(const State& _state, const uint8_t _page, const uint8_t _track, const uint8_t _index)
	{
		if(_track >= md::automation::machinedrum::TrackCount)
			return std::nullopt;
		const auto& kit = _state.kit;
		switch(_page)
		{
		case pages::Synthesis:
		case pages::Effects:
		case pages::Routing:
			return _index < 8 ? std::optional<int>(kit.parameters[_track][_page * 8 + _index]) : std::nullopt;
		case pages::Level:
			return _index == 0 ? std::optional<int>(kit.levels[_track]) : std::nullopt;
		case pages::Mute:
			return _index == 0 ? std::optional<int>(bit(_state.mixer.mute, _track)) : std::nullopt;
		case pages::Machine:
			return _index == 0 ? std::optional<int>(kit.machine(_track)) : std::nullopt;
		case pages::Lfo:
			return _index < 5 ? std::optional<int>(kit.lfos[_track][_index]) : std::nullopt;
		case pages::Mixer:
			if(_index == 0)
				return bit(_state.mixer.solo, _track);
			if(_index == 1)
				return bit(_state.mixer.out, _track);
			return std::nullopt;
		case pages::Master:
			if(_index >= 32)
				return std::nullopt;
			return kit.masterEffects[g_masterBlock[_index / 8] * 8 + _index % 8];
		default:
			return std::nullopt;
		}
	}

	bool isKitParameter(const uint8_t _page)
	{
		return _page != pages::Mute && _page != pages::Mixer;
	}

	bool setKitValue(md::automation::sysex::MdKit& _kit, const uint8_t _page, const uint8_t _track, const uint8_t _index,
		const int _value)
	{
		if(_track >= md::automation::machinedrum::TrackCount)
			return false;
		const auto value = static_cast<uint8_t>(std::clamp(_value, 0, 255));
		switch(_page)
		{
		case pages::Synthesis:
		case pages::Effects:
		case pages::Routing:
			if(_index >= 8)
				return false;
			_kit.parameters[_track][_page * 8 + _index] = value;
			return true;
		case pages::Level:
			if(_index != 0)
				return false;
			_kit.levels[_track] = value;
			return true;
		case pages::Machine:
			if(_index != 0)
				return false;
			_kit.machines[_track] = (_kit.machines[_track] & ~0xffu) | value;
			return true;
		case pages::Lfo:
			if(_index >= 5)
				return false;
			_kit.lfos[_track][_index] = value;
			return true;
		case pages::Master:
			if(_index >= 32)
				return false;
			_kit.masterEffects[g_masterBlock[_index / 8] * 8 + _index % 8] = value;
			return true;
		default:
			return false;
		}
	}
}
