#include "mdsysexautomation.h"

#include "mdmachines.h"

#include <algorithm>
#include <bitset>
#include <utility>

namespace md::automation::sysex
{
	namespace
	{
		constexpr uint8_t g_globalDump = 0x50;
		constexpr uint8_t g_globalRequest = 0x51;
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_kitRequest = 0x53;
		constexpr uint8_t g_kitLoad = 0x58;
		constexpr uint8_t g_kitSave = 0x59;
		constexpr uint8_t g_patternDump = 0x67;
		constexpr uint8_t g_patternRequest = 0x68;
		constexpr uint8_t g_assignMachine = 0x5b;
		constexpr uint8_t g_trackRouting = 0x5c;
		constexpr uint8_t g_masterEffect = 0x5d;    // RHYTHM ECHO; then reverb, EQ, dynamix
		constexpr uint8_t g_lfoChange = 0x62;
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

		// _maximum: the most decoded bytes the dump may hold (a pattern holds more than a Global or a Kit)
		std::optional<std::vector<uint8_t>> decodeMonomachinePayload(
			const MessageView _message, const size_t _maximum = 4096)
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
				if(decoded.size() + count > _maximum)
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

	namespace
	{
		// Pattern dump sections after the 6 plain bytes: the lock rows, then accent,
		// slide and swing edit flags and per-track patterns. The 64-step form adds
		// one 7-bit run: trigs, accent/slide/swing, lock rows and per-track patterns
		// of steps 33 to 64 (MCL's MDPattern).
		constexpr size_t g_patternShortSize = 0xacb;
		constexpr size_t g_patternLongSize = 0x1522;
		constexpr size_t g_patternTailSize = 204;
		// In the tail, after the three EDIT ALL words: the tracks' accent, slide and swing masks
		constexpr size_t g_patternTrackFlags = 12;
		constexpr size_t g_patternExtensionSize = 64 + 12 + 64 * 32 + 192;
		// In the extension: steps 33 to 64 of every track's flags, of the lock rows, of the tracks' flags
		constexpr size_t g_patternExtensionFlags = 64;
		constexpr size_t g_patternExtensionLocks = 64 + 12;
		constexpr size_t g_patternExtensionTrackFlags = 64 + 12 + 64 * 32;
		constexpr size_t g_patternRows = 64;
		constexpr uint8_t g_patternParameters = 24;
		constexpr uint8_t g_noLock = 0xff;

		// 16 big-endian 32-bit masks
		uint32_t readMask32(const uint8_t* _bytes)
		{
			return static_cast<uint32_t>(_bytes[0]) << 24 | static_cast<uint32_t>(_bytes[1]) << 16
				| static_cast<uint32_t>(_bytes[2]) << 8 | _bytes[3];
		}

		void writeMask32(uint8_t* _bytes, const uint32_t _mask)
		{
			_bytes[0] = static_cast<uint8_t>(_mask >> 24);
			_bytes[1] = static_cast<uint8_t>(_mask >> 16);
			_bytes[2] = static_cast<uint8_t>(_mask >> 8);
			_bytes[3] = static_cast<uint8_t>(_mask);
		}
	}

	bool PatternDump::hasTrig(const uint8_t _track, const uint8_t _step) const
	{
		return _track < 16 && _step < steps && (trigs[_track] >> _step) & 1u;
	}

	uint64_t PatternDump::flagMask(const StepFlag _flag, const uint8_t _track) const
	{
		const auto flag = static_cast<uint8_t>(_flag);
		if(flag >= StepFlagCount || _track >= 16)
			return 0;
		return flagPerTrack[flag] ? trackFlags[flag][_track] : flags[flag];
	}

	bool PatternDump::hasFlag(const StepFlag _flag, const uint8_t _track, const uint8_t _step) const
	{
		return _step < steps && (flagMask(_flag, _track) >> _step) & 1u;
	}

	std::optional<uint8_t> PatternDump::lock(const uint8_t _track, const uint8_t _parameter, const uint8_t _step) const
	{
		if(_track >= 16 || _parameter >= 24 || _step >= steps || !((lockMasks[_track] >> _parameter) & 1u))
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
			|| (_message.size() != g_patternShortSize && _message.size() != g_patternLongSize))
			return std::nullopt;
		PatternDump result;
		result.slot = _message[9];
		if(result.slot >= 128)
			return std::nullopt;
		result.steps = _message.size() == g_patternLongSize ? 64 : 32;

