#include "mdsysexautomation.h"

#include "mdmachines.h"

#include <algorithm>
#include <utility>

namespace md::automation::sysex
{
	namespace
	{
		constexpr uint8_t g_globalDump = 0x50;
		constexpr uint8_t g_globalRequest = 0x51;
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_kitRequest = 0x53;
		constexpr uint8_t g_kitSave = 0x59;
		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_statusRequest = 0x70;
		constexpr uint8_t g_setStatus = 0x71;
		constexpr uint8_t g_statusResponse = 0x72;

		uint8_t product(const MachineModel _model)
		{
			return _model == MachineModel::Monomachine ? 0x03 : 0x02;
		}

		Message request(const MachineModel _model, const uint8_t _command,
			const uint8_t _value)
		{
			return {0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00,
				_command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
		}

		bool hasHeader(const MachineModel _model, const MessageView _message,
			const uint8_t _command)
		{
			return _message.size() >= 8 && _message[0] == 0xf0
				&& _message[1] == 0x00 && _message[2] == 0x20
				&& _message[3] == 0x3c && _message[4] == product(_model)
				&& _message[5] == 0x00 && _message[6] == _command
				&& _message.back() == 0xf7;
		}

		bool validDump(const MachineModel _model, const MessageView _message,
			const uint8_t _command)
		{
			if(_message.size() < 13 || !hasHeader(_model, _message, _command))
				return false;
			if(std::any_of(_message.begin() + 1, _message.end() - 1,
				[](const uint8_t _value) { return _value > 0x7f; }))
				return false;

			const auto checksumPosition = _message.size() - 5;
			uint32_t sum = 0;
			for(size_t i = 9; i < checksumPosition; ++i)
				sum += _message[i];
			const auto checksum = static_cast<uint16_t>(
				(_message[checksumPosition] << 7) | _message[checksumPosition + 1]);
			if((sum & 0x3fff) != checksum)
				return false;

			const auto length = static_cast<uint16_t>(
				(_message[checksumPosition + 2] << 7) | _message[checksumPosition + 3]);
			return length == _message.size() - 10;
		}

		bool validStatusValue(const MachineModel _model,
			const StatusParameter _parameter, const uint8_t _value)
		{
			switch(_parameter)
			{
			case StatusParameter::Global:
				return _value < 8;
			case StatusParameter::Kit:
				return _value < (_model == MachineModel::Monomachine ? 128 : 64);
			case StatusParameter::Pattern:
				return _value < 128;
			}
			return false;
		}

		std::optional<StatusResponse> parseStatus(const MachineModel _model,
			const MessageView _message, const uint8_t _command)
		{
			if(_message.size() != 10 || !hasHeader(_model, _message, _command)
				|| std::any_of(_message.begin() + 1, _message.end() - 1,
					[](const uint8_t _value) { return _value > 0x7f; }))
				return std::nullopt;
			const auto parameter = _message[7];
			if(parameter != static_cast<uint8_t>(StatusParameter::Global)
				&& parameter != static_cast<uint8_t>(StatusParameter::Kit)
				&& parameter != static_cast<uint8_t>(StatusParameter::Pattern))
				return std::nullopt;
			const auto typedParameter = static_cast<StatusParameter>(parameter);
			return validStatusValue(_model, typedParameter, _message[8])
				? std::optional<StatusResponse>(StatusResponse{typedParameter, _message[8]})
				: std::nullopt;
		}

		std::optional<std::vector<uint8_t>> decodeMonomachinePayload(
			const MessageView _message)
		{
			const auto end = _message.size() - 5;
			std::vector<uint8_t> rle;
			rle.reserve(end - 10);
			for(size_t position = 10; position < end;)
			{
				const auto highBits = _message[position++];
				for(uint8_t bit = 0; bit < 7 && position < end; ++bit)
				{
					auto value = _message[position++];
					if(highBits & (1u << (6u - bit)))
						value |= 0x80;
					rle.push_back(value);
				}
			}

			std::vector<uint8_t> decoded;
			for(size_t position = 0; position < rle.size(); ++position)
			{
				const auto value = rle[position];
				if((value & 0x80) == 0)
				{
					decoded.push_back(value);
					continue;
				}

				const auto count = static_cast<size_t>(value & 0x7f);
				if(count == 0 || ++position >= rle.size())
					return std::nullopt;
				if(decoded.size() + count > 4096)
					return std::nullopt;
				decoded.insert(decoded.end(), count, rle[position]);
			}
			return decoded;
		}

		// Inverse of decodeMonomachinePayload: run-length pass, then 7-bit groups.
		std::vector<uint8_t> encodeMonomachinePayload(const std::vector<uint8_t>& _decoded)
		{
			// A byte with bit 7 set is the repeat count of the byte after it. Single
			// bytes stay literal unless their own bit 7 would read as a count.
			std::vector<uint8_t> rle;
			rle.reserve(_decoded.size() + 16);
			for(size_t position = 0; position < _decoded.size();)
			{
				const auto value = _decoded[position];
				size_t count = 1;
				while(count < 0x7f && position + count < _decoded.size()
					&& _decoded[position + count] == value)
					++count;
				if(count == 1 && (value & 0x80) == 0)
					rle.push_back(value);
				else
				{
					rle.push_back(static_cast<uint8_t>(0x80 | count));
					rle.push_back(value);
				}
				position += count;
			}

			// Each group of up to seven bytes is preceded by their top bits, MSB first.
			std::vector<uint8_t> packed;
			packed.reserve(rle.size() + rle.size() / 7 + 1);
			for(size_t position = 0; position < rle.size(); position += 7)
			{
				const auto count = std::min<size_t>(7, rle.size() - position);
				uint8_t highBits = 0;
				for(size_t bit = 0; bit < count; ++bit)
				{
					if(rle[position + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				}
				packed.push_back(highBits);
				for(size_t bit = 0; bit < count; ++bit)
					packed.push_back(static_cast<uint8_t>(rle[position + bit] & 0x7f));
			}
			return packed;
		}

		// Appends checksum, length and F7 to a dump that ends with its payload.
		void finishDump(Message& _message)
		{
			uint32_t sum = 0;
			for(size_t i = 9; i < _message.size(); ++i)
				sum += _message[i];
			const auto length = _message.size() - 5;
			_message.push_back(static_cast<uint8_t>((sum >> 7) & 0x7f));
			_message.push_back(static_cast<uint8_t>(sum & 0x7f));
			_message.push_back(static_cast<uint8_t>((length >> 7) & 0x7f));
			_message.push_back(static_cast<uint8_t>(length & 0x7f));
			_message.push_back(0xf7);
		}

		// Machinedrum Global: raw bytes follow the 7-bit packed key map. In the
		// sync byte (OS 1.63, Global version 6), bit 0 selects the EXTERNAL tempo
		// source and bit 4 turns CTRL IN (start, stop, song position) off; the
		// factory Global has both clear. Bits 5 and 6 are the outputs.
		constexpr size_t g_mdGlobalSize = 0xc5;
		constexpr size_t g_mdSyncPosition = 0xb2;
		constexpr uint8_t g_mdClockIn = 0x01;
		constexpr uint8_t g_mdTransportInOff = 0x10;

		// Monomachine Global, decoded payload: five MIDI channels, then a byte
		// whose bit 0 is CLOCK IN (its bits 4 to 6 are CTRL IN, CLOCK OUT and
		// CTRL OUT), then TRANSPORT IN as a byte of its own.
		constexpr size_t g_mmSyncIndex = 5;
		constexpr size_t g_mmTransportInIndex = 6;
		constexpr uint8_t g_mmClockIn = 0x01;
	}

	Message statusRequest(const MachineModel _model,
		const StatusParameter _parameter)
	{
		return request(_model, g_statusRequest, static_cast<uint8_t>(_parameter));
	}

	Message globalRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_globalRequest, _slot);
	}

