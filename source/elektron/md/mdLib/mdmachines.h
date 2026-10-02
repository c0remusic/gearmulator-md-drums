#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "mdtypes.h"

namespace md::machines
{
	// No machine known for a track yet (no Kit dump seen, nothing assigned).
	constexpr uint16_t g_unknown = 0xffff;

	struct Family
	{
		std::string_view name;       // as on the machine, e.g. "TRX"
		std::string_view synthesis;  // kind of sound source, shown next to the machine name
	};

	struct Machine
	{
		uint16_t id;           // Kit dump and ASSIGN MACHINE id; Machinedrum ROM/RAM machines are 128 and up
		std::string_view name; // as on the machine's display, e.g. "TRX-BD"
		uint8_t family;        // index into families()
		bool assignable;       // false: known from Kit data but not offered in the machine picker
	};

	const std::vector<Family>& families(MachineModel _model);
	const std::vector<Machine>& machines(MachineModel _model);

	const Machine* find(MachineModel _model, uint16_t _id);
	const Family* familyOf(MachineModel _model, uint16_t _id);

	// The machine's eight synthesis parameters as the machine's display names them ("PTCH", "DEC"...),
	// in parameter order; an empty name for a parameter the machine does not use. nullptr when the id
	// is not in the firmware's machine table.
	using ParameterNames = std::array<std::string_view, 8>;
	const ParameterNames* parameterNames(MachineModel _model, uint16_t _id);
}