		// Trig and lock-row masks: 16 big-endian 32-bit values each, in their own 7-bit runs.
		size_t position = 0x0a;
		std::array<uint8_t, 64> raw{};
		if(!read7Bit(_message, position, raw.size(), raw.data()))
			return std::nullopt;
		for(size_t track = 0; track < 16; ++track)
			result.trigs[track] = readMask32(&raw[track * 4]);
		if(!read7Bit(_message, position, raw.size(), raw.data()))
			return std::nullopt;
		for(size_t track = 0; track < 16; ++track)
			result.lockMasks[track] = readMask32(&raw[track * 4]);
		// Accent, slide and swing patterns (steps 1 to 32) and the swing amount, then six plain
		// bytes: accent amount, length, double tempo, scale, kit, locked rows.
		std::array<uint8_t, 16> flags{};
		if(!read7Bit(_message, position, flags.size(), flags.data()))
			return std::nullopt;
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
			result.flags[flag] = readMask32(&flags[flag * 4]);
		result.swingAmount = readMask32(&flags[12]);
		// The row count byte is not needed: rows follow the masks, as on the machine.
		result.accentAmount = _message[position];
		result.length = _message[position + 1];
		result.kit = _message[position + 4];
		position += 6;
		if(result.length == 0 || result.length > 64)
			return std::nullopt;

		std::vector<uint8_t> locks(g_patternRows * 32);
		if(!read7Bit(_message, position, locks.size(), locks.data()))
			return std::nullopt;
		result.lockRows.resize(g_patternRows);
		for(size_t row = 0; row < g_patternRows; ++row)
		{
			result.lockRows[row].fill(g_noLock);
			std::copy_n(locks.begin() + row * 32, 32, result.lockRows[row].begin());
		}
		// The tail: per kind, whether every track follows one mask (MCL: EDIT ALL), then the tracks'
		// own masks, steps 1 to 32
		std::vector<uint8_t> tail(g_patternTailSize);
		if(!read7Bit(_message, position, tail.size(), tail.data()))
			return std::nullopt;
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
		{
			result.flagPerTrack[flag] = readMask32(&tail[flag * 4]) == 0;
			for(size_t track = 0; track < 16; ++track)
				result.trackFlags[flag][track] = readMask32(&tail[g_patternTrackFlags + (flag * 16 + track) * 4]);
		}
		// The 64-step form: steps 33 to 64 of the trigs, the flags and each lock row, after the tail
		if(result.steps == 64)
		{
			std::vector<uint8_t> extension(g_patternExtensionSize);
			if(!read7Bit(_message, position, extension.size(), extension.data()))
				return std::nullopt;
			for(size_t track = 0; track < 16; ++track)
				result.trigs[track] |= static_cast<uint64_t>(readMask32(&extension[track * 4])) << 32;
			for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
			{
				const auto everyTrack = readMask32(&extension[g_patternExtensionFlags + flag * 4]);
				result.flags[flag] |= static_cast<uint64_t>(everyTrack) << 32;
				for(size_t track = 0; track < 16; ++track)
				{
					result.trackFlags[flag][track] |= static_cast<uint64_t>(readMask32(
						&extension[g_patternExtensionTrackFlags + (flag * 16 + track) * 4])) << 32;
				}
			}
			for(size_t row = 0; row < g_patternRows; ++row)
				std::copy_n(&extension[g_patternExtensionLocks + row * 32], 32, result.lockRows[row].begin() + 32);
		}
		// Parameters 24 and up belong to lock rows the classic format does not have.
		for(auto& mask : result.lockMasks)
			mask &= 0x00ffffffu;
		return result;
	}