	Message kitRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitRequest, _slot);
	}

	bool isReadOnlyRequest(const MachineModel _model, const MessageView _message)
	{
		if(_message.size() != 9
			|| std::any_of(_message.begin() + 1, _message.end() - 1,
				[](const uint8_t _value) { return _value > 0x7f; })
			|| !hasHeader(_model, _message, _message[6]))
			return false;
		switch(_message[6])
		{
		case g_globalRequest:
			return _message[7] < 8;
		case g_kitRequest:
			return _message[7]
				< (_model == MachineModel::Monomachine ? 128 : 64);
		case g_statusRequest:
			return _message[7] == static_cast<uint8_t>(StatusParameter::Global)
					|| _message[7] == static_cast<uint8_t>(StatusParameter::Kit)
					|| _message[7] == static_cast<uint8_t>(StatusParameter::Pattern);
		default:
			return false;
		}
	}

	Message kitSave(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitSave, _slot);
	}

	std::optional<StatusResponse> parseStatusResponse(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_statusResponse);
	}

	std::optional<StatusResponse> parseSetStatus(const MachineModel _model,
		const MessageView _message)
	{
		return parseStatus(_model, _message, g_setStatus);
	}

	std::optional<GlobalDump> parseGlobalDump(const MachineModel _model,
		const MessageView _message)
	{
		if(!validDump(_model, _message, g_globalDump))
			return std::nullopt;
		// Dump headers are command, format version, revision, original position.
		// The request correlation identity is the original position, not the format
		// version (which happens to be a small in-range value on both machines).
		const auto slot = _message[9];
		if(slot >= 8)
			return std::nullopt;

		uint8_t channel = 0;
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t baseChannelPosition = 0xad;
			if(_message.size() <= baseChannelPosition)
				return std::nullopt;
			channel = _message[baseChannelPosition];
		}
		else
		{
			const auto decoded = decodeMonomachinePayload(_message);
			if(!decoded || decoded->size() < 2)
				return std::nullopt;
			channel = (*decoded)[1];
		}

		return channel < 16 || channel == 0x7f
			? std::optional<GlobalDump>(GlobalDump{slot, channel}) : std::nullopt;
	}

	std::optional<KitDump> parseKitDump(
		const MachineModel _model, const MessageView _message)
	{
		if(!validDump(_model, _message, g_kitDump))
			return std::nullopt;
		const auto slot = _message[9];
		if(slot >= (_model == MachineModel::Monomachine ? 128 : 64))
			return std::nullopt;

		std::vector<ParameterChange> result;
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t parameterPosition = 0x1a;
			constexpr size_t levelPosition = 0x19a;
			if(_message.size() <= levelPosition + machinedrum::TrackCount)
				return std::nullopt;
			result.reserve(machinedrum::TrackCount * 25);
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				for(uint8_t page = machinedrum::Synthesis;
					page <= machinedrum::Routing; ++page)
				{
					for(uint8_t index = 0; index < 8; ++index)
					{
						const auto offset = parameterPosition + track * 24 + page * 8 + index;
						result.push_back({page, track, index, _message[offset]});
					}
				}
				result.push_back({machinedrum::Level, track, 0,
					_message[levelPosition + track]});
			}

			// Machine assignments follow the levels: 16 big-endian 32-bit values in
			// 7-bit groups (a byte of top bits, MSB first, then up to seven bytes).
			// The id is the low byte; the upper bits carry flags such as TONAL.
			constexpr size_t machinePosition = 0x1aa;
			constexpr size_t machineBytes = machinedrum::TrackCount * 4;
			constexpr size_t machineEncoded = machineBytes + (machineBytes + 6) / 7;
			std::vector<uint16_t> machines;
			if(_message.size() >= machinePosition + machineEncoded + 5)
			{
				std::vector<uint8_t> raw;
				raw.reserve(machineBytes);
				for(size_t position = machinePosition; raw.size() < machineBytes;)
				{
					const auto highBits = _message[position++];
					for(uint8_t bit = 0; bit < 7 && raw.size() < machineBytes; ++bit)
					{
						auto value = _message[position++];
						if(highBits & (1u << (6u - bit)))
							value |= 0x80;
						raw.push_back(value);
					}
				}
				for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
					machines.push_back(raw[track * 4 + 3]);
			}
			return KitDump{slot, std::move(result), std::move(machines)};
		}

		const auto decoded = decodeMonomachinePayload(_message);
		constexpr size_t levelPosition = 0x0b;
		constexpr size_t parameterPosition = 0x11;
		constexpr size_t parameterStride = 72;
		if(!decoded || decoded->size() < parameterPosition
			+ monomachine::TrackCount * parameterStride)
			return std::nullopt;
		result.reserve(monomachine::TrackCount * 57);
		for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
		{
			for(uint8_t page = monomachine::Synthesis;
				page <= monomachine::Lfo3; ++page)
			{
				for(uint8_t index = 0; index < 8; ++index)
				{
					const auto offset = parameterPosition + track * parameterStride
						+ page * 8 + index;
					result.push_back({page, track, index, (*decoded)[offset]});
				}
			}
			result.push_back({monomachine::Level, track, 0,
				(*decoded)[levelPosition + track]});
		}

		// One machine id byte per track right after the parameters.
		constexpr size_t machinePosition = parameterPosition + monomachine::TrackCount * parameterStride;
		std::vector<uint16_t> machines;
		if(decoded->size() >= machinePosition + monomachine::TrackCount)
		{
			for(uint8_t track = 0; track < monomachine::TrackCount; ++track)
				machines.push_back((*decoded)[machinePosition + track]);
		}
		return KitDump{slot, std::move(result), std::move(machines)};
	}

	std::optional<Message> assignMachine(const MachineModel _model, const uint8_t _track,
		const uint16_t _machine)
	{
		const auto trackCount = _model == MachineModel::Monomachine
			? monomachine::TrackCount : machinedrum::TrackCount;
		const auto* machine = machines::find(_model, _machine);
		if(_track >= trackCount || !machine || !machine->assignable)
			return std::nullopt;
		if(_model == MachineModel::Monomachine)
			return Message{0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00, g_assignMachine,
				_track, static_cast<uint8_t>(_machine), 0x00, 0xf7};
		return Message{0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00, g_assignMachine,
			_track, static_cast<uint8_t>(_machine & 0x7f), static_cast<uint8_t>(_machine >= 128 ? 1 : 0), 0xf7};
	}

	Message globalReload(const MachineModel _model, const uint8_t _slot)
	{
		return {0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00, g_setStatus,
			static_cast<uint8_t>(StatusParameter::Global), static_cast<uint8_t>(_slot & 0x07), 0xf7};
	}

	std::optional<GlobalSync> parseGlobalSync(const MachineModel _model,
		const MessageView _message)
	{
		if(!parseGlobalDump(_model, _message))
			return std::nullopt;
		if(_model == MachineModel::Machinedrum)
		{
			if(_message.size() != g_mdGlobalSize)
				return std::nullopt;
			const auto sync = _message[g_mdSyncPosition];
			return GlobalSync{(sync & g_mdClockIn) != 0, (sync & g_mdTransportInOff) == 0};
		}
		const auto decoded = decodeMonomachinePayload(_message);
		if(!decoded || decoded->size() <= g_mmTransportInIndex)
			return std::nullopt;
		return GlobalSync{((*decoded)[g_mmSyncIndex] & g_mmClockIn) != 0,
			(*decoded)[g_mmTransportInIndex] != 0};
	}

	std::optional<Message> withGlobalSync(const MachineModel _model,
		const MessageView _message, const GlobalSync _sync)
	{
		if(!parseGlobalSync(_model, _message))
			return std::nullopt;
		if(_model == MachineModel::Machinedrum)
		{
			Message result(_message.begin(), _message.end() - 5);
			auto& sync = result[g_mdSyncPosition];
			sync = static_cast<uint8_t>((sync & ~(g_mdClockIn | g_mdTransportInOff))
				| (_sync.clockIn ? g_mdClockIn : 0) | (_sync.transportIn ? 0 : g_mdTransportInOff));
			finishDump(result);
			return result;
		}
		auto decoded = *decodeMonomachinePayload(_message);
		auto& sync = decoded[g_mmSyncIndex];
		sync = static_cast<uint8_t>((sync & ~g_mmClockIn) | (_sync.clockIn ? g_mmClockIn : 0));
		decoded[g_mmTransportInIndex] = _sync.transportIn ? 1 : 0;
		Message result(_message.begin(), _message.begin() + 10);
		const auto packed = encodeMonomachinePayload(decoded);
		result.insert(result.end(), packed.begin(), packed.end());
		finishDump(result);
		return result;
	}
}
