#include "mdDrumsBank.h"

#include <fstream>
#include <iterator>

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
			std::filesystem::create_directories(m_file.parent_path(), error);
			std::ofstream out(m_file, std::ios::binary);
			for(int slot = 0; slot < SlotCount; ++slot)
			{
				const auto kit = static_cast<size_t>(slot) < m_factory.size() ? std::optional<Kit>(m_factory[static_cast<size_t>(slot)])
					: std::nullopt;
				const auto bytes = dump(kit, slot);
				out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			}
			if(!out)
				return false;
		}
		std::ifstream in(m_file, std::ios::binary);
		if(!in)
			return false;
		const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		for(int slot = 0; slot < SlotCount; ++slot)
		{
			const auto at = static_cast<size_t>(slot) * SlotSize;
			if(at + SlotSize > bytes.size())
				break;
			const std::vector<uint8_t> message(bytes.begin() + static_cast<std::ptrdiff_t>(at),
				bytes.begin() + static_cast<std::ptrdiff_t>(at + SlotSize));
			auto kit = sysex::parseMdKit(message);
			if(kit && !kit->isEmptySlot())
				m_slots[static_cast<size_t>(slot)] = std::move(*kit);
		}
		return true;
	}

	const std::optional<Bank::Kit>& Bank::slot(const int _slot) const
	{
		static const std::optional<Kit> none;
		return _slot >= 0 && _slot < SlotCount ? m_slots[static_cast<size_t>(_slot)] : none;
	}

	bool Bank::write(const int _slot, const std::optional<Kit>& _kit)
	{
		if(_slot < 0 || _slot >= SlotCount || !read())
			return false;
		// The file holds every Slot by now: its Slot's bytes rewritten in place
		std::fstream file(m_file, std::ios::binary | std::ios::in | std::ios::out);
		if(!file)
			return false;
		std::error_code error;
		const auto size = std::filesystem::file_size(m_file, error);
		const auto at = static_cast<std::streamoff>(static_cast<size_t>(_slot) * SlotSize);
		// A short file (written by another tool) is padded with empty Slots up to this one
		for(auto slot = static_cast<int>(error ? 0 : size / SlotSize); slot < _slot; ++slot)
		{
			const auto empty = dump(std::nullopt, slot);
			file.seekp(static_cast<std::streamoff>(static_cast<size_t>(slot) * SlotSize));
			file.write(reinterpret_cast<const char*>(empty.data()), static_cast<std::streamsize>(empty.size()));
		}
		const auto bytes = dump(_kit, _slot);
		file.seekp(at);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if(!file)
			return false;
		file.close();
		m_slots[static_cast<size_t>(_slot)] = _kit && !_kit->isEmptySlot() ? _kit : std::nullopt;
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
			const std::vector<uint8_t> message(_bytes.begin() + static_cast<std::ptrdiff_t>(at),
				_bytes.begin() + static_cast<std::ptrdiff_t>(end + 1));
			if(auto kit = sysex::parseMdKit(message); kit && !kit->isEmptySlot())
				kits.push_back(std::move(*kit));
			at = end;
		}
		return kits;
	}
}