	namespace
	{
		// Monomachine pattern payload, decoded (MCL's MNMPattern). First 13 kinds of 64-bit masks,
		// big-endian, 6 tracks each: the amp, filter and LFO trigs; what MCL names off, MIDI note on
		// and note off; the trigs themselves (MCL: trigless, a superset of the amp trigs on the
		// factory patterns); chord, MIDI trigless, slide, swing, MIDI slide and MIDI swing. Then the
		// swing amount, 6 lock masks and 6 x 64 notes before the length, double tempo and Kit; then
		// transposition, the arpeggiators, the count of lock rows, the 62 rows of 64 steps, the MIDI
		// and chord notes. The factory patterns decode to 6520 bytes.
		constexpr size_t g_mmMaskSize = 6 * 8;
		constexpr size_t g_mmTrigKinds = 13;
		constexpr size_t g_mmAmpTrigs = 0 * g_mmMaskSize;
		constexpr size_t g_mmFilterTrigs = 1 * g_mmMaskSize;
		constexpr size_t g_mmLfoTrigs = 2 * g_mmMaskSize;
		constexpr size_t g_mmTrigs = 6 * g_mmMaskSize;
		constexpr size_t g_mmSlides = 9 * g_mmMaskSize;
		constexpr size_t g_mmSwings = 10 * g_mmMaskSize;
		constexpr size_t g_mmMidiSwings = 12 * g_mmMaskSize;
		constexpr size_t g_mmSwingAmount = 13 * g_mmMaskSize;		// big-endian, as swingWord makes it
		constexpr size_t g_mmLockMasks = 13 * g_mmMaskSize + 4;
		constexpr size_t g_mmNotes = g_mmLockMasks + g_mmMaskSize;
		constexpr size_t g_mmPatternLengthIndex = g_mmNotes + 6 * 64;
		static_assert(g_mmPatternLengthIndex == 1060, "MNMPattern layout");
		constexpr size_t g_mmLocksUsed = 1366;
		constexpr size_t g_mmLockRows = g_mmLocksUsed + 1;
		constexpr size_t g_mmPatternSize = g_mmLockRows + MmPatternDump::LockRowCount * 64 + 400 * 2 + 192 * 2 + 1;
		static_assert(g_mmPatternSize == 6520, "MNMPattern layout");
		constexpr size_t g_mmPatternMaximum = 8192;

		uint64_t readMask(const uint8_t* _bytes)
		{
			uint64_t mask = 0;
			for(size_t i = 0; i < 8; ++i)
				mask = (mask << 8) | _bytes[i];
			return mask;
		}

		std::optional<std::vector<uint8_t>> decodeMmPattern(const MessageView _message)
		{
			if(!validDump(MachineModel::Monomachine, _message, g_patternDump) || _message[9] >= 128)
				return std::nullopt;
			auto decoded = decodeMonomachinePayload(_message, g_mmPatternMaximum);
			if(!decoded || decoded->size() < g_mmPatternSize)
				return std::nullopt;
			const auto length = (*decoded)[g_mmPatternLengthIndex];
			if(length == 0 || length > MmPatternDump::StepCount)
				return std::nullopt;
			return decoded;
		}
	}

	bool MmPatternDump::hasTrig(const uint8_t _track, const uint8_t _step) const
	{
		return _track < TrackCount && _step < StepCount && ((trigs[_track] | ampTrigs[_track]) >> _step) & 1u;
	}

	std::optional<uint8_t> MmPatternDump::note(const uint8_t _track, const uint8_t _step) const
	{
		if(!hasTrig(_track, _step) || notes[_track][_step] >= 0x80)
			return std::nullopt;
		return notes[_track][_step];
	}

	std::optional<uint8_t> MmPatternDump::lock(const uint8_t _track, const uint8_t _bit, const uint8_t _step) const
	{
		if(_track >= TrackCount || _bit >= LockBitCount || _step >= StepCount || !((lockMasks[_track] >> _bit) & 1u))
			return std::nullopt;
		size_t row = 0;
		for(uint8_t track = 0; track < _track; ++track)
			row += std::bitset<64>(lockMasks[track]).count();
		row += std::bitset<64>(lockMasks[_track] & ((uint64_t{1} << _bit) - 1)).count();
		if(row >= lockRows.size() || lockRows[row][_step] >= 0x80)
			return std::nullopt;
		return lockRows[row][_step];
	}

	uint64_t MmPatternDump::lockedSteps(const uint8_t _track, const uint8_t _bit) const
	{
		uint64_t steps = 0;
		for(uint8_t step = 0; step < StepCount; ++step)
		{
			if(lock(_track, _bit, step))
				steps |= uint64_t{1} << step;
		}
		return steps;
	}

