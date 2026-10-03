#include "mdmachines.h"

#include <algorithm>
#include <utility>

namespace md::machines
{
	namespace
	{
		// Machine ids as the Kit dump stores them and ASSIGN MACHINE ($5B) takes
		// them (Machinedrum ids 128 and up are sent as id - 128 with the UW flag).
		// Ids and names follow the MegaCommand/MCL tables (github.com/jmamma/MCL);
		// the firmware tests here agree where they overlap: MM GND-SIN = 1,
		// FX-THRU = 12, DPRO-DDRW = 32, DPRO-DENS = 33; MD ROM-01 = 128.
		enum MdFamily : uint8_t { MdGnd, MdTrx, MdEfm, MdE12, MdPi, MdInp, MdMid, MdCtr, MdNfx, MdRom, MdRam };
		enum MmFamily : uint8_t { MmGnd, MmSid, MmSwave, MmDpro, MmFm, MmVo, MmFx };

		const std::vector<Family> g_mdFamilies =
		{
			{"GND", "générateur"},
			{"TRX", "synthèse analogique modélisée"},
			{"EFM", "synthèse FM"},
			{"E12", "sample 12 bits intégré"},
			{"P-I", "modélisation physique"},
			{"INP", "entrée audio"},
			{"MID", "piste MIDI, pas de son"},
			{"CTR", "contrôle, pas de son"},
			{"NFX", "effet de piste"},
			{"ROM", "sample utilisateur (UW)"},
			{"RAM", "enregistrement et relecture (UW)"},
		};

		const std::vector<Family> g_mmFamilies =
		{
			{"GND", "générateur"},
			{"SID", "synthèse SID 6581"},
			{"SWAVE", "synthèse superwave"},
			{"DPRO", "synthèse DigiPRO"},
			{"FM+", "synthèse FM"},
			{"VO", "synthèse vocale"},
			{"FX", "effet sur un bus"},
		};

