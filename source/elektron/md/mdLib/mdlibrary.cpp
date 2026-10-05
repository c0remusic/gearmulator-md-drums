#include "mdlibrary.h"

namespace md
{
	std::string Library::name(const uint8_t _kit) const
	{
		std::string text;
		if(_kit >= kitCount)
			return text;
		const auto& bytes = kits[_kit].name;
		for(size_t index = 0; index < nameSize && bytes[index] != 0; ++index)
			text.push_back(bytes[index] >= 0x20 && bytes[index] < 0x7f ? static_cast<char>(bytes[index]) : '?');
		while(!text.empty() && text.back() == ' ')
			text.pop_back();
		return text;
	}

	bool LibraryControl::publish(const uint32_t _id, const Library& _library)
	{
		const std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock())
			return false;
		m_library = _library;
		m_readId.store(_id, std::memory_order_release);
		return true;
	}

	Library LibraryControl::read(uint32_t& _id) const
	{
		const std::lock_guard lock(m_mutex);
		_id = m_readId.load(std::memory_order_acquire);
		return m_library;
	}
}
