#include "mdkit.h"

#include "mdkitchange.h"
#include "mdsysexcodec.h"

#include <algorithm>

namespace md::automation::sysex
{
	using namespace codec;

	namespace
	{
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_version = 0x04;
		constexpr uint8_t g_revision = 0x01;

		constexpr size_t g_tracks = machinedrum::TrackCount;

		// The record (patch memory); the dump's layout is mdKitDumpLayout
		constexpr size_t g_recordName = 0x000;
		constexpr size_t g_recordParameters = 0x010;
		constexpr size_t g_recordLevels = 0x190;
		constexpr size_t g_recordMachines = 0x1a0;
		constexpr size_t g_recordLfos = 0x1e0;
		constexpr size_t g_recordMasterEffects = 0x420;
		constexpr size_t g_recordLinks = 0x440;
		constexpr size_t g_recordChokes = 0x450;

		// The LFO state word OS 1.63 writes into every LFO it initialises ($207a62, $23dcc2)
		constexpr std::array<uint8_t, 4> g_lfoStateWord{0x00, 0x00, 0x02, 0x9a};
		constexpr size_t g_lfoStateWordAt = 28;

		void appendMasked(Message& _out, const uint8_t* _data, const size_t _count)
		{
			for(size_t i = 0; i < _count; ++i)
				_out.push_back(static_cast<uint8_t>(_data[i] & 0x7f));
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
			kit.machines[t] = readBigEndian32(_record + g_recordMachines + t * 4);
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
			writeBigEndian32(_record + g_recordMachines + t * 4, machines[t]);
			std::copy(lfos[t].begin(), lfos[t].end(), _record + g_recordLfos + t * LfoSize);
		}
		std::copy(levels.begin(), levels.end(), _record + g_recordLevels);
		std::copy(masterEffects.begin(), masterEffects.end(), _record + g_recordMasterEffects);
		std::copy(links.begin(), links.end(), _record + g_recordLinks);
		std::copy(chokes.begin(), chokes.end(), _record + g_recordChokes);
	}

	std::string MdKit::displayName() const
	{
		return kitName(name.data(), NameSize);
	}

	void MdKit::setName(const std::string_view _name)
	{
		// The Machinedrum's names: 16 characters of 7 bits
		name.fill(0);
		size_t length = 0;
		for(const auto c : _name)
		{
			if(length == NameSize)
				break;
			const auto byte = static_cast<uint8_t>(c);
			if(byte >= 0x20 && byte <= 0x7e)
				name[length++] = byte;
		}
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
		auto dump = header(MachineModel::Machinedrum, g_kitDump, MdKitDumpSize);
		dump.insert(dump.end(), {g_version, g_revision, static_cast<uint8_t>(_slot & 0x7f)});

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
		std::copy_n(&_message[mdKitDumpLayout::Name], MdKit::NameSize, &record[g_recordName]);
		std::copy_n(&_message[mdKitDumpLayout::Parameters], g_tracks * MdKit::ParameterCount,
			&record[g_recordParameters]);
		std::copy_n(&_message[mdKitDumpLayout::Levels], g_tracks, &record[g_recordLevels]);
		std::copy_n(&_message[mdKitDumpLayout::MasterEffects], MdKit::MasterEffectsSize,
			&record[g_recordMasterEffects]);

		size_t position = mdKitDumpLayout::Machines;
		if(!read7Bit(_message, position, g_tracks * 4, &record[g_recordMachines])
			|| !read7Bit(_message, position, g_tracks * MdKit::LfoSize, &record[g_recordLfos]))
			return std::nullopt;
		position = mdKitDumpLayout::Groups;
		if(!read7Bit(_message, position, g_tracks * 2, &record[g_recordLinks]) || position != mdKitDumpLayout::End)
			return std::nullopt;

		if(_slot)
			*_slot = slot;
		return MdKit::fromRecord(record.data());
	}

	std::optional<Message> linkChange(const uint8_t _track, const uint8_t _target)
	{
		return kitChangeMessage({KitChange::Kind::Link, _track, 0, _target});
	}

	std::optional<Message> chokeChange(const uint8_t _track, const uint8_t _target)
	{
		return kitChangeMessage({KitChange::Kind::Choke, _track, 0, _target});
	}
}