	std::optional<MmPatternDump> parseMmPatternDump(const MessageView _message)
	{
		const auto decoded = decodeMmPattern(_message);
		if(!decoded)
			return std::nullopt;
		const auto& data = *decoded;
		MmPatternDump result;
		result.slot = _message[9];
		result.length = data[g_mmPatternLengthIndex];
		result.doubleTempo = data[g_mmPatternLengthIndex + 1] != 0;
		result.kit = data[g_mmPatternLengthIndex + 2];
		size_t rows = 0;
		for(uint8_t track = 0; track < MmPatternDump::TrackCount; ++track)
		{
			result.ampTrigs[track] = readMask(&data[g_mmAmpTrigs + track * 8]);
			result.filterTrigs[track] = readMask(&data[g_mmFilterTrigs + track * 8]);
			result.lfoTrigs[track] = readMask(&data[g_mmLfoTrigs + track * 8]);
			result.trigs[track] = readMask(&data[g_mmTrigs + track * 8]);
			result.lockMasks[track] = readMask(&data[g_mmLockMasks + track * 8]);
			result.slides[track] = readMask(&data[g_mmSlides + track * 8]);
			result.swings[track] = readMask(&data[g_mmSwings + track * 8]);
			std::copy_n(&data[g_mmNotes + track * 64], 64, result.notes[track].begin());
			rows += std::bitset<64>(result.lockMasks[track]).count();
		}
		result.swingAmount = readMask32(&data[g_mmSwingAmount]);
		// As MCL reads them: a row per set bit, as many as the pattern holds
		result.lockRows.resize(std::min<size_t>(rows, MmPatternDump::LockRowCount));
		for(size_t row = 0; row < result.lockRows.size(); ++row)
			std::copy_n(&data[g_mmLockRows + row * 64], 64, result.lockRows[row].begin());
		return result;
	}

	std::optional<MmPatternEditor> MmPatternEditor::fromDump(const MessageView _message)
	{
		auto decoded = decodeMmPattern(_message);
		if(!decoded)
			return std::nullopt;
		MmPatternEditor editor;
		editor.m_header.assign(_message.begin(), _message.begin() + 10);
		editor.m_payload = std::move(*decoded);
		return editor;
	}

	uint64_t MmPatternEditor::mask(const size_t _offset) const
	{
		return readMask(&m_payload[_offset]);
	}

	void MmPatternEditor::setMask(const size_t _offset, uint64_t _mask)
	{
		for(size_t i = 8; i-- > 0;)
		{
			m_payload[_offset + i] = static_cast<uint8_t>(_mask & 0xff);
			_mask >>= 8;
		}
	}

	size_t MmPatternEditor::rowPosition(const size_t _row)
	{
		return g_mmLockRows + _row * 64;
	}

	size_t MmPatternEditor::rowIndex(const uint8_t _track, const uint8_t _bit) const
	{
		size_t row = 0;
		for(uint8_t track = 0; track < _track; ++track)
			row += std::bitset<64>(mask(g_mmLockMasks + track * 8)).count();
		return row + std::bitset<64>(mask(g_mmLockMasks + _track * 8) & ((uint64_t{1} << _bit) - 1)).count();
	}

	size_t MmPatternEditor::rowCount() const
	{
		return rowIndex(MmPatternDump::TrackCount - 1, 0) + std::bitset<64>(mask(g_mmLockMasks + 5 * 8)).count();
	}

	bool MmPatternEditor::setTrig(const uint8_t _track, const uint8_t _step, const std::optional<uint8_t> _note)
	{
		const auto length = m_payload[g_mmPatternLengthIndex];
		if(_track >= MmPatternDump::TrackCount || _step >= length || (_note && *_note >= 0x80))
			return false;
		const auto bit = uint64_t{1} << _step;
		for(const auto kind : {g_mmAmpTrigs, g_mmFilterTrigs, g_mmLfoTrigs, g_mmTrigs})
		{
			const auto offset = kind + _track * 8;
			setMask(offset, _note ? mask(offset) | bit : mask(offset) & ~bit);
		}
		m_payload[g_mmNotes + _track * 64 + _step] = _note ? *_note : MmPatternDump::None;
		if(!_note)
		{
			for(uint8_t lockBit = 0; lockBit < MmPatternDump::LockBitCount; ++lockBit)
				setLock(_track, lockBit, _step, std::nullopt);
		}
		return true;
	}

