#pragma once

#include "mdsysexautomation.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

// What every Elektron message shares: the header, the 7-bit packing of bytes with their top bit set, the big-endian
// words, the checksum and the length before F7, the names. Used by the codecs of mdsysexautomation.cpp and mdkit.cpp,
// and by MD Drums' Device and messages (mdDrumsDevice.cpp, mdDrumsMessages.cpp).
namespace md::automation::sysex::codec
{
	// The product byte after F0 00 20 3C: 02 the Machinedrum, 03 the Monomachine
	uint8_t product(MachineModel _model);

	// F0 00 20 3C <product> 00 <command> ... F7
	bool hasHeader(MachineModel _model, MessageView _message, uint8_t _command);

	// F0 00 20 3C <product> 00 <command>, with room reserved for a message of _size bytes
	Message header(MachineModel _model, uint8_t _command, size_t _size);
	// F0 00 20 3C <product> 00 <command> <body> F7
	Message message(MachineModel _model, uint8_t _command, std::initializer_list<uint8_t> _body);

	// Whether every byte between the first and the last of a message of two bytes or more is below $80
	bool isSevenBit(MessageView _message);

	// A dump as the machines check one: the header, 7-bit data, the checksum (the low 14 bits of the sum of the
	// bytes from the slot, byte 9, to the one before the checksum) and the length (the size minus 10).
	bool validDump(MachineModel _model, MessageView _message, uint8_t _command);

	// Appends _count bytes in 7-bit groups: each group of up to seven bytes is preceded by their top bits, MSB
	// first. The inverse of read7Bit.
	void append7Bit(std::vector<uint8_t>& _out, const uint8_t* _data, size_t _count);

	// Reads _count bytes packed in 7-bit groups (a byte of top bits, MSB first, then up to seven bytes) starting at
	// _position, which it advances.
	bool read7Bit(MessageView _message, size_t& _position, size_t _count, uint8_t* _out);

	// Appends checksum, length and F7 to a dump that ends with its payload.
	void finishDump(Message& _message);

	// The big-endian words the dumps hold: masks, machines, the swing amount
	inline uint32_t readBigEndian32(const uint8_t* _bytes)
	{
		return static_cast<uint32_t>(_bytes[0]) << 24 | static_cast<uint32_t>(_bytes[1]) << 16
			| static_cast<uint32_t>(_bytes[2]) << 8 | _bytes[3];
	}

	inline void writeBigEndian32(uint8_t* _bytes, const uint32_t _value)
	{
		_bytes[0] = static_cast<uint8_t>(_value >> 24);
		_bytes[1] = static_cast<uint8_t>(_value >> 16);
		_bytes[2] = static_cast<uint8_t>(_value >> 8);
		_bytes[3] = static_cast<uint8_t>(_value);
	}

	inline uint64_t readBigEndian64(const uint8_t* _bytes)
	{
		uint64_t value = 0;
		for(size_t i = 0; i < 8; ++i)
			value = (value << 8) | _bytes[i];
		return value;
	}

	inline void writeBigEndian64(uint8_t* _bytes, uint64_t _value)
	{
		for(size_t i = 8; i-- > 0;)
		{
			_bytes[i] = static_cast<uint8_t>(_value & 0xff);
			_value >>= 8;
		}
	}

	// A Kit name as the firmware stores it: ASCII up to a zero or the end of its field, anything else shown as '?',
	// trailing spaces removed
	std::string kitName(const uint8_t* _name, size_t _size);
}