		std::vector<Machine> createMdMachines()
		{
			std::vector<Machine> m =
			{
				{0, "GND---", MdGnd, true}, {1, "GND-SN", MdGnd, true}, {2, "GND-NS", MdGnd, true}, {3, "GND-IM", MdGnd, true},
				// Machines whose presence in OS 1.63 is unconfirmed: shown when a Kit holds them, not offered.
				{4, "GND-SW", MdGnd, false}, {5, "GND-PU", MdGnd, false},
				{7, "NFX-EV", MdNfx, false}, {8, "NFX-CO", MdNfx, false}, {9, "NFX-UC", MdNfx, false},
				{16, "TRX-BD", MdTrx, true}, {17, "TRX-SD", MdTrx, true}, {18, "TRX-XT", MdTrx, true}, {19, "TRX-CP", MdTrx, true},
				{20, "TRX-RS", MdTrx, true}, {21, "TRX-CB", MdTrx, true}, {22, "TRX-CH", MdTrx, true}, {23, "TRX-OH", MdTrx, true},
				{24, "TRX-CY", MdTrx, true}, {25, "TRX-MA", MdTrx, true}, {26, "TRX-CL", MdTrx, true}, {27, "TRX-XC", MdTrx, true},
				{28, "TRX-B2", MdTrx, true}, {29, "TRX-S2", MdTrx, false},
				{32, "EFM-BD", MdEfm, true}, {33, "EFM-SD", MdEfm, true}, {34, "EFM-XT", MdEfm, true}, {35, "EFM-CP", MdEfm, true},
				{36, "EFM-RS", MdEfm, true}, {37, "EFM-CB", MdEfm, true}, {38, "EFM-HH", MdEfm, true}, {39, "EFM-CY", MdEfm, true},
				{48, "E12-BD", MdE12, true}, {49, "E12-SD", MdE12, true}, {50, "E12-HT", MdE12, true}, {51, "E12-LT", MdE12, true},
				{52, "E12-CP", MdE12, true}, {53, "E12-RS", MdE12, true}, {54, "E12-CB", MdE12, true}, {55, "E12-CH", MdE12, true},
				{56, "E12-OH", MdE12, true}, {57, "E12-RC", MdE12, true}, {58, "E12-CC", MdE12, true}, {59, "E12-BR", MdE12, true},
				{60, "E12-TA", MdE12, true}, {61, "E12-TR", MdE12, true}, {62, "E12-SH", MdE12, true}, {63, "E12-BC", MdE12, true},
				{64, "P-I-BD", MdPi, true}, {65, "P-I-SD", MdPi, true}, {66, "P-I-MT", MdPi, true}, {67, "P-I-ML", MdPi, true},
				{68, "P-I-MA", MdPi, true}, {69, "P-I-RS", MdPi, true}, {70, "P-I-RC", MdPi, true}, {71, "P-I-CC", MdPi, true},
				{72, "P-I-HH", MdPi, true},
				{80, "INP-GA", MdInp, true}, {81, "INP-GB", MdInp, true}, {82, "INP-FA", MdInp, true}, {83, "INP-FB", MdInp, true},
				{84, "INP-EA", MdInp, true}, {85, "INP-EB", MdInp, true},
				// Not in the OS 1.63 machine table (see parameterNames): a later OS's.
				{86, "INP-CA", MdInp, false}, {87, "INP-CB", MdInp, false},
				{112, "CTR-AL", MdCtr, true}, {113, "CTR-8P", MdCtr, true},
				{120, "CTR-RE", MdCtr, true}, {121, "CTR-GB", MdCtr, true}, {122, "CTR-EQ", MdCtr, true}, {123, "CTR-DX", MdCtr, true},
				{160, "RAM-R1", MdRam, true}, {161, "RAM-R2", MdRam, true}, {162, "RAM-P1", MdRam, true}, {163, "RAM-P2", MdRam, true},
				{165, "RAM-R3", MdRam, true}, {166, "RAM-R4", MdRam, true}, {167, "RAM-P3", MdRam, true}, {168, "RAM-P4", MdRam, true},
				// MID-01..16 are 96..111. ROM-01..32 are 128..159, ROM-33..48 are 176..191.
				{96, "MID-01", MdMid, true}, {97, "MID-02", MdMid, true}, {98, "MID-03", MdMid, true}, {99, "MID-04", MdMid, true},
				{100, "MID-05", MdMid, true}, {101, "MID-06", MdMid, true}, {102, "MID-07", MdMid, true}, {103, "MID-08", MdMid, true},
				{104, "MID-09", MdMid, true}, {105, "MID-10", MdMid, true}, {106, "MID-11", MdMid, true}, {107, "MID-12", MdMid, true},
				{108, "MID-13", MdMid, true}, {109, "MID-14", MdMid, true}, {110, "MID-15", MdMid, true}, {111, "MID-16", MdMid, true},
				{128, "ROM-01", MdRom, true}, {129, "ROM-02", MdRom, true}, {130, "ROM-03", MdRom, true}, {131, "ROM-04", MdRom, true},
				{132, "ROM-05", MdRom, true}, {133, "ROM-06", MdRom, true}, {134, "ROM-07", MdRom, true}, {135, "ROM-08", MdRom, true},
				{136, "ROM-09", MdRom, true}, {137, "ROM-10", MdRom, true}, {138, "ROM-11", MdRom, true}, {139, "ROM-12", MdRom, true},
				{140, "ROM-13", MdRom, true}, {141, "ROM-14", MdRom, true}, {142, "ROM-15", MdRom, true}, {143, "ROM-16", MdRom, true},
				{144, "ROM-17", MdRom, true}, {145, "ROM-18", MdRom, true}, {146, "ROM-19", MdRom, true}, {147, "ROM-20", MdRom, true},
				{148, "ROM-21", MdRom, true}, {149, "ROM-22", MdRom, true}, {150, "ROM-23", MdRom, true}, {151, "ROM-24", MdRom, true},
				{152, "ROM-25", MdRom, true}, {153, "ROM-26", MdRom, true}, {154, "ROM-27", MdRom, true}, {155, "ROM-28", MdRom, true},
				{156, "ROM-29", MdRom, true}, {157, "ROM-30", MdRom, true}, {158, "ROM-31", MdRom, true}, {159, "ROM-32", MdRom, true},
				{176, "ROM-33", MdRom, true}, {177, "ROM-34", MdRom, true}, {178, "ROM-35", MdRom, true}, {179, "ROM-36", MdRom, true},
				{180, "ROM-37", MdRom, true}, {181, "ROM-38", MdRom, true}, {182, "ROM-39", MdRom, true}, {183, "ROM-40", MdRom, true},
				{184, "ROM-41", MdRom, true}, {185, "ROM-42", MdRom, true}, {186, "ROM-43", MdRom, true}, {187, "ROM-44", MdRom, true},
				{188, "ROM-45", MdRom, true}, {189, "ROM-46", MdRom, true}, {190, "ROM-47", MdRom, true}, {191, "ROM-48", MdRom, true},
			};

			// Ids are unique, so a plain sort gives a fixed order. std::stable_sort would
			// take a temporary buffer that ASan reports as an allocator mismatch.
			std::sort(m.begin(), m.end(), [](const Machine& _a, const Machine& _b)
			{
				return _a.family != _b.family ? _a.family < _b.family : _a.id < _b.id;
			});
			return m;
		}

