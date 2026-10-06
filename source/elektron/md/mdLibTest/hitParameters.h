// What a comparison hit plays with, shared by the firmware bench and the engine test.
#pragma once

#include <array>
#include <cstdint>

namespace mdFirmwareBench
{
	// The 24 track parameters: SYN1-8, then AMD AMF EQF EQG FLTF FLTW FLTQ SRR, then DIST VOL PAN DEL REV LFOS LFOD
	// LFOM. The filter is open, the effects off, the LFO at zero depth.
	inline constexpr std::array<uint8_t, 24> g_hitParameters{64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 64, 64, 0, 127, 0, 0,
		0, 100, 64, 0, 0, 0, 0, 0};

	// The track level (the Kit's LEV), which the boot Kit sets differently on each track.
	inline constexpr uint8_t g_hitLevel = 100;

	// The machines mdEngineFirmwareTest compares: TRX-BD, TRX-SD, EFM-CB, P-I-MT.
	inline constexpr std::array<uint16_t, 4> g_hitMachines{16, 17, 37, 66};
}