	bool MmPatternEditor::setLock(const uint8_t _track, const uint8_t _bit, const uint8_t _step,
		const std::optional<uint8_t> _value)
	{
		const auto length = m_payload[g_mmPatternLengthIndex];
		if(_track >= MmPatternDump::TrackCount || _bit >= MmPatternDump::LockBitCount || _step >= length
			|| (_value && *_value >= 0x80))
			return false;
		const auto maskOffset = g_mmLockMasks + _track * 8;
		const auto bit = uint64_t{1} << _bit;
		const bool hasRow = mask(maskOffset) & bit;
		const auto index = rowIndex(_track, _bit);
		const auto rowSize = static_cast<std::ptrdiff_t>(64);
		auto* rows = row(0);
		auto* end = row(MmPatternDump::LockRowCount);
		if(!_value)
		{
			if(!hasRow || index >= MmPatternDump::LockRowCount)
				return true;
			auto* values = row(index);
			values[_step] = MmPatternDump::None;
			if(std::all_of(values, values + 64, [](const uint8_t _v) { return _v >= 0x80; }))
			{
				// The row goes, the rows after it move up
				std::copy(values + rowSize, end, values);
				std::fill(end - rowSize, end, MmPatternDump::None);
				setMask(maskOffset, mask(maskOffset) & ~bit);
			}
		}
		else
		{
			const auto trigs = mask(g_mmTrigs + _track * 8) | mask(g_mmAmpTrigs + _track * 8);
			if(!((trigs >> _step) & 1u))
				return false;
			if(!hasRow)
			{
				if(rowCount() >= MmPatternDump::LockRowCount)
					return false;
				// A new row, the rows after it move down
				auto* values = rows + index * rowSize;
				std::copy_backward(values, end - rowSize, end);
				std::fill(values, values + rowSize, MmPatternDump::None);
				setMask(maskOffset, mask(maskOffset) | bit);
			}
			row(index)[_step] = *_value;
		}
		m_payload[g_mmLocksUsed] = static_cast<uint8_t>(std::min<size_t>(rowCount(), MmPatternDump::LockRowCount));
		return true;
	}

	bool MmPatternEditor::setLength(const uint8_t _length)
	{
		if(_length == 0 || _length > MmPatternDump::StepCount)
			return false;
		m_payload[g_mmPatternLengthIndex] = _length;
		return true;
	}

	void MmPatternEditor::clear()
	{
		// The 13 kinds of step masks but the swing ones (where the groove falls, as MCL leaves them), the lock
		// masks and rows, the notes
		const auto swingsAt = m_payload.begin() + g_mmSwings;
		const auto midiSwingsAt = m_payload.begin() + g_mmMidiSwings;
		const std::vector<uint8_t> swings(swingsAt, swingsAt + g_mmMaskSize);
		const std::vector<uint8_t> midiSwings(midiSwingsAt, midiSwingsAt + g_mmMaskSize);
		std::fill_n(m_payload.begin(), g_mmTrigKinds * g_mmMaskSize, uint8_t{0});
		std::copy(swings.begin(), swings.end(), m_payload.begin() + g_mmSwings);
		std::copy(midiSwings.begin(), midiSwings.end(), m_payload.begin() + g_mmMidiSwings);
		std::fill_n(m_payload.begin() + g_mmLockMasks, g_mmMaskSize, uint8_t{0});
		std::fill_n(m_payload.begin() + g_mmNotes, MmPatternDump::TrackCount * 64, MmPatternDump::None);
		std::fill_n(m_payload.begin() + g_mmLockRows, MmPatternDump::LockRowCount * 64, MmPatternDump::None);
		m_payload[g_mmLocksUsed] = 0;
	}

	bool MmPatternEditor::setFlag(const StepFlag _flag, const uint8_t _track, const uint8_t _step, const bool _on)
	{
		if((_flag != StepFlag::Slide && _flag != StepFlag::Swing) || _track >= MmPatternDump::TrackCount
			|| _step >= m_payload[g_mmPatternLengthIndex])
			return false;
		const auto offset = (_flag == StepFlag::Slide ? g_mmSlides : g_mmSwings) + _track * 8;
		const auto bit = uint64_t{1} << _step;
		setMask(offset, _on ? mask(offset) | bit : mask(offset) & ~bit);
		return true;
	}

