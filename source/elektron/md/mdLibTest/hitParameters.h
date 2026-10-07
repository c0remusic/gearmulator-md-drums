// What a comparison hit plays with, shared by the firmware bench and the engine test.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace mdFirmwareBench
{
	// The 24 track parameters: SYN1-8, then AMD AMF EQF EQG FLTF FLTW FLTQ SRR, then DIST VOL PAN DEL REV LFOS LFOD
	// LFOM. The filter is open, the effects off, the LFO at zero depth.
	inline constexpr std::array<uint8_t, 24> g_hitParameters{64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 64, 64, 0, 127, 0, 0,
		0, 100, 64, 0, 0, 0, 0, 0};

	// The ROM machines: ROM-01 to -32 are ids 128-159, ROM-33 to -48 ids 176-191.
	inline constexpr bool isRomMachine(const uint16_t _machine)
	{
		return (_machine >= 128 && _machine < 160) || (_machine >= 176 && _machine < 192);
	}

	// g_hitParameters, except that a ROM machine plays its whole sample once: PTCH 64, DEC and HOLD 127, BRR 0,
	// STRT 0, END 127, RTRG and RTIM 0. With its SYN at 64 it would replay a sliver from the sample's middle.
	inline std::array<uint8_t, 24> hitParameters(const uint16_t _machine)
	{
		auto parameters = g_hitParameters;
		if(isRomMachine(_machine))
		{
			constexpr std::array<uint8_t, 8> rom{64, 127, 127, 0, 0, 127, 0, 0};
			std::copy(rom.begin(), rom.end(), parameters.begin());
		}
		return parameters;
	}

	// The track level (the Kit's LEV), which the boot Kit sets differently on each track.
	inline constexpr uint8_t g_hitLevel = 100;

	// The machines mdEngineFirmwareTest compares: TRX-BD, TRX-SD, EFM-CB, P-I-MT, and three samples of the UW bank:
	// ROM-01 (44.1 kHz, an odd length), ROM-02 (32 kHz) and ROM-22 (44.1 kHz, looped).
	inline constexpr std::array<uint16_t, 7> g_hitMachines{16, 17, 37, 66, 128, 129, 149};

	// The residual a hit may leave against the firmware's. A ROM hit of the firmware varies with what the firmware
	// played before it: ROM-22 recorded alone and after ROM-01 and -02 differs from itself by -79.5 dB, and the engine,
	// whose hits do not depend on what came before, matches one recording within -112 dB and the other within -79.5
	// (2026-10-07). The voice DSP's memory and the voice's coefficient words are the same in both cases; the cause is
	// not known. The other machines have stayed within -99 to -110 dB.
	inline constexpr double hitResidualFloorDb(const uint16_t _machine) { return isRomMachine(_machine) ? -70.0 : -90.0; }
}
