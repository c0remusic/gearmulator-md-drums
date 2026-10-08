// What a comparison hit plays with, shared by the firmware bench and the engine test.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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

	// Links and Chokes (trig and mute groups), played by the firmware (mdTrigLatencyFirmwareTest --groups) and by the
	// engine (mdEngineFirmwareTest) with hitParameters and g_hitLevel, on tracks no scenario plays twice but where it
	// strikes a track again: a voice's noise and oscillators keep their state from one Hit to the next. Tracks 9 and 10
	// keep factory Kit 1's TRX-CH and TRX-OH and its Choke from 9 to 10; the scenarios set and clear their own groups.
	// Track numbers are 0-based.
	struct GroupTrack
	{
		uint8_t track;
		uint16_t machine;
	};
	inline constexpr std::array<GroupTrack, 13> g_groupTracks{{
		{1, 66},	// P-I-MT
		{2, 16},	// TRX-BD
		{3, 17},	// TRX-SD
		{4, 37},	// EFM-CB
		{5, 66},	// P-I-MT
		{8, 22},	// TRX-CH, Kit 1's Choke source
		{9, 23},	// TRX-OH, Kit 1's Choke target
		{10, 66},	// P-I-MT
		{11, 16},	// TRX-BD
		{12, 37},	// EFM-CB
		{13, 17},	// TRX-SD
		{14, 37},	// EFM-CB
		{15, 16},	// TRX-BD
	}};

	inline constexpr uint8_t g_noTrack = 0xff;

	struct GroupNote
	{
		uint32_t frame;		// from the scenario's first note
		uint8_t track;
		uint8_t velocity;
	};

	struct GroupScenario
	{
		const char* name;
		// The track on each of outputs A to F, or g_noTrack. A and B also carry the main mix, which holds what earlier
		// scenarios left ringing: the scenarios play on C to F.
		std::array<uint8_t, 6> outputs;
		std::vector<std::array<uint8_t, 2>> links;		// track, target
		std::vector<std::array<uint8_t, 2>> chokes;		// track, target
		std::vector<uint8_t> muted;
		std::vector<GroupNote> notes;
		// More than one track sounds: the firmware's voices nudge each other (g_concurrentResidualFloorDb)
		bool concurrent;
		// The blocks, at least and at most, on which only one of the firmware and the engine sounds. DSP1 takes the UC's
		// words a block ahead of DSP2's audio of the same tick, so a cut in the Hit's tick comes a block before the Hit's
		// sound on the firmware and in the Hit's block in the engine (which keeps the engine's note-to-sound latency);
		// what the OS changes at its next tick comes up to an engine tick (11 blocks) apart, the firmware's tick
		// following the load of its UC (3 to 11 blocks measured).
		std::array<size_t, 2> apartBlocks;
	};

	// The residual of a track against the firmware's while other tracks sound: the firmware's voices nudge each
	// other, its P-I-MT after a TRX-BD differing by -79 dB from the same hit after an EFM-CB (2026-10-08), where the
	// engine's tracks are independent. Measured: -67.7 to -79.1 dB.
	inline constexpr double g_concurrentResidualFloorDb = -60.0;

	inline const std::array<GroupScenario, 6> g_groupScenarios{{
		// Kit 1's Choke: the open hat, then the closed hat a tenth of a second later cuts it in the same tick
		{"Choke 9 to 10 (factory Kit 1)", {g_noTrack, g_noTrack, 9, 8, g_noTrack, g_noTrack}, {}, {}, {},
			{{0, 9, 100}, {4410, 8, 100}}, true, {1, 1}},
		// A Choke's target struck again: its words in the Hit's tick are still those of the Choke
		{"Choked track struck again", {g_noTrack, g_noTrack, 11, 10, g_noTrack, g_noTrack}, {}, {{10, 11}}, {},
			{{0, 11, 100}, {4410, 10, 100}, {13230, 11, 100}}, true, {1, 12}},
		{"Struck again without a Choke", {g_noTrack, g_noTrack, 2, 1, g_noTrack, g_noTrack}, {}, {}, {},
			{{0, 2, 100}, {4410, 1, 100}, {13230, 2, 100}}, true, {0, 0}},
		// A Link fires its target in the same tick, at the source's velocity
		{"Link 13 to 14", {g_noTrack, g_noTrack, 13, 12, g_noTrack, g_noTrack}, {{12, 13}}, {}, {},
			{{0, 12, 80}}, true, {0, 0}},
		// A muted track drops its Hit: no sound, no Link, no Choke
		{"Muted track struck", {g_noTrack, g_noTrack, 15, 14, 3, g_noTrack}, {{14, 3}}, {{14, 15}}, {14},
			{{0, 15, 100}, {4410, 14, 100}}, false, {0, 0}},
		// A Choke onto a lower track takes effect a tick later
		{"Choke 6 to 5", {g_noTrack, g_noTrack, 4, 5, g_noTrack, g_noTrack}, {}, {{5, 4}}, {},
			{{0, 4, 100}, {4410, 5, 100}}, true, {0, 11}},
	}};
}
