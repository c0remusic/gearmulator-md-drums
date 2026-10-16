#pragma once

#include "mdkit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The Machinedrum's messages that change one thing of its live Kit, as OS 1.63 reads them, in one place and without
// allocating: ASSIGN MACHINE ($5B), the master effects ($5D-$60), SET LFO PARAM ($62), SET TRIG GROUP and SET MUTE
// GROUP ($65, $66). MD Drums' Device decodes and applies them, MD Drums' messages encode them, and the allocating
// encoders (assignMachine, masterEffectChange, lfoChange, linkChange, chokeChange) wrap them. The CCs are
// mdautomation's.
namespace md::automation::sysex
{
	struct KitChange
	{
		enum class Kind : uint8_t
		{
			Machine,	// track; value the id 0-191; index the scope (KitChange::AssignScope for a $5B without one)
			Master,		// index the master effect's parameter in MasterEffect order (effect * 8 + parameter), value
			Lfo,		// track; index the field: LfoSettings' five, then LFOS, LFOD and LFOM; value
			Link,		// track; value the Track its Hit also hits, or MdKit::Off
			Choke,		// track; value the Track its Hit silences, or MdKit::Off
		};
		// A $5B's scope: 0 sets SYN1-8, 1 the effects page too, 2 (a $5B without a scope byte) the routing page too
		static constexpr uint8_t AssignScope = 2;
		// $62's fields after LfoSettings' five: LFOS, LFOD and LFOM, the Track's parameters 21 to 23
		static constexpr uint8_t LfoFields = 8;

		Kind kind = Kind::Machine;
		uint8_t track = 0;
		uint8_t index = 0;
		uint8_t value = 0;

		bool operator==(const KitChange& _other) const
		{
			return kind == _other.kind && track == _other.track && index == _other.index && value == _other.value;
		}
		bool operator!=(const KitChange& _other) const { return !(*this == _other); }
	};

	// A message from F0 to F7, 12 bytes at most (a $5B with its scope)
	struct KitChangeMessage
	{
		std::array<uint8_t, 12> bytes{};
		uint8_t size = 0;
	};

	// The change a message carries, as OS 1.63 reads it ($2052fe, $205852, $205a4a): a $5B of 10, 11 or 12 bytes
	// (only a UW flag of 1 adds 128 to the id), a $5D-$60, $62, $65 or $66 of 10. Empty for any other message, and for
	// one the OS ignores: a Track or a field out of range, an id past 191, an LFO value past its field's range
	// (LfoSettings::FieldMaximum), a Link or Choke target from 16 to 126 ($7f is MdKit::Off).
	std::optional<KitChange> decodeKitChange(MessageView _message);
	// The message for a change, as OS 1.63 sends one: a $5B without a scope byte for AssignScope. Empty for a change
	// decodeKitChange would not give.
	std::optional<KitChangeMessage> encodeKitChange(const KitChange& _change);
	// The same as an allocating Message, for the encoders that return one (assignMachine, masterEffectChange,
	// lfoChange, linkChange, chokeChange)
	std::optional<Message> kitChangeMessage(const KitChange& _change);

	// What OS 1.63 does to its live Kit with a change ($5B: $2053e0 on). _machineDefaults: the SYN1-8 defaults the OS's
	// descriptor of the machine holds (md::engine::MachineInfo::defaults), null for a machine the OS lacks, which
	// changes nothing. False when the Kit did not take the change.
	// A machine takes the whole machine word and, for its scope, SYN1-8 the machine's own (for CTR-RE, -GB, -EQ and -DX
	// the Kit's master effect each controls, os163::CtrBlocks; for CTR-8P the values of the parameters its knobs aim
	// at, os163::Ctr8pTargets, read as the OS reads them), then the effects and routing pages the OS's (os163: MID
	// machines' own, 64 everywhere for CTR-AL, CTR-8P's knobs' targets, which scope 0 only checks). Left out: CTR-AL
	// also aims the Track's LFO at "track 17", and a machine that replaces it aims the LFO back at its own Track; no
	// host parameter of MD Drums holds that destination.
	bool applyKitChange(const KitChange& _change, MdKit& _kit,
		const std::array<uint8_t, 8>* _machineDefaults = nullptr);

	// The tables ASSIGN MACHINE copies, as OS 1.63 holds them (each page's routing 8 bytes after its effects), which
	// mdDrumsEngineTest reads in the OS image
	namespace os163
	{
		constexpr std::array<uint8_t, 8> Effects{0, 0, 64, 64, 0, 127, 0, 0};
		constexpr std::array<uint8_t, 8> Routing{0, 127, 64, 0, 0, 64, 0, 0};
		constexpr uint32_t EffectsAddress = 0x246148;
		constexpr std::array<uint8_t, 8> MidEffects{0, 64, 0, 64, 0, 64, 0, 64};
		constexpr std::array<uint8_t, 8> MidRouting{0, 64, 0, 64, 0, 64, 0, 0};
		constexpr uint32_t MidEffectsAddress = 0x246258;
		constexpr std::array<uint8_t, 8> CtrAl{64, 64, 64, 64, 64, 64, 64, 64};	// both pages
		constexpr uint32_t CtrAlAddress = 0x246268;
		// CTR-8P's knobs' targets as (track, parameter) pairs: the effects page aims knobs 1-4, the routing page 5-8;
		// its SYN1-8 take the values of the pairs after them
		constexpr std::array<uint8_t, 8> Ctr8pEffects{0, 0, 1, 0, 2, 0, 3, 0};
		constexpr std::array<uint8_t, 8> Ctr8pRouting{4, 0, 5, 0, 6, 0, 7, 0};
		constexpr std::array<uint8_t, 16> Ctr8pTargets{0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 7, 0, 0, 0};
		constexpr uint32_t Ctr8pAddress = 0x246278;
		// The Kit's master block (MdKitMasterEffectBlock's) that CTR-RE, -GB, -EQ and -DX control
		constexpr std::array<uint8_t, 4> CtrBlocks{1, 0, 2, 3};
		constexpr uint32_t CtrBlocksAddress = 0x2462db;
	}
}
