#include "mdkit.h"

#include "mdsysexcodec.h"

#include <algorithm>

namespace md::automation::sysex
{
	using namespace codec;

	namespace
	{
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_linkChange = 0x65;		// SET TRIG GROUP
		constexpr uint8_t g_chokeChange = 0x66;		// SET MUTE GROUP
		constexpr uint8_t g_version = 0x04;
		constexpr uint8_t g_revision = 0x01;
		constexpr uint8_t g_offOnTheWire = 0x7f;

		constexpr size_t g_tracks = machinedrum::TrackCount;

		// The record (patch memory) ...
		constexpr size_t g_recordName = 0x000;
		constexpr size_t g_recordParameters = 0x010;
		constexpr size_t g_recordLevels = 0x190;
		constexpr size_t g_recordMachines = 0x1a0;
		constexpr size_t g_recordLfos = 0x1e0;
		constexpr size_t g_recordMasterEffects = 0x420;
		constexpr size_t g_recordLinks = 0x440;
		constexpr size_t g_recordChokes = 0x450;

		// ... and the dump, whose packed fields take one byte more per seven
		constexpr size_t packedSize(const size_t _bytes) { return _bytes + (_bytes + 6) / 7; }
		constexpr size_t g_dumpName = 0x00a;
		constexpr size_t g_dumpParameters = 0x01a;
		constexpr size_t g_dumpLevels = 0x19a;
		constexpr size_t g_dumpMachines = 0x1aa;
		constexpr size_t g_dumpLfos = g_dumpMachines + packedSize(g_tracks * 4);
		constexpr size_t g_dumpMasterEffects = g_dumpLfos + packedSize(g_tracks * MdKit::LfoSize);
		constexpr size_t g_dumpGroups = g_dumpMasterEffects + MdKit::MasterEffectsSize;
		constexpr size_t g_dumpEnd = g_dumpGroups + packedSize(g_tracks * 2);
		static_assert(g_dumpMasterEffects == 0x487 && g_dumpGroups == 0x4a7 && g_dumpEnd + 5 == MdKitDumpSize);

		// The LFO state word OS 1.63 writes into every LFO it initialises ($207a62, $23dcc2)
		constexpr std::array<uint8_t, 4> g_lfoStateWord{0x00, 0x00, 0x02, 0x9a};
		constexpr size_t g_lfoStateWordAt = 28;

		uint32_t readBigEndian(const uint8_t* _bytes)
		{
			return static_cast<uint32_t>(_bytes[0]) << 24 | static_cast<uint32_t>(_bytes[1]) << 16
				| static_cast<uint32_t>(_bytes[2]) << 8 | _bytes[3];
		}

		void writeBigEndian(uint8_t* _bytes, const uint32_t _value)
		{
			_bytes[0] = static_cast<uint8_t>(_value >> 24);
			_bytes[1] = static_cast<uint8_t>(_value >> 16);
			_bytes[2] = static_cast<uint8_t>(_value >> 8);
			_bytes[3] = static_cast<uint8_t>(_value);
		}

		void appendMasked(Message& _out, const uint8_t* _data, const size_t _count)
		{
			for(size_t i = 0; i < _count; ++i)
				_out.push_back(static_cast<uint8_t>(_data[i] & 0x7f));
		}

		std::optional<Message> groupChange(const uint8_t _command, const uint8_t _track, const uint8_t _target)
		{
			if(_track >= g_tracks || (_target >= g_tracks && _target != MdKit::Off))
				return std::nullopt;
			return Message{0xf0, 0x00, 0x20, 0x3c, product(MachineModel::Machinedrum), 0x00, _command, _track,
				_target == MdKit::Off ? g_offOnTheWire : _target, 0xf7};
		}
	}

	MdKit::MdKit()
	{
		for(auto& lfo : lfos)
			std::copy(g_lfoStateWord.begin(), g_lfoStateWord.end(), lfo.begin() + g_lfoStateWordAt);
		links.fill(Off);
		chokes.fill(Off);
	}

	MdKit MdKit::fromRecord(const uint8_t* _record)
	{
		MdKit kit;
		std::copy_n(_record + g_recordName, NameSize, kit.name.begin());
		for(size_t t = 0; t < g_tracks; ++t)
		{
			std::copy_n(_record + g_recordParameters + t * ParameterCount, ParameterCount, kit.parameters[t].begin());
			kit.machines[t] = readBigEndian(_record + g_recordMachines + t * 4);
			std::copy_n(_record + g_recordLfos + t * LfoSize, LfoSize, kit.lfos[t].begin());
		}
		std::copy_n(_record + g_recordLevels, g_tracks, kit.levels.begin());
		std::copy_n(_record + g_recordMasterEffects, MasterEffectsSize, kit.masterEffects.begin());
		std::copy_n(_record + g_recordLinks, g_tracks, kit.links.begin());
		std::copy_n(_record + g_recordChokes, g_tracks, kit.chokes.begin());
		return kit;
	}

