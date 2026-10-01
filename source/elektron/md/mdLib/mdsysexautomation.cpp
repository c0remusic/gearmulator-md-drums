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
		constexpr uint8_t g_patternDump = 0x67;
		constexpr uint8_t g_patternRequest = 0x68;
		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_trackRouting = 0x5c;
		constexpr uint8_t g_masterEffect = 0x5d;    // RHYTHM ECHO; then reverb, EQ, dynamix
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

		// Appends _count bytes in 7-bit groups: each group of up to seven bytes is
		// preceded by their top bits, MSB first. The inverse of read7Bit.
		void append7Bit(std::vector<uint8_t>& _out, const uint8_t* _data, const size_t _count)
		{
			for(size_t position = 0; position < _count; position += 7)
			{
				const auto count = std::min<size_t>(7, _count - position);
				uint8_t highBits = 0;
				for(size_t bit = 0; bit < count; ++bit)
				{
					if(_data[position + bit] & 0x80)
						highBits |= static_cast<uint8_t>(1u << (6u - bit));
				}
				_out.push_back(highBits);
				for(size_t bit = 0; bit < count; ++bit)
					_out.push_back(static_cast<uint8_t>(_data[position + bit] & 0x7f));
			}
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

			std::vector<uint8_t> packed;
			packed.reserve(rle.size() + rle.size() / 7 + 1);
			append7Bit(packed, rle.data(), rle.size());
			return packed;
		}

		// Reads _count bytes packed in 7-bit groups (a byte of top bits, MSB first,
		// then up to seven bytes) starting at _position, which it advances.
		bool read7Bit(const MessageView _message, size_t& _position, const size_t _count, uint8_t* _out)
		{
			for(size_t done = 0; done < _count;)
			{
				if(_position >= _message.size())
					return false;
				const auto highBits = _message[_position++];
				for(uint8_t bit = 0; bit < 7 && done < _count; ++bit, ++done)
				{
					if(_position >= _message.size())
						return false;
					auto value = _message[_position++];
					if(highBits & (1u << (6u - bit)))
						value |= 0x80;
					_out[done] = value;
				}
			}
			return true;
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
		// Before the key map: one raw byte per track, 0 to 5 for outputs A to F, 6 for MAIN.
		constexpr size_t g_mdRoutingPosition = 0x0a;

		// Machinedrum Kit: after the LFOs, 32 raw bytes of master effects, 8 per effect,
		// reverb first (mdEditorFirmwareTest checks the order).
		constexpr size_t g_mdKitSize = 0x4d1;
		constexpr size_t g_mdMasterEffectsPosition = 0x487;
		constexpr std::array<uint8_t, MasterEffectCount> g_mdMasterEffectBlock{
			1,      // Echo
			0,      // Reverb
			2,      // Eq
			3       // Dynamix
		};
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

	Message patternRequest(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_patternRequest, _slot);
	}

	bool PatternDump::hasTrig(const uint8_t _track, const uint8_t _step) const
	{
		return _track < 16 && _step < 32 && (trigs[_track] >> _step) & 1u;
	}

	std::optional<uint8_t> PatternDump::lock(const uint8_t _track, const uint8_t _parameter, const uint8_t _step) const
	{
		if(_track >= 16 || _parameter >= 24 || _step >= 32 || !((lockMasks[_track] >> _parameter) & 1u))
			return std::nullopt;
		size_t row = 0;
		for(uint8_t t = 0; t < _track; ++t)
			for(uint8_t p = 0; p < 24; ++p)
				row += (lockMasks[t] >> p) & 1u;
		for(uint8_t p = 0; p < _parameter; ++p)
			row += (lockMasks[_track] >> p) & 1u;
		if(row >= lockRows.size() || lockRows[row][_step] >= 0x80)
			return std::nullopt;
		return lockRows[row][_step];
	}

	std::optional<PatternDump> parseMdPatternDump(const MessageView _message)
	{
		// 32-step form 0xacb bytes; the 64-step form appends the second half.
		if(!validDump(MachineModel::Machinedrum, _message, g_patternDump)
			|| (_message.size() != 0xacb && _message.size() != 0x1522))
			return std::nullopt;
		PatternDump result;
		result.slot = _message[9];
		if(result.slot >= 128)
			return std::nullopt;

		// Trig and lock-row masks: 16 big-endian 32-bit values each, in their own 7-bit runs.
		size_t position = 0x0a;
		std::array<uint8_t, 64> raw{};
		for(auto* target : {&result.trigs, &result.lockMasks})
		{
			if(!read7Bit(_message, position, raw.size(), raw.data()))
				return std::nullopt;
			for(size_t track = 0; track < 16; ++track)
				(*target)[track] = static_cast<uint32_t>(raw[track * 4]) << 24 | static_cast<uint32_t>(raw[track * 4 + 1]) << 16
					| static_cast<uint32_t>(raw[track * 4 + 2]) << 8 | raw[track * 4 + 3];
		}
		// Accent, slide and swing patterns and the swing amount, then six plain bytes:
		// accent amount, length, double tempo, scale, kit, locked rows.
		std::array<uint8_t, 16> skipped{};
		if(!read7Bit(_message, position, skipped.size(), skipped.data()))
			return std::nullopt;
		// The row count byte is not needed: rows follow the masks, as on the machine.
		result.length = _message[position + 1];
		position += 6;
		if(result.length == 0 || result.length > 64)
			return std::nullopt;

		std::vector<uint8_t> locks(64 * 32);
		if(!read7Bit(_message, position, locks.size(), locks.data()))
			return std::nullopt;
		for(size_t row = 0; row < 64; ++row)
		{
			std::array<uint8_t, 32> values{};
			std::copy_n(locks.begin() + row * 32, 32, values.begin());
			result.lockRows.push_back(values);
		}
		// Parameters 24 and up belong to lock rows the classic format does not have.
		for(auto& mask : result.lockMasks)
			mask &= 0x00ffffffu;
		return result;
	}

	namespace
	{
		// Pattern dump sections after the 6 plain bytes: the lock rows, then accent,
		// slide and swing edit flags and per-track patterns. The 64-step form adds
		// one 7-bit run: trigs, accent/slide/swing, lock rows and per-track patterns
		// of steps 33 to 64.
		constexpr size_t g_patternTailSize = 204;
		constexpr size_t g_patternExtensionSize = 64 + 12 + 64 * 32 + 192;
		constexpr size_t g_patternExtensionLocks = 64 + 12;
		constexpr size_t g_patternRows = 64;
		constexpr uint8_t g_patternParameters = 24;
		constexpr uint8_t g_noLock = 0xff;
	}

	std::optional<MdPatternEditor> MdPatternEditor::fromDump(const MessageView _message)
	{
		if(!parseMdPatternDump(_message))
			return std::nullopt;
		MdPatternEditor editor;
		editor.m_header.assign(_message.begin(), _message.begin() + 0x0a);
		editor.m_locks.resize(g_patternRows * 32);
		editor.m_tail.resize(g_patternTailSize);
		size_t position = 0x0a;
		if(!read7Bit(_message, position, editor.m_trigs.size(), editor.m_trigs.data())
			|| !read7Bit(_message, position, editor.m_masks.size(), editor.m_masks.data())
			|| !read7Bit(_message, position, editor.m_swing.size(), editor.m_swing.data()))
			return std::nullopt;
		std::copy_n(_message.begin() + position, editor.m_plain.size(), editor.m_plain.begin());
		position += editor.m_plain.size();
		if(!read7Bit(_message, position, editor.m_locks.size(), editor.m_locks.data())
			|| !read7Bit(_message, position, editor.m_tail.size(), editor.m_tail.data()))
			return std::nullopt;
		if(_message.size() == 0x1522)
		{
			editor.m_extension.resize(g_patternExtensionSize);
			if(!read7Bit(_message, position, editor.m_extension.size(), editor.m_extension.data()))
				return std::nullopt;
		}
		if(position != _message.size() - 5)
			return std::nullopt;
		return editor;
	}

	uint8_t& MdPatternEditor::trigByte(const uint8_t _track, const uint8_t _step)
	{
		// Big-endian 32-bit masks: step n is bit n % 32 of the low or high half.
		auto* bytes = _step < 32 ? m_trigs.data() : m_extension.data();
		return bytes[_track * 4 + 3 - (_step % 32) / 8];
	}

	bool MdPatternEditor::hasTrig(const uint8_t _track, const uint8_t _step)
	{
		return (trigByte(_track, _step) >> (_step % 8)) & 1u;
	}

	size_t MdPatternEditor::rowIndex(const uint8_t _track, const uint8_t _parameter)
	{
		size_t row = 0;
		for(uint8_t track = 0; track < 16; ++track)
		{
			for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
			{
				if(track == _track && parameter == _parameter)
					return row;
				row += hasRow(track, parameter) ? 1 : 0;
			}
		}
		return row;
	}

	size_t MdPatternEditor::rowCount()
	{
		size_t rows = 0;
		for(uint8_t track = 0; track < 16; ++track)
			for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
				rows += hasRow(track, parameter) ? 1 : 0;
		return rows;
	}

	uint8_t& MdPatternEditor::lockValue(const size_t _row, const uint8_t _step)
	{
		return _step < 32 ? m_locks[_row * 32 + _step] : m_extension[g_patternExtensionLocks + _row * 32 + _step - 32];
	}

	bool MdPatternEditor::rowEmpty(const size_t _row)
	{
		for(uint8_t step = 0; step < stepCount(); ++step)
		{
			if(lockValue(_row, step) < 0x80)
				return false;
		}
		return true;
	}

	void MdPatternEditor::insertRow(const size_t _row)
	{
		for(size_t row = g_patternRows - 1; row > _row; --row)
		{
			for(uint8_t step = 0; step < stepCount(); ++step)
				lockValue(row, step) = lockValue(row - 1, step);
		}
		for(uint8_t step = 0; step < stepCount(); ++step)
			lockValue(_row, step) = g_noLock;
	}

	void MdPatternEditor::removeRow(const size_t _row)
	{
		for(size_t row = _row; row + 1 < g_patternRows; ++row)
		{
			for(uint8_t step = 0; step < stepCount(); ++step)
				lockValue(row, step) = lockValue(row + 1, step);
		}
		for(uint8_t step = 0; step < stepCount(); ++step)
			lockValue(g_patternRows - 1, step) = g_noLock;
	}

	bool MdPatternEditor::setTrig(const uint8_t _track, const uint8_t _step, const bool _on)
	{
		if(_track >= 16 || _step >= std::min(length(), stepCount()))
			return false;
		auto& byte = trigByte(_track, _step);
		const auto bit = static_cast<uint8_t>(1u << (_step % 8));
		if(_on)
		{
			byte |= bit;
			return true;
		}
		byte &= static_cast<uint8_t>(~bit);
		for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
			setLock(_track, parameter, _step, std::nullopt);
		return true;
	}

	bool MdPatternEditor::setLock(const uint8_t _track, const uint8_t _parameter, const uint8_t _step,
		const std::optional<uint8_t> _value)
	{
		if(_track >= 16 || _parameter >= g_patternParameters || _step >= std::min(length(), stepCount())
			|| (_value && *_value >= 0x80))
			return false;
		const auto row = rowIndex(_track, _parameter);
		if(!_value)
		{
			if(!hasRow(_track, _parameter) || row >= g_patternRows)
				return true;
			lockValue(row, _step) = g_noLock;
			if(rowEmpty(row))
			{
				removeRow(row);
				maskByte(_track, _parameter) &= static_cast<uint8_t>(~(1u << (_parameter % 8)));
			}
		}
		else
		{
			if(!hasTrig(_track, _step))
				return false;
			if(!hasRow(_track, _parameter))
			{
				if(rowCount() >= g_patternRows)
					return false;
				insertRow(row);
				maskByte(_track, _parameter) |= static_cast<uint8_t>(1u << (_parameter % 8));
			}
			if(row >= g_patternRows)
				return false;
			lockValue(row, _step) = *_value;
		}
		m_plain[5] = static_cast<uint8_t>(std::min(rowCount(), g_patternRows));
		return true;
	}

	bool MdPatternEditor::setLength(const uint8_t _length)
	{
		if(_length == 0 || _length > stepCount())
			return false;
		m_plain[1] = _length;
		return true;
	}

	Message MdPatternEditor::toDump() const
	{
		Message result(m_header);
		append7Bit(result, m_trigs.data(), m_trigs.size());
		append7Bit(result, m_masks.data(), m_masks.size());
		append7Bit(result, m_swing.data(), m_swing.size());
		result.insert(result.end(), m_plain.begin(), m_plain.end());
		append7Bit(result, m_locks.data(), m_locks.size());
		append7Bit(result, m_tail.data(), m_tail.size());
		if(!m_extension.empty())
			append7Bit(result, m_extension.data(), m_extension.size());
		finishDump(result);
		return result;
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
		case g_patternRequest:
			return _message[7] < 128;
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
		std::optional<TrackOutputs> outputs;
		if(_model == MachineModel::Machinedrum)
		{
			constexpr size_t baseChannelPosition = 0xad;
			if(_message.size() <= baseChannelPosition)
				return std::nullopt;
			channel = _message[baseChannelPosition];
			TrackOutputs routing{};
			bool valid = true;
			for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
			{
				const auto output = _message[g_mdRoutingPosition + track];
				valid &= output <= static_cast<uint8_t>(TrackOutput::Main);
				routing[track] = static_cast<TrackOutput>(output);
			}
			if(valid)
				outputs = routing;
		}
		else
		{
			const auto decoded = decodeMonomachinePayload(_message);
			if(!decoded || decoded->size() < 2)
				return std::nullopt;
			channel = (*decoded)[1];
		}

		return channel < 16 || channel == 0x7f
			? std::optional<GlobalDump>(GlobalDump{slot, channel, outputs}) : std::nullopt;
	}

	namespace
	{
		// A Kit name as the firmware stores it: ASCII up to a zero or the end of
		// its field. Anything else shows as '?'.
		std::string kitName(const uint8_t* _name, const size_t _size)
		{
			std::string name;
			for(size_t index = 0; index < _size && _name[index] != 0; ++index)
				name.push_back(_name[index] >= 0x20 && _name[index] < 0x7f ? static_cast<char>(_name[index]) : '?');
			while(!name.empty() && name.back() == ' ')
				name.pop_back();
			return name;
		}
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
			std::optional<MasterEffects> effects;
			if(_message.size() == g_mdKitSize)
			{
				MasterEffects values{};
				for(uint8_t effect = 0; effect < MasterEffectCount; ++effect)
				{
					const auto* block = &_message[g_mdMasterEffectsPosition + g_mdMasterEffectBlock[effect] * MasterEffectParameters];
					std::copy_n(block, MasterEffectParameters, values[effect].begin());
				}
				effects = values;
			}
			// 16 raw bytes between the slot and the parameters
			constexpr size_t namePosition = 0x0a;
			return KitDump{slot, std::move(result), std::move(machines),
				kitName(&_message[namePosition], parameterPosition - namePosition), effects};
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
		// The first 11 decoded bytes, before the levels
		return KitDump{slot, std::move(result), std::move(machines), kitName(decoded->data(), levelPosition)};
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

	std::optional<Message> masterEffectChange(const MasterEffect _effect, const uint8_t _parameter, const uint8_t _value)
	{
		const auto effect = static_cast<uint8_t>(_effect);
		if(effect >= MasterEffectCount || _parameter >= MasterEffectParameters || _value > 0x7f)
			return std::nullopt;
		return Message{0xf0, 0x00, 0x20, 0x3c, product(MachineModel::Machinedrum), 0x00,
			static_cast<uint8_t>(g_masterEffect + effect), _parameter, _value, 0xf7};
	}

	std::optional<Message> trackRouting(const uint8_t _track, const TrackOutput _output)
	{
		if(_track >= machinedrum::TrackCount || _output > TrackOutput::Main)
			return std::nullopt;
		return Message{0xf0, 0x00, 0x20, 0x3c, product(MachineModel::Machinedrum), 0x00, g_trackRouting,
			_track, static_cast<uint8_t>(_output), 0xf7};
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
