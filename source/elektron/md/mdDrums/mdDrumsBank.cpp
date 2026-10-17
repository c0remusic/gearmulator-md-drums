#include "mdDrumsBank.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <fstream>

namespace mdDrums
{
	namespace sysex = md::automation::sysex;

	Bank::Bank(std::filesystem::path _file, std::vector<Kit> _factory)
		: m_file(std::move(_file)), m_factory(std::move(_factory))
	{
	}

	bool Bank::read()
	{
		m_slots.fill(std::nullopt);
		std::error_code error;
		if(!std::filesystem::exists(m_file, error))
		{
			// A new Bank whole or not at all: another instance creating it at the same moment keeps its own
			std::filesystem::create_directories(m_file.parent_path(), error);
			std::vector<uint8_t> bytes;
			bytes.reserve(static_cast<size_t>(SlotCount) * SlotSize);
			for(int slot = 0; slot < SlotCount; ++slot)
			{
				const auto s = static_cast<size_t>(slot);
				const auto kit = dump(s < m_factory.size() ? std::optional<Kit>(m_factory[s]) : std::nullopt, slot);
				bytes.insert(bytes.end(), kit.begin(), kit.end());
			}
			const auto written = baseLib::filesystem::writeFileExclusive(m_file.u8string(), bytes);
			if(!written && !std::filesystem::exists(m_file, error))
				return false;
		}
		std::vector<uint8_t> bytes;
		if(!baseLib::filesystem::readFile(bytes, m_file.u8string()))
			return false;
		for(int slot = 0; slot < SlotCount; ++slot)
		{
			const auto at = static_cast<size_t>(slot) * SlotSize;
			if(at + SlotSize > bytes.size())
				break;
			auto kit = sysex::parseMdKit({bytes.data() + at, SlotSize});
			if(kit && !kit->isEmptySlot())
				m_slots[static_cast<size_t>(slot)] = std::move(*kit);
		}
		m_seen = std::filesystem::last_write_time(m_file, error);
		return true;
	}

	bool Bank::follow()
	{
		std::error_code error;
		const auto time = std::filesystem::last_write_time(m_file, error);
		if(error || time == m_seen)
			return false;
		return read();
	}

	const std::optional<Bank::Kit>& Bank::slot(const int _slot) const
	{
		static const std::optional<Kit> none;
		return _slot >= 0 && _slot < SlotCount ? m_slots[static_cast<size_t>(_slot)] : none;
	}

	bool Bank::write(const std::vector<SlotKit>& _slots)
	{
		int last = 0;
		for(const auto& entry : _slots)
		{
			if(entry.first < 0 || entry.first >= SlotCount)
				return false;
			last = std::max(last, entry.first);
		}
		if(!read())
			return false;
		// The file holds every Slot by now: the Slots' bytes rewritten in place
		std::fstream file(m_file, std::ios::binary | std::ios::in | std::ios::out);
		if(!file)
			return false;
		const auto put = [&file](const int _slot, const std::optional<Kit>& _kit)
		{
			const auto bytes = dump(_kit, _slot);
			file.seekp(static_cast<std::streamoff>(static_cast<size_t>(_slot) * SlotSize));
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		};
		std::error_code error;
		const auto size = std::filesystem::file_size(m_file, error);
		// A short file (written by another tool) is padded with empty Slots up to the last one written
		for(auto slot = static_cast<int>(error ? 0 : size / SlotSize); slot < last; ++slot)
			put(slot, std::nullopt);
		for(const auto& [slot, kit] : _slots)
			put(slot, kit);
		if(!file)
			return false;
		file.close();
		for(const auto& [slot, kit] : _slots)
			m_slots[static_cast<size_t>(slot)] = kit && !kit->isEmptySlot() ? kit : std::nullopt;
		m_seen = std::filesystem::last_write_time(m_file, error);
		return true;
	}

	std::vector<uint8_t> Bank::dump(const std::optional<Kit>& _kit, const int _slot)
	{
		if(_kit)
			return sysex::mdKitDump(*_kit, static_cast<uint8_t>(_slot));
		Kit empty;
		empty.name[0] = 0x7f;
		return sysex::mdKitDump(empty, static_cast<uint8_t>(_slot));
	}

	std::vector<Bank::Kit> Bank::parseSyx(const std::vector<uint8_t>& _bytes)
	{
		std::vector<Kit> kits;
		for(size_t at = 0; at < _bytes.size(); ++at)
		{
			if(_bytes[at] != 0xf0)
				continue;
			size_t end = at + 1;
			while(end < _bytes.size() && _bytes[end] != 0xf7)
				++end;
			if(end == _bytes.size())
				break;
			if(auto kit = sysex::parseMdKit({_bytes.data() + at, end + 1 - at}); kit && !kit->isEmptySlot())
				kits.push_back(std::move(*kit));
			at = end;
		}
		return kits;
	}
}
