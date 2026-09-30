#include "mdmachines.h"

#include <algorithm>

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
				{84, "INP-EA", MdInp, true}, {85, "INP-EB", MdInp, true}, {86, "INP-CA", MdInp, true}, {87, "INP-CB", MdInp, true},
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

			std::stable_sort(m.begin(), m.end(), [](const Machine& _a, const Machine& _b)
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
			};
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
}