	bool MmPatternEditor::setKit(const uint8_t _kit)
	{
		if(_kit >= 128)
			return false;
		m_payload[g_mmPatternLengthIndex + 2] = _kit;
		return true;
	}

	bool MmPatternEditor::setSwingAmount(const uint8_t _percent)
	{
		if(_percent < 50 || _percent > 80)
			return false;
		writeMask32(&m_payload[g_mmSwingAmount], swingWord(_percent));
		return true;
	}

	bool MmPatternEditor::setSlot(const uint8_t _slot)
	{
		if(_slot >= 128)
			return false;
		// The last byte of the header; the checksum, which counts it, is made again by toDump
		m_header.back() = _slot;
		return true;
	}

	Message MmPatternEditor::toDump() const
	{
		Message result(m_header);
		const auto packed = encodeMonomachinePayload(m_payload);
		result.insert(result.end(), packed.begin(), packed.end());
		finishDump(result);
		return result;
	}

	std::optional<Message> withMmPatternLength(const MessageView _message, const uint8_t _length)
	{
		auto editor = MmPatternEditor::fromDump(_message);
		if(!editor || !editor->setLength(_length))
			return std::nullopt;
		return editor->toDump();
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
		if(_message.size() == g_patternLongSize)
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
		if(_length == 0 || _length > 64)
			return false;
		// Over 32 steps, a dump of 32 steps takes the long form, its steps 33 to 64 without trigs, accents,
		// slides, swings or locks. A long dump stays long: the Machinedrum OS 1.63 sends every pattern in
		// the long form, takes both, and clears steps 33 to 64 of a pattern sent in the short form
		// (mdEditorFirmwareTest)
		if(_length > 32 && m_extension.empty())
		{
			m_extension.assign(g_patternExtensionSize, 0);
			std::fill_n(m_extension.begin() + g_patternExtensionLocks, g_patternRows * 32, g_noLock);
		}
		m_plain[1] = _length;
		return true;
	}

	void MdPatternEditor::clear()
	{
		for(uint8_t track = 0; track < 16; ++track)
		{
			for(uint8_t step = 0; step < stepCount(); ++step)
				trigByte(track, step) = 0;
		}
		m_masks.fill(0);
		for(size_t row = 0; row < g_patternRows; ++row)
		{
			for(uint8_t step = 0; step < stepCount(); ++step)
				lockValue(row, step) = g_noLock;
		}
		m_plain[5] = 0;
		// The accents and slides too, every track's and each track's; the swing steps stay, as MCL leaves them:
		// they say where the groove falls, the swing amount how much
		for(const auto flag : {StepFlag::Accent, StepFlag::Slide})
		{
			const auto kind = static_cast<uint8_t>(flag);
			writeMask32(&m_swing[kind * 4], 0);
			if(!m_extension.empty())
				writeMask32(&m_extension[g_patternExtensionFlags + kind * 4], 0);
			for(uint8_t track = 0; track < 16; ++track)
			{
				writeMask32(&m_tail[g_patternTrackFlags + (kind * 16 + track) * 4], 0);
				if(!m_extension.empty())
					writeMask32(&m_extension[g_patternExtensionTrackFlags + (kind * 16 + track) * 4], 0);
			}
		}
	}

	bool MdPatternEditor::setSlot(const uint8_t _slot)
	{
		if(_slot >= 128)
			return false;
		// The last byte of the header; the checksum, which counts it, is made again by toDump
		m_header.back() = _slot;
		return true;
	}

	bool MdPatternEditor::setKit(const uint8_t _kit)
	{
		if(_kit >= 64)
			return false;
		m_plain[4] = _kit;
		return true;
	}

	MdTrackSteps trackSteps(const PatternDump& _pattern, const uint8_t _track)
	{
		MdTrackSteps steps;
		if(_track >= 16)
			return steps;
		steps.trigs = _pattern.trigs[_track];
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
			steps.flags[flag] = _pattern.trackFlags[flag][_track];
		for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
		{
			for(uint8_t step = 0; step < _pattern.steps; ++step)
			{
				if(const auto value = _pattern.lock(_track, parameter, step))
					steps.locks[parameter][step] = *value;
			}
		}
		return steps;
	}