	void MdKit::toRecord(uint8_t* _record) const
	{
		std::copy(name.begin(), name.end(), _record + g_recordName);
		for(size_t t = 0; t < g_tracks; ++t)
		{
			std::copy(parameters[t].begin(), parameters[t].end(), _record + g_recordParameters + t * ParameterCount);
			writeBigEndian(_record + g_recordMachines + t * 4, machines[t]);
			std::copy(lfos[t].begin(), lfos[t].end(), _record + g_recordLfos + t * LfoSize);
		}
		std::copy(levels.begin(), levels.end(), _record + g_recordLevels);
		std::copy(masterEffects.begin(), masterEffects.end(), _record + g_recordMasterEffects);
		std::copy(links.begin(), links.end(), _record + g_recordLinks);
		std::copy(chokes.begin(), chokes.end(), _record + g_recordChokes);
	}

	std::string MdKit::displayName() const
	{
		std::string result;
		for(const auto c : name)
		{
			if(c == 0)
				break;
			result.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?');
		}
		while(!result.empty() && result.back() == ' ')
			result.pop_back();
		return result;
	}

	LfoSettings MdKit::lfo(const uint8_t _track) const
	{
		const auto& block = lfos[_track];
		return {block[0], block[1], block[2], block[3], block[4]};
	}

	void MdKit::setLfo(const uint8_t _track, const LfoSettings& _lfo)
	{
		auto& block = lfos[_track];
		block[0] = _lfo.track;
		block[1] = _lfo.parameter;
		block[2] = _lfo.shape1;
		block[3] = _lfo.shape2;
		block[4] = _lfo.update;
	}

	bool MdKit::operator==(const MdKit& _other) const
	{
		return name == _other.name && parameters == _other.parameters && levels == _other.levels
			&& machines == _other.machines && lfos == _other.lfos && masterEffects == _other.masterEffects
			&& links == _other.links && chokes == _other.chokes;
	}

	Message mdKitDump(const MdKit& _kit, const uint8_t _slot)
	{
		Message dump{0xf0, 0x00, 0x20, 0x3c, product(MachineModel::Machinedrum), 0x00, g_kitDump, g_version,
			g_revision, static_cast<uint8_t>(_slot & 0x7f)};
		dump.reserve(MdKitDumpSize);

		std::array<uint8_t, MdKit::RecordSize> record{};
		_kit.toRecord(record.data());

		appendMasked(dump, &record[g_recordName], MdKit::NameSize);
		appendMasked(dump, &record[g_recordParameters], g_tracks * MdKit::ParameterCount);
		appendMasked(dump, &record[g_recordLevels], g_tracks);
		append7Bit(dump, &record[g_recordMachines], g_tracks * 4);
		append7Bit(dump, &record[g_recordLfos], g_tracks * MdKit::LfoSize);
		appendMasked(dump, &record[g_recordMasterEffects], MdKit::MasterEffectsSize);
		// Links then Chokes, one run of 32 bytes
		append7Bit(dump, &record[g_recordLinks], g_tracks * 2);
		finishDump(dump);
		return dump;
	}

	std::optional<MdKit> parseMdKit(const MessageView _message, uint8_t* _slot)
	{
		if(_message.size() != MdKitDumpSize || !validDump(MachineModel::Machinedrum, _message, g_kitDump))
			return std::nullopt;
		const auto version = _message[7];
		const auto slot = _message[9];
		if(version < 3 || version > 4 || slot >= 64)
			return std::nullopt;

		std::array<uint8_t, MdKit::RecordSize> record{};
		std::copy_n(&_message[g_dumpName], MdKit::NameSize, &record[g_recordName]);
		std::copy_n(&_message[g_dumpParameters], g_tracks * MdKit::ParameterCount, &record[g_recordParameters]);
		std::copy_n(&_message[g_dumpLevels], g_tracks, &record[g_recordLevels]);
		std::copy_n(&_message[g_dumpMasterEffects], MdKit::MasterEffectsSize, &record[g_recordMasterEffects]);

		size_t position = g_dumpMachines;
		if(!read7Bit(_message, position, g_tracks * 4, &record[g_recordMachines])
			|| !read7Bit(_message, position, g_tracks * MdKit::LfoSize, &record[g_recordLfos]))
			return std::nullopt;
		position = g_dumpGroups;
		if(!read7Bit(_message, position, g_tracks * 2, &record[g_recordLinks]) || position != g_dumpEnd)
			return std::nullopt;

		if(_slot)
			*_slot = slot;
		return MdKit::fromRecord(record.data());
	}

	std::optional<Message> linkChange(const uint8_t _track, const uint8_t _target)
	{
		return groupChange(g_linkChange, _track, _target);
	}

	std::optional<Message> chokeChange(const uint8_t _track, const uint8_t _target)
	{
		return groupChange(g_chokeChange, _track, _target);
	}
}
