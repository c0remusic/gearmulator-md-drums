#pragma once

#include "mdsysexautomation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// What every Elektron dump shares: the header, the 7-bit packing of bytes with their top bit set, the checksum and
// the length before F7. Used by the codecs of mdsysexautomation.cpp and mdkit.cpp.
namespace md::automation::sysex::codec
{
	// The product byte after F0 00 20 3C: 02 the Machinedrum, 03 the Monomachine
	uint8_t product(MachineModel _model);

	// F0 00 20 3C <product> 00 <command> ... F7
	bool hasHeader(MachineModel _model, MessageView _message, uint8_t _command);

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
}