	bool MdPatternEditor::setTrackSteps(const uint8_t _track, const MdTrackSteps& _steps)
	{
		if(_track >= 16)
			return false;
		const auto steps = stepCount();
		// The parameters locked on a step with a trig, among the steps the dump holds
		std::array<bool, g_patternParameters> locked{};
		size_t needed = 0;
		size_t own = 0;
		for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
		{
			for(uint8_t step = 0; step < steps && !locked[parameter]; ++step)
				locked[parameter] = ((_steps.trigs >> step) & 1u) && _steps.locks[parameter][step] < 0x80;
			needed += locked[parameter] ? 1 : 0;
			own += hasRow(_track, parameter) ? 1 : 0;
		}
		if(rowCount() - own + needed > g_patternRows)
			return false;
		// The track's own rows go
		for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
		{
			if(!hasRow(_track, parameter))
				continue;
			removeRow(rowIndex(_track, parameter));
			maskByte(_track, parameter) &= static_cast<uint8_t>(~(1u << (parameter % 8)));
		}
		// Its trigs and its own flags: steps 1 to 32, then 33 to 64 in the long form
		const auto low = [](const uint64_t _mask) { return static_cast<uint32_t>(_mask); };
		const auto high = [](const uint64_t _mask) { return static_cast<uint32_t>(_mask >> 32); };
		writeMask32(&m_trigs[_track * 4], low(_steps.trigs));
		if(!m_extension.empty())
			writeMask32(&m_extension[_track * 4], high(_steps.trigs));
		for(uint8_t flag = 0; flag < StepFlagCount; ++flag)
		{
			const auto word = (flag * 16 + _track) * 4;
			writeMask32(&m_tail[g_patternTrackFlags + word], low(_steps.flags[flag]));
			if(!m_extension.empty())
				writeMask32(&m_extension[g_patternExtensionTrackFlags + word], high(_steps.flags[flag]));
		}
		// A row per locked parameter, in track then parameter order, its values on the steps with a trig
		for(uint8_t parameter = 0; parameter < g_patternParameters; ++parameter)
		{
			if(!locked[parameter])
				continue;
			const auto row = rowIndex(_track, parameter);
			insertRow(row);
			maskByte(_track, parameter) |= static_cast<uint8_t>(1u << (parameter % 8));
			for(uint8_t step = 0; step < steps; ++step)
				lockValue(row, step) = ((_steps.trigs >> step) & 1u) ? _steps.locks[parameter][step] : g_noLock;
		}
		m_plain[5] = static_cast<uint8_t>(std::min(rowCount(), g_patternRows));
		return true;
	}

	bool MdPatternEditor::setFlag(const StepFlag _flag, const std::optional<uint8_t> _track, const uint8_t _step,
		const bool _on)
	{
		const auto flag = static_cast<uint8_t>(_flag);
		if(flag >= StepFlagCount || (_track && *_track >= 16) || _step >= std::min(length(), stepCount()))
			return false;
		// Steps 1 to 32 in the masks' first words, 33 to 64 in the long form's
		uint8_t* word = nullptr;
		const auto trackWord = _track ? (flag * 16 + *_track) * 4 : 0;
		if(_step < 32)
			word = _track ? &m_tail[g_patternTrackFlags + trackWord] : &m_swing[flag * 4];
		else if(_track)
			word = &m_extension[g_patternExtensionTrackFlags + trackWord];
		else
			word = &m_extension[g_patternExtensionFlags + flag * 4];
		const auto bit = uint32_t{1} << (_step % 32);
		writeMask32(word, _on ? readMask32(word) | bit : readMask32(word) & ~bit);
		return true;
	}

	bool MdPatternEditor::setFlagPerTrack(const StepFlag _flag, const bool _perTrack)
	{
		const auto flag = static_cast<uint8_t>(_flag);
		if(flag >= StepFlagCount)
			return false;
		writeMask32(&m_tail[flag * 4], _perTrack ? 0 : 1);
		return true;
	}

	bool MdPatternEditor::setAccentAmount(const uint8_t _amount)
	{
		if(_amount >= 0x80)
			return false;
		m_plain[0] = _amount;
		return true;
	}