		std::vector<Machine> createMmMachines()
		{
			return
			{
				{0, "GND-GND", MmGnd, true}, {1, "GND-SIN", MmGnd, true}, {2, "GND-NOIS", MmGnd, true},
				{3, "SID-6581", MmSid, true},
				{4, "SWAVE-SAW", MmSwave, true}, {5, "SWAVE-PULS", MmSwave, true}, {14, "SWAVE-ENS", MmSwave, true},
				{6, "DPRO-WAVE", MmDpro, true}, {7, "DPRO-BBOX", MmDpro, true}, {32, "DPRO-DDRW", MmDpro, true}, {33, "DPRO-DENS", MmDpro, true},
				{8, "FM+-STAT", MmFm, true}, {9, "FM+-PAR", MmFm, true}, {10, "FM+-DYN", MmFm, true},
				{11, "VO-VO-6", MmVo, true},
				{12, "FX-THRU", MmFx, true}, {13, "FX-REVERB", MmFx, true}, {15, "FX-CHORUS", MmFx, true},
				{16, "FX-DYNAMIX", MmFx, true}, {17, "FX-RINGMOD", MmFx, true},
				// In the OS 1.32b machine table, not in MCL's; ASSIGN MACHINE not tried with them.
				{18, "FX-PHASER", MmFx, false}, {19, "FX-FLANGER", MmFx, false},
			};
		}

		// The synthesis parameter names of the firmware's machine table, which the OS unpacks into RAM:
		// Machinedrum OS 1.63 from $24ef55 (records of 86 bytes: id, family and model in 5 characters,
		// 8 names of 4), Monomachine OS 1.32b from $257fc4 (records of 176 bytes: id, family, model,
		// 8 names of 6). Read with mdPlayheadProbe --dump. An empty name: the machine does not use the
		// parameter (the Monomachine's table says "---"). The Machinedrum's GND-SW, GND-PU, NFX-* and
		// INP-CA/CB are not in its table.
		using Names = std::pair<uint16_t, ParameterNames>;

