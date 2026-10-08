#pragma once

#include "mdsysexautomation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace md::automation::sysex
{
	// A Machinedrum Kit with every byte its dump ($52) holds, in the layout OS 1.63 gives a stored Kit in patch memory
	// (a $460-byte record: .scratch/md-drums-editor/research/04-kits.md). Where KitDump keeps what the editor shows,
	// this keeps all of it: a dump read and written again comes out byte for byte, and a Kit made here holds what the
	// OS writes into a Kit it initialises.
	struct MdKit
	{
		static constexpr size_t RecordSize = 0x460;
		static constexpr size_t NameSize = 16;
		static constexpr size_t LfoSize = 36;
		static constexpr size_t MasterEffectsSize = MasterEffectCount * MasterEffectParameters;
		static constexpr uint8_t ParameterCount = 24;
		// A Link or a Choke switched off. SET TRIG GROUP and SET MUTE GROUP ($65, $66) send it as $7f.
		static constexpr uint8_t Off = 0xff;

		using TrackParameters = std::array<uint8_t, ParameterCount>;

		// NUL-terminated and padded; the bytes after the NUL are kept as they came (the factory Kits hold stale ones)
		std::array<uint8_t, NameSize> name{};
		// SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST VOL PAN DEL REV LFOS LFOD LFOM
		std::array<TrackParameters, machinedrum::TrackCount> parameters{};
		std::array<uint8_t, machinedrum::TrackCount> levels{};
		// A big-endian word in the dump: the machine id (mdmachines.h) in the low byte; the upper bytes are 0 in every
		// factory Kit and kept as they came
		std::array<uint32_t, machinedrum::TrackCount> machines{};
		// Five fields $62 sets (LfoSettings: destination track and parameter, shapes, update), then the running LFO's
		// state, which LOAD KIT copies into the running LFO
		std::array<std::array<uint8_t, LfoSize>, machinedrum::TrackCount> lfos{};
		// In the dump's order, reverb, echo, EQ, dynamix (masterEffectValues gives them by MasterEffect)
		std::array<uint8_t, MasterEffectsSize> masterEffects{};
		// The Track a Track's Hit also hits (trig group), or Off
		std::array<uint8_t, machinedrum::TrackCount> links{};
		// The Track a Track's Hit silences (mute group), or Off
		std::array<uint8_t, machinedrum::TrackCount> chokes{};

		// Every value 0, each LFO's state as OS 1.63 writes it into every LFO it initialises (the word $0000029a at
		// bytes 28-31), no Link and no Choke
		MdKit();

		// From and to a record of RecordSize bytes, as the patch memory and the factory images hold Kits
		static MdKit fromRecord(const uint8_t* _record);
		void toRecord(uint8_t* _record) const;

		// The name as the machine shows it, up to the NUL, trailing spaces removed
		std::string displayName() const;
		// Whether the slot holding this Kit is empty: the OS marks it with a first name byte of $ff, sent as $7f
		bool isEmptySlot() const { return name[0] == 0xff || name[0] == 0x7f; }

		uint8_t machine(const uint8_t _track) const { return static_cast<uint8_t>(machines[_track] & 0xff); }
		LfoSettings lfo(uint8_t _track) const;
		void setLfo(uint8_t _track, const LfoSettings& _lfo);
		MasterEffects masterEffectValues() const { return masterEffectsFromKit(masterEffects.data()); }

		bool operator==(const MdKit& _other) const;
		bool operator!=(const MdKit& _other) const { return !(*this == _other); }
	};

	// The size of a Machinedrum Kit dump, $4d1 bytes
	constexpr size_t MdKitDumpSize = 0x4d1;

	// A Kit dump ($52) of _kit for slot _slot (0-63) as OS 1.63 sends one: version 4, revision 1, the name and
	// parameters masked to 7 bits, the machines, LFOs, Links and Chokes 7-bit packed
	Message mdKitDump(const MdKit& _kit, uint8_t _slot);
	// The Kit a Machinedrum dump holds, and its slot. Empty for a dump OS 1.63 would refuse (header, checksum,
	// length, slot above 63), and for any version but 3 and 4, whose LFOs the OS stores whole: versions 1 and 2 lay
	// them out differently.
	std::optional<MdKit> parseMdKit(MessageView _message, uint8_t* _slot = nullptr);

	// SET TRIG GROUP ($65): the Track (0-15) a Track's Hit also hits, or MdKit::Off. Empty for a Track out of range.
	std::optional<Message> linkChange(uint8_t _track, uint8_t _target);
	// SET MUTE GROUP ($66): the Track (0-15) a Track's Hit silences, or MdKit::Off.
	std::optional<Message> chokeChange(uint8_t _track, uint8_t _target);
}