	bool MdPatternEditor::setSwingAmount(const uint8_t _percent)
	{
		if(_percent < 50 || _percent > 80)
			return false;
		setSwingWord(swingWord(_percent));
		return true;
	}

	void MdPatternEditor::setSwingWord(const uint32_t _word)
	{
		writeMask32(&m_swing[12], _word);
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

	Message kitLoad(const MachineModel _model, const uint8_t _slot)
	{
		return request(_model, g_kitLoad, _slot);
	}

	Message patternSelect(const MachineModel _model, const uint8_t _slot)
	{
		return {0xf0, 0x00, 0x20, 0x3c, product(_model), 0x00, g_setStatus,
			static_cast<uint8_t>(StatusParameter::Pattern), static_cast<uint8_t>(_slot & 0x7f), 0xf7};
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

	MasterEffects masterEffectsFromKit(const uint8_t* _bytes)
	{
		MasterEffects values{};
		for(uint8_t effect = 0; effect < MasterEffectCount; ++effect)
			std::copy_n(_bytes + g_mdMasterEffectBlock[effect] * MasterEffectParameters, MasterEffectParameters, values[effect].begin());
		return values;
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

			// _bytes bytes packed in 7-bit groups from _position (a byte of top bits, MSB first, then up
			// to seven bytes); empty when the dump is too short to hold them
			const auto unpack = [&](const size_t _position, const size_t _bytes)
			{
				std::vector<uint8_t> raw;
				if(_message.size() < _position + _bytes + (_bytes + 6) / 7 + 5)
					return raw;
				raw.reserve(_bytes);
				for(size_t position = _position; raw.size() < _bytes;)
				{
					const auto highBits = _message[position++];
					for(uint8_t bit = 0; bit < 7 && raw.size() < _bytes; ++bit)
					{
						auto value = _message[position++];
						if(highBits & (1u << (6u - bit)))
							value |= 0x80;
						raw.push_back(value);
					}
				}
				return raw;
			};

			// Machine assignments follow the levels: 16 big-endian 32-bit values. The id
			// is the low byte; the upper bits carry flags such as TONAL.
			constexpr size_t machinePosition = 0x1aa;
			constexpr size_t machineBytes = machinedrum::TrackCount * 4;
			std::vector<uint16_t> machines;
			if(const auto raw = unpack(machinePosition, machineBytes); !raw.empty())
			{
				for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
					machines.push_back(raw[track * 4 + 3]);
			}
			// Then the LFOs, 36 bytes a track: the five fields $62 sets, then the LFO's state
			constexpr size_t lfoPosition = machinePosition + machineBytes + (machineBytes + 6) / 7;
			constexpr size_t lfoBytes = 36;
			std::optional<std::array<LfoSettings, machinedrum::TrackCount>> lfos;
			if(const auto raw = unpack(lfoPosition, machinedrum::TrackCount * lfoBytes); !raw.empty())
			{
				lfos.emplace();
				for(uint8_t track = 0; track < machinedrum::TrackCount; ++track)
				{
					const auto* lfo = &raw[track * lfoBytes];
					(*lfos)[track] = {lfo[0], lfo[1], lfo[2], lfo[3], lfo[4]};
				}
			}
			std::optional<MasterEffects> effects;
			if(_message.size() == g_mdKitSize)
				effects = masterEffectsFromKit(&_message[g_mdMasterEffectsPosition]);
			// 16 raw bytes between the slot and the parameters
			constexpr size_t namePosition = 0x0a;
			return KitDump{slot, std::move(result), std::move(machines),
				kitName(&_message[namePosition], parameterPosition - namePosition), effects, lfos};
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

	std::optional<Message> lfoChange(const uint8_t _track, const uint8_t _field, const uint8_t _value)
	{
		constexpr uint8_t maximum[] = {machinedrum::TrackCount - 1, 23, LfoSettings::ShapeCount - 1,
			LfoSettings::ShapeCount - 1, LfoSettings::UpdateCount - 1};
		if(_track >= machinedrum::TrackCount || _field >= std::size(maximum) || _value > maximum[_field])
			return std::nullopt;
		return Message{0xf0, 0x00, 0x20, 0x3c, product(MachineModel::Machinedrum), 0x00, g_lfoChange,
			static_cast<uint8_t>(_track << 3 | _field), _value, 0xf7};
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
