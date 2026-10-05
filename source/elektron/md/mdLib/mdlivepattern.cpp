#include "mdlivepattern.h"

namespace md
{
	namespace
	{
		// A dump's header ends at the pattern's number; its checksum, length and F7 follow the payload
		constexpr size_t g_headerSize = 0x0a;
		constexpr size_t g_trailerSize = 5;
		constexpr size_t g_longDumpSize = 0x1522;

		// _count bytes packed in 7-bit groups (a byte of top bits, MSB first, then up to seven bytes)
		bool unpack7Bit(const automation::sysex::MessageView _dump, size_t& _position, const size_t _count,
			std::vector<uint8_t>& _out)
		{
			for(size_t done = 0; done < _count;)
			{
				if(_position >= _dump.size())
					return false;
				const auto highBits = _dump[_position++];
				for(uint8_t bit = 0; bit < 7 && done < _count; ++bit, ++done)
				{
					if(_position >= _dump.size())
						return false;
					const auto high = (highBits >> (6u - bit)) & 1u;
					_out.push_back(static_cast<uint8_t>(_dump[_position++] | (high << 7)));
				}
			}
			return true;
		}
	}

	std::optional<std::vector<uint8_t>> unpackMdPatternPayload(const automation::sysex::MessageView _dump)
	{
		if(!automation::sysex::parseMdPatternDump(_dump))
			return std::nullopt;
		std::vector<uint8_t> payload;
		payload.reserve(LivePatternLayout::MainSize + LivePatternLayout::TailSize + LivePatternLayout::ExtensionSize);
		size_t position = g_headerSize;
		// Trigs, lock masks, accent/slide/swing, then six plain bytes (accent amount to row count), the lock rows
		if(!unpack7Bit(_dump, position, 64, payload) || !unpack7Bit(_dump, position, 64, payload)
			|| !unpack7Bit(_dump, position, 16, payload) || position + 6 > _dump.size())
			return std::nullopt;
		payload.insert(payload.end(), _dump.begin() + position, _dump.begin() + position + 6);
		position += 6;
		if(!unpack7Bit(_dump, position, 64 * 32, payload)
			|| !unpack7Bit(_dump, position, LivePatternLayout::TailSize, payload))
			return std::nullopt;
		if(_dump.size() == g_longDumpSize && !unpack7Bit(_dump, position, LivePatternLayout::ExtensionSize, payload))
			return std::nullopt;
		if(position != _dump.size() - g_trailerSize)
			return std::nullopt;
		return payload;
	}

	std::optional<std::vector<RamWrite>> livePatternWrites(const LivePatternLayout& _layout,
		const automation::sysex::MessageView _from, const automation::sysex::MessageView _to)
	{
		const auto fromPattern = automation::sysex::parseMdPatternDump(_from);
		const auto toPattern = automation::sysex::parseMdPatternDump(_to);
		if(!fromPattern || !toPattern || fromPattern->slot != toPattern->slot
			|| toPattern->slot >= LivePatternLayout::PatternCount)
			return std::nullopt;
		const auto slot = toPattern->slot;
		const auto from = unpackMdPatternPayload(_from);
		const auto to = unpackMdPatternPayload(_to);
		if(!from || !to)
			return std::nullopt;

		std::vector<RamWrite> writes;
		const auto write = [&](const size_t _offset, const uint32_t _size, const uint32_t _address, const bool _all)
		{
			for(uint32_t i = 0; i < _size; ++i)
			{
				const auto value = (*to)[_offset + i];
				if(_all)
					writes.push_back({_address + i, value, std::nullopt});
				else if((*from)[_offset + i] != value)
					writes.push_back({_address + i, value, (*from)[_offset + i]});
			}
		};
		constexpr size_t tailOffset = LivePatternLayout::MainSize;
		constexpr size_t extensionOffset = tailOffset + LivePatternLayout::TailSize;
		write(0, LivePatternLayout::MainSize, _layout.mainOf(slot), false);
		write(tailOffset, LivePatternLayout::TailSize, _layout.tailOf(slot), false);
		// Steps 33 to 64: what the RAM holds there is not known when _from has none
		if(to->size() > extensionOffset)
			write(extensionOffset, LivePatternLayout::ExtensionSize, _layout.extensionOf(slot), from->size() <= extensionOffset);
		return writes;
	}

	uint32_t LivePatternControl::request(automation::sysex::Message _from, automation::sysex::Message _to)
	{
		const std::lock_guard lock(m_mutex);
		m_waiting.push_back({++m_lastId, std::move(_from), std::move(_to)});
		return m_lastId;
	}

	std::optional<LivePatternControl::Edit> LivePatternControl::take()
	{
		const std::unique_lock lock(m_mutex, std::try_to_lock);
		if(!lock.owns_lock() || m_waiting.empty())
			return std::nullopt;
		auto edit = std::move(m_waiting.front());
		m_waiting.pop_front();
		return edit;
	}
}