		const std::vector<Names> g_mdParameterNames =
		{
			{0, {"", "", "", "", "", "", "", ""}},
			{1, {"PTCH", "DEC", "RAMP", "RDEC", "", "", "", ""}},
			{2, {"DEC", "", "", "", "", "", "", ""}},
			{3, {"UP", "UVAL", "DOWN", "DVAL", "", "", "", ""}},
			{16, {"PTCH", "DEC", "RAMP", "RDEC", "STRT", "NOIS", "HARM", "CLIP"}},
			{17, {"PTCH", "DEC", "BUMP", "BENV", "SNAP", "TONE", "TUNE", "CLIP"}},
			{18, {"PTCH", "DEC", "RAMP", "RDEC", "DAMP", "DIST", "DTYP", ""}},
			{19, {"CLPY", "TONE", "HARD", "RICH", "RATE", "ROOM", "RSIZ", "RTUN"}},
			{20, {"PTCH", "DEC", "DIST", "", "", "", "", ""}},
			{21, {"PTCH", "DEC", "ENH", "DAMP", "TONE", "BUMP", "", ""}},
			{22, {"GAP", "DEC", "HPF", "LPF", "MTAL", "", "", ""}},
			{23, {"GAP", "DEC", "HPF", "LPF", "MTAL", "", "", ""}},
			{24, {"RICH", "DEC", "TOP", "TTUN", "SIZE", "PEAK", "", ""}},
			{25, {"ATT", "SUS", "REV", "DAMP", "RATL", "RTYP", "TONE", "HARD"}},
			{26, {"PTCH", "DEC", "DUAL", "ENH", "TUNE", "CLIC", "", ""}},
			{27, {"PTCH", "DEC", "RAMP", "RDEC", "DAMP", "DIST", "DTYP", ""}},
			{28, {"PTCH", "DEC", "RAMP", "HOLD", "TICK", "NOIS", "DIRT", "DIST"}},
			{29, {"PTCH", "DEC", "NOIS", "NDEC", "POWR", "TUNE", "NTUN", "NTYP"}},
			{32, {"PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "MFB"}},
			{33, {"PTCH", "DEC", "NOIS", "NDEC", "MOD", "MFRQ", "MDEC", "HPF"}},
			{34, {"PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "CLIC"}},
			{35, {"PTCH", "DEC", "CLPS", "CDEC", "MOD", "MFRQ", "MDEC", "HPF"}},
			{36, {"PTCH", "DEC", "MOD", "HPF", "SNAR", "SPTC", "SDEC", "SMOD"}},
			{37, {"PTCH", "DEC", "SNAP", "FB", "MOD", "MFRQ", "MDEC", ""}},
			{38, {"PTCH", "DEC", "TREM", "TFRQ", "MOD", "MFRQ", "MDEC", "FB"}},
			{39, {"PTCH", "DEC", "FB", "HPF", "MOD", "MFRQ", "MDEC", ""}},
			{48, {"PTCH", "DEC", "SNAP", "SPLN", "STRT", "RTRG", "RTIM", "BEND"}},
			{49, {"PTCH", "DEC", "HP", "RING", "STRT", "RTRG", "RTIM", "BEND"}},
			{50, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{51, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{52, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{53, {"PTCH", "DEC", "HP", "RATL", "STRT", "RTRG", "RTIM", "BEND"}},
			{54, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{55, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{56, {"PTCH", "DEC", "HP", "STOP", "STRT", "RTRG", "RTIM", "BEND"}},
			{57, {"PTCH", "DEC", "HP", "BELL", "STRT", "RTRG", "RTIM", "BEND"}},
			{58, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{59, {"PTCH", "DEC", "HP", "REAL", "STRT", "RTRG", "RTIM", "BEND"}},
			{60, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{61, {"PTCH", "DEC", "HP", "HPQ", "STRT", "RTRG", "RTIM", "BEND"}},
			{62, {"PTCH", "DEC", "HP", "SLEW", "STRT", "RTRG", "RTIM", "BEND"}},
			{63, {"PTCH", "DEC", "HP", "BC", "STRT", "RTRG", "RTIM", "BEND"}},
			{64, {"PTCH", "DEC", "HARD", "HAMR", "TENS", "DAMP", "", ""}},
			{65, {"PTCH", "DEC", "HARD", "RING", "TENS", "RVOL", "RDEC", ""}},
			{66, {"PTCH", "DEC", "HARD", "HAMR", "TUNE", "DAMP", "SIZE", "POS"}},
			{67, {"PTCH", "DEC", "HARD", "TENS", "", "", "", ""}},
			{68, {"GRNS", "DEC", "GLEN", "", "SIZE", "HARD", "", ""}},
			{69, {"PTCH", "DEC", "HARD", "RING", "RVOL", "RDEC", "", ""}},
			{70, {"PTCH", "DEC", "HARD", "RING", "AG", "AU", "BR", "GRAB"}},
			{71, {"PTCH", "DEC", "HARD", "RING", "AG", "AU", "BR", "GRAB"}},
			{72, {"PTCH", "DEC", "CLSN", "RING", "AG", "AU", "BR", "CLOS"}},
			{80, {"VOL", "GATE", "ATCK", "HLD", "DEC", "", "", ""}},
			{81, {"VOL", "GATE", "ATCK", "HLD", "DEC", "", "", ""}},
			{82, {"ALEV", "GATE", "FATK", "FHLD", "FDEC", "FDPH", "FFRQ", "FQ"}},
			{83, {"ALEV", "GATE", "FATK", "FHLD", "FDEC", "FDPH", "FFRQ", "FQ"}},
			{84, {"ALEV", "AHLD", "ADEC", "FQ", "FDPH", "FHLD", "FDEC", "FFRQ"}},
			{85, {"ALEV", "AHLD", "ADEC", "FQ", "FDPH", "FHLD", "FDEC", "FFRQ"}},
			{96, {"NOTE", "N2", "N3", "LEN", "VEL", "PB", "MW", "AT"}},		// MID-01, as MID-02..16
			{112, {"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8"}},
			{113, {"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8"}},
			{120, {"TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"}},
			{121, {"DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"}},
			{122, {"LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"}},
			{123, {"ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"}},
			{128, {"PTCH", "DEC", "HOLD", "BRR", "STRT", "END", "RTRG", "RTIM"}},	// ROM-01, as ROM-02..48, RAM-P1..4
			{160, {"MLEV", "MBAL", "ILEV", "IBAL", "CUE1", "CUE2", "LEN", "RATE"}},	// RAM-R1, as RAM-R2..4
		};

		const std::vector<Names> g_mmParameterNames =
		{
			{0, {"", "", "", "", "", "", "", ""}},
			{1, {"", "", "", "", "", "", "", "TUNE"}},
			{2, {"ST", "RED", "STON", "", "", "", "", "TUNE"}},
			{3, {"PW", "PWAD", "PWRS", "WAVE", "MOD", "MSRC", "MFRQ", "TUNE"}},
			{4, {"UNIL", "UNIW", "UNIX", "", "SUBX", "SUB1", "SUB2", "TUNE"}},
			{5, {"UNIL", "UNIW", "SUB1", "SUB2", "PW", "PWAD", "PWRS", "TUNE"}},
			{6, {"WAVE", "WP", "WPM", "WPRS", "SYNC", "SFRQ", "", "TUNE"}},
			{7, {"PTCH", "STRT", "", "", "RTRG", "RTIM", "", ""}},
			{8, {"1FRQ", "1FIN", "1ENV", "1FB", "2FRQ", "2VOL", "TONE", "TUNE"}},
			{9, {"1FRQ", "1ENV", "2FRQ", "2ENV", "3FRQ", "3ENV", "TONE", "TUNE"}},
			{10, {"1FRQ", "1FEN", "1VOL", "1VEN", "2FRQ", "2ENV", "2FB", "TUNE"}},
			{11, {"VOC1", "VOC2", "V-SW", "VOIC", "CONS", "CLEN", "CVOL", "TUNE"}},
			{12, {"", "", "", "", "", "", "", "INP"}},
			{13, {"DEC", "DAMP", "GATE", "MIX", "HP", "LP", "", "INP"}},
			{14, {"PCH2", "PCH3", "PCH4", "WAVE", "PW", "CHRL", "CHRW", "TUNE"}},
			{15, {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "LP", "INP"}},
			{16, {"ATK", "REL", "THRS", "MIX", "RAT", "GAIN", "RMS", "INP"}},
			{17, {"WAVE", "EXT", "", "MIX", "", "", "", "INP"}},
			{18, {"CNTR", "DEP", "SPD", "MIX", "FB", "WID", "", "INP"}},
			{19, {"DEL", "DEP", "SPD", "MIX", "FB", "WID", "", "INP"}},
			{32, {"WAV1", "MIX", "WAV2", "TIME", "BR1", "WID", "BR2", "TUNE"}},
			{33, {"PCH2", "PCH3", "PCH4", "WAVE", "", "CHRL", "CHRW", "TUNE"}},
		};

		// The Machinedrum machines that share their names with one entry of the table
		uint16_t mdNamesId(const uint16_t _id)
		{
			if(_id >= 96 && _id <= 111)
				return 96;
			if((_id >= 128 && _id <= 159) || (_id >= 176 && _id <= 191) || _id == 162 || _id == 163 || _id == 167 || _id == 168)
				return 128;
			if(_id == 160 || _id == 161 || _id == 165 || _id == 166)
				return 160;
			return _id;
		}
	}

	const std::vector<Family>& families(const MachineModel _model)
	{
		return _model == MachineModel::Monomachine ? g_mmFamilies : g_mdFamilies;
	}

	const std::vector<Machine>& machines(const MachineModel _model)
	{
		static const std::vector<Machine> g_md = createMdMachines();
		static const std::vector<Machine> g_mm = createMmMachines();
		return _model == MachineModel::Monomachine ? g_mm : g_md;
	}

	const Machine* find(const MachineModel _model, const uint16_t _id)
	{
		const auto& list = machines(_model);
		const auto it = std::find_if(list.begin(), list.end(), [_id](const Machine& _m) { return _m.id == _id; });
		return it == list.end() ? nullptr : &*it;
	}

	const Family* familyOf(const MachineModel _model, const uint16_t _id)
	{
		const auto* machine = find(_model, _id);
		return machine ? &families(_model)[machine->family] : nullptr;
	}

	std::string_view machinedrumParameterName(const uint16_t _machine, const uint8_t _index)
	{
		constexpr std::string_view names[24] = {
			"P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8",
			"AMD", "AMF", "EQF", "EQG", "BASE", "WDTH", "Q", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
		if(_index >= 24)
			return {};
		if(_index < 8)
		{
			if(const auto* machine = parameterNames(MachineModel::Machinedrum, _machine))
				return (*machine)[_index];
		}
		return names[_index];
	}

	const ParameterNames* parameterNames(const MachineModel _model, const uint16_t _id)
	{
		const bool mm = _model == MachineModel::Monomachine;
		const auto& table = mm ? g_mmParameterNames : g_mdParameterNames;
		const auto id = mm ? _id : mdNamesId(_id);
		const auto it = std::find_if(table.begin(), table.end(), [id](const Names& _n) { return _n.first == id; });
		return it == table.end() ? nullptr : &it->second;
	}
}
