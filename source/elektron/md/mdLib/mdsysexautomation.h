#pragma once

#include "mdautomation.h"
#include "mdlivekit.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace md::automation::sysex
{
	using Message = std::vector<uint8_t>;
	class MessageView
	{
	public:
		template<typename Allocator>
		MessageView(const std::vector<uint8_t, Allocator>& _message)
			: m_data(_message.data()), m_size(_message.size()) {}

		const uint8_t* begin() const { return m_data; }
		const uint8_t* end() const { return m_data + m_size; }
		const uint8_t& operator[](size_t _index) const { return m_data[_index]; }
		const uint8_t& back() const { return m_data[m_size - 1]; }
		size_t size() const { return m_size; }

	private:
		const uint8_t* m_data;
		size_t m_size;
	};

	enum class StatusParameter : uint8_t
	{
		Global = 0x01,
		Kit = 0x02,
		Pattern = 0x04
	};

	struct StatusResponse
	{
		StatusParameter parameter;
		uint8_t value;
	};

	// Where a Machinedrum track plays: one of the six individual outputs, or the
	// main pair (stereo, pan and master effects).
	enum class TrackOutput : uint8_t
	{
		A, B, C, D, E, F,
		Main
	};
	using TrackOutputs = std::array<TrackOutput, machinedrum::TrackCount>;

	struct GlobalDump
	{
		uint8_t slot;
		uint8_t baseChannel;
		// Machinedrum only: the output of each track.
		std::optional<TrackOutputs> trackOutputs;
	};

	// The Machinedrum Kit's four master effects, in the order of their SysEx ids
	// ($5D to $60), 8 parameters each.
	enum class MasterEffect : uint8_t
	{
		Echo,       // RHYTHM ECHO
		Reverb,     // GATE BOX REVERB
		Eq,
		Dynamix
	};
	constexpr uint8_t MasterEffectCount = 4;
	constexpr uint8_t MasterEffectParameters = 8;
	using MasterEffects = std::array<std::array<uint8_t, MasterEffectParameters>, MasterEffectCount>;

	struct KitDump
	{
		uint8_t slot;
		std::vector<ParameterChange> parameters;
		// Machine id per track (see mdmachines.h), empty when the dump is too short to hold them.
		std::vector<uint16_t> machines;
		// As the machine shows it, trailing spaces removed: up to 16 characters on
		// the Machinedrum, 11 on the Monomachine.
		std::string name;
		// Machinedrum only, indexed by MasterEffect.
		std::optional<MasterEffects> masterEffects;
		// Machinedrum only: each track's LFO.
		std::optional<std::array<LfoSettings, machinedrum::TrackCount>> lfos;
	};

	// The Machinedrum's master effects from the 32 bytes of a Kit that hold them, in the dump's order
	// (reverb, echo, EQ, dynamix): the dump at $487, the live Kit (LiveKit::masterEffects).
	MasterEffects masterEffectsFromKit(const uint8_t* _bytes);

	// A Machinedrum pattern's trigs and locks, as far as the editor shows them. The
	// dump holds 32 steps, or 64 in its long form (a length over 32).
	// What a step does besides its trig: accent (Machinedrum), slide and swing
	enum class StepFlag : uint8_t
	{
		Accent,
		Slide,
		Swing
	};
	constexpr uint8_t StepFlagCount = 3;
	// The swing amount as dumps hold it: how far a swung step is delayed, in 16384ths of the step, scaled so
	// that 50 % is 0 and 80 %, the most the machines take, 9830 (MCL's MDSeqTrack; the factory patterns hold
	// 0). Not measured on the firmware.
	constexpr uint32_t swingWord(const uint8_t _percent)
	{
		return _percent <= 50 ? 0 : ((static_cast<uint32_t>(_percent > 80 ? 80 : _percent) - 50) * 16384 + 25) / 50;
	}
	constexpr uint8_t swingPercent(const uint32_t _word)
	{
		const auto extra = (static_cast<uint64_t>(_word) * 50 + 8192) >> 14;
		return static_cast<uint8_t>(50 + (extra > 30 ? 30 : extra));
	}

	struct PatternDump
	{
		uint8_t slot = 0;      // 0..127, A01..H16
		uint8_t length = 16;   // steps, 1..64
		uint8_t kit = 0;       // 0..63, the Kit the pattern loads
		uint8_t steps = 32;    // steps the dump holds: 32, or 64 in the long form
		std::array<uint64_t, 16> trigs{};      // bit n: a trig on step n + 1
		std::array<uint32_t, 16> lockMasks{};  // bit p: parameter p (0..23) has a lock row
		// The 64 lock rows: row k belongs to the k-th set bit of the masks, in
		// track then parameter order; 128 and above means no lock on that step
		// (always past the steps the dump holds).
		std::vector<std::array<uint8_t, 64>> lockRows;
		// Accent, slide and swing (by StepFlag): a mask of steps every track follows,
		// or, when the pattern sets that kind per track (the machine's EDIT ALL off),
		// a mask per track. Bit n: step n + 1.
		std::array<uint64_t, StepFlagCount> flags{};
		std::array<bool, StepFlagCount> flagPerTrack{};
		std::array<std::array<uint64_t, 16>, StepFlagCount> trackFlags{};
		uint8_t accentAmount = 0;
		uint32_t swingAmount = 0;		// swingPercent

		bool hasTrig(uint8_t _track, uint8_t _step) const;
		// Locked value of parameter _parameter (0..23: synthesis, effects, routing)
		// on a step, or nullopt.
		std::optional<uint8_t> lock(uint8_t _track, uint8_t _parameter, uint8_t _step) const;
		// Whether a track's step is accented, slid or swung: by the track's own mask
		// when the kind is set per track, else by every track's
		bool hasFlag(StepFlag _flag, uint8_t _track, uint8_t _step) const;
		// The steps of a kind a track follows
		uint64_t flagMask(StepFlag _flag, uint8_t _track) const;
	};

	// A Machinedrum track's steps as a pattern holds them, on every step of the dump: what the editor's track
	// copy puts on a track of the same pattern or of another one
	struct MdTrackSteps
	{
		static constexpr uint8_t NoLock = 0xff;

		uint64_t trigs = 0;									// bit n: step n + 1
		std::array<uint64_t, StepFlagCount> flags{};		// the track's own accent, slide and swing
		std::array<std::array<uint8_t, 64>, 24> locks{};	// per parameter and step, NoLock without one

		MdTrackSteps() { for(auto& parameter : locks) parameter.fill(NoLock); }
	};
	MdTrackSteps trackSteps(const PatternDump& _pattern, uint8_t _track);

	// A Machinedrum pattern dump ($67) that can be edited and sent back. Every
	// byte of the dump is kept; toDump() changes only what the edits touched.
	class MdPatternEditor
	{
	public:
		static std::optional<MdPatternEditor> fromDump(MessageView _message);

		// Sets or clears the trig of a step below the pattern length. Clearing it
		// also clears that step's locks on the track, as the Machinedrum does.
		bool setTrig(uint8_t _track, uint8_t _step, bool _on);
		// Sets a locked value (0..127) on a step that has a trig, or clears it.
		// A parameter (0..23) gets its lock row with its first lock and loses it
		// with its last. False when the step has no trig or all 64 rows are taken.
		bool setLock(uint8_t _track, uint8_t _parameter, uint8_t _step, std::optional<uint8_t> _value);
		// Sets the length, 1 to 64. Over 32, a dump of 32 steps takes the long form,
		// its steps 33 to 64 empty; a long dump stays long. Trigs and locks past the
		// length stay in the dump.
		bool setLength(uint8_t _length);
		// Clears every trig and every lock of every step the dump holds, past the length too.
		void clear();
		// The pattern's number, 0 to 127 (A01 to H16): the firmware stores a dump in the
		// slot it names, so a dump given another slot's number is a copy.
		bool setSlot(uint8_t _slot);
		// The Kit the pattern plays, 0 to 63: the machine loads it when it selects the pattern, or takes the
		// pattern as its current one; a Kit loaded makes it the current pattern's (mdEditorFirmwareTest)
		bool setKit(uint8_t _kit);
		// A track's steps made _steps', on every step the dump holds (past it, they are dropped): its trigs, its
		// own accent, slide and swing, its locks, each locked parameter on a row of its own. False, and nothing
		// changed, when the other tracks' rows leave too few of the 64 for its locks.
		bool setTrackSteps(uint8_t _track, const MdTrackSteps& _steps);
		// A step's accent, slide or swing below the pattern length: in the mask every
		// track follows (_track nullopt), or in a track's own (0..15)
		bool setFlag(StepFlag _flag, std::optional<uint8_t> _track, uint8_t _step, bool _on);
		// Whether a kind follows the tracks' own masks (EDIT ALL off) or every track's
		bool setFlagPerTrack(StepFlag _flag, bool _perTrack);
		bool setAccentAmount(uint8_t _amount);		// 0..127
		bool setSwingAmount(uint8_t _percent);		// 50..80
		// The swing amount's word as the dump holds it
		void setSwingWord(uint32_t _word);

		Message toDump() const;

	private:
		uint8_t stepCount() const { return m_extension.empty() ? 32 : 64; }
		uint8_t length() const { return m_plain[1]; }
		uint8_t& trigByte(uint8_t _track, uint8_t _step);
		bool hasTrig(uint8_t _track, uint8_t _step);
		uint8_t& maskByte(uint8_t _track, uint8_t _parameter) { return m_masks[_track * 4 + 3 - _parameter / 8]; }
		bool hasRow(uint8_t _track, uint8_t _parameter) { return (maskByte(_track, _parameter) >> (_parameter % 8)) & 1u; }
		size_t rowIndex(uint8_t _track, uint8_t _parameter);
		size_t rowCount();
		uint8_t& lockValue(size_t _row, uint8_t _step);
		bool rowEmpty(size_t _row);
		void insertRow(size_t _row);
		void removeRow(size_t _row);

		Message m_header;                  // F0 up to the pattern position, included
		std::array<uint8_t, 64> m_trigs{};
		std::array<uint8_t, 64> m_masks{};
		std::array<uint8_t, 16> m_swing{};
		std::array<uint8_t, 6> m_plain{};  // accent amount, length, double tempo, scale, kit, row count
		std::vector<uint8_t> m_locks;      // 64 rows of 32 steps
		std::vector<uint8_t> m_tail;
		std::vector<uint8_t> m_extension;  // 64-step form: steps 33 to 64 of the above
	};

	// The receiving half of a Global's MIDI SYNC page.
	struct GlobalSync
	{
		bool clockIn = false;
		bool transportIn = false;

		bool operator==(const GlobalSync& _other) const
		{
			return clockIn == _other.clockIn && transportIn == _other.transportIn;
		}
		bool operator!=(const GlobalSync& _other) const { return !(*this == _other); }
	};

	Message statusRequest(MachineModel _model, StatusParameter _parameter);
	Message globalRequest(MachineModel _model, uint8_t _slot);
	Message kitRequest(MachineModel _model, uint8_t _slot);
	Message patternRequest(MachineModel _model, uint8_t _slot);
	// These requests only inspect firmware state. They may be sent while the UW
	// factory image is being learned without making that image user-modified.
	bool isReadOnlyRequest(MachineModel _model, MessageView _message);
	Message kitSave(MachineModel _model, uint8_t _slot);
	// LOAD KIT ($58, MCL's MD::loadKit): the stored Kit becomes the live one, as stored; what the live Kit held
	// and was not saved is gone. SET STATUS with the Kit does the same on both machines (mdEditorFirmwareTest).
	Message kitLoad(MachineModel _model, uint8_t _slot);
	// SET STATUS with the pattern ($71 $04): the machine's pattern at once when stopped, at the end of the one
	// playing otherwise (patternChainFirmwareTest), its Kit loaded with it (mdEditorFirmwareTest)
	Message patternSelect(MachineModel _model, uint8_t _slot);
	// ASSIGN MACHINE ($5B) for a track of the live Kit. Machinedrum ids 128 and up
	// go out as id - 128 with the UW flag; the Monomachine form asks for no page
	// initialisation. Empty for a track or machine the model does not have.
	std::optional<Message> assignMachine(MachineModel _model, uint8_t _track, uint16_t _machine);
	// Machinedrum: one parameter (0..7) of a master effect of the live Kit, $5D to $60.
	// Empty for a parameter or value out of range.
	std::optional<Message> masterEffectChange(MasterEffect _effect, uint8_t _parameter, uint8_t _value);
	// Machinedrum: one field of a track's LFO (SET LFO PARAM, $62, after MCL's MD::setLFOParam), 0 the
	// destination track, 1 its parameter, 2 and 3 the shapes, 4 the update (LfoSettings). The firmware
	// clamps a value out of range (mdPlayheadProbe --lfo); empty for one here, or a track or field out of
	// range.
	std::optional<Message> lfoChange(uint8_t _track, uint8_t _field, uint8_t _value);
	// Machinedrum: the output of a track (SET TRACK ROUTING, $5C), kept in the Global.
	// Empty for a track or output out of range.
	std::optional<Message> trackRouting(uint8_t _track, TrackOutput _output);

	std::optional<StatusResponse> parseStatusResponse(MachineModel _model,
		MessageView _message);
	std::optional<StatusResponse> parseSetStatus(MachineModel _model,
		MessageView _message);
	std::optional<GlobalDump> parseGlobalDump(MachineModel _model,
		MessageView _message);
	std::optional<KitDump> parseKitDump(
		MachineModel _model, MessageView _message);
	// Machinedrum pattern dump ($67), 32- or 64-step form; only the first 32 steps are kept.
	std::optional<PatternDump> parseMdPatternDump(MessageView _message);

	// A Monomachine pattern dump ($67), its payload as MCL's MNMPattern lays it out: per track,
	// 64-bit masks of the steps with a trig and of what each trig starts (amp envelope, filter
	// envelope, LFOs), the note of every step and the lock rows; then the length, double tempo and
	// Kit. Slides, swing, the MIDI tracks and the arpeggiators are not read.
	struct MmPatternDump
	{
		static constexpr uint8_t TrackCount = 6;
		static constexpr uint8_t StepCount = 64;
		static constexpr uint8_t LockRowCount = 62;
		static constexpr uint8_t LockBitCount = 64;
		// A step's note, or a lock row's value, when the step has none
		static constexpr uint8_t None = 0xff;

		uint8_t slot = 0;			// 0..127, A01..H16
		uint8_t length = 16;		// steps, 1..64
		bool doubleTempo = false;
		uint8_t kit = 0;
		// Bit n: step n + 1
		std::array<uint64_t, TrackCount> trigs{};
		std::array<uint64_t, TrackCount> ampTrigs{};
		std::array<uint64_t, TrackCount> filterTrigs{};
		std::array<uint64_t, TrackCount> lfoTrigs{};
		// The MIDI note each step plays, None without a trig
		std::array<std::array<uint8_t, StepCount>, TrackCount> notes{};
		// Bit b: the parameter of lock mask bit b (mmLockBit) has a lock row
		std::array<uint64_t, TrackCount> lockMasks{};
		// The lock rows: row k belongs to the k-th set bit of the masks, in track then bit order
		std::vector<std::array<uint8_t, StepCount>> lockRows;
		// Slide and swing, a mask per track (no accent on the Monomachine)
		std::array<uint64_t, TrackCount> slides{};
		std::array<uint64_t, TrackCount> swings{};
		uint32_t swingAmount = 0;		// swingPercent

		bool hasTrig(uint8_t _track, uint8_t _step) const;
		std::optional<uint8_t> note(uint8_t _track, uint8_t _step) const;
		// Locked value on a step for the parameter of a lock mask bit, or nullopt
		std::optional<uint8_t> lock(uint8_t _track, uint8_t _bit, uint8_t _step) const;
		// Steps with a lock for the parameter of a lock mask bit, bit n for step n + 1
		uint64_t lockedSteps(uint8_t _track, uint8_t _bit) const;
	};
	std::optional<MmPatternDump> parseMmPatternDump(MessageView _message);

	// The lock mask bit of a Monomachine track parameter, page monomachine::Synthesis to Lfo3 and index
	// 0..7: its place among the track's parameters in the Kit (mmPatternFirmwareTest: a lock of bit 13,
	// AMP VOL, at 0 silences its step). Bits 56 to 63 hold parameters the editor does not name.
	constexpr uint8_t mmLockBit(const uint8_t _page, const uint8_t _index)
	{
		return static_cast<uint8_t>(_page * 8 + _index);
	}

	// A Monomachine pattern dump ($67) that can be edited and sent back. Its decoded payload is kept
	// whole; toDump() encodes it again with the edits.
	class MmPatternEditor
	{
	public:
		static std::optional<MmPatternEditor> fromDump(MessageView _message);

		// Sets a trig playing _note (0..127) on a step below the length. It starts the amp and filter
		// envelopes and the LFOs, as a trig placed on the machine does. nullopt clears the step's trig,
		// its note and its locks.
		bool setTrig(uint8_t _track, uint8_t _step, std::optional<uint8_t> _note);
		// Sets a locked value (0..127) on a step that has a trig, or clears it. The parameter of a lock
		// mask bit gets its row with its first lock and loses it with its last. False when the step has
		// no trig or all 62 rows are taken.
		bool setLock(uint8_t _track, uint8_t _bit, uint8_t _step, std::optional<uint8_t> _value);
		// 1 to 64 steps; trigs and locks past the length stay in the pattern
		bool setLength(uint8_t _length);
		// Clears every step's trigs of every kind, notes and locks, past the length too
		void clear();
		// The pattern's number, 0 to 127 (A01 to H16): received in SYSEX RECV's ORIG mode, a dump goes to the
		// slot it names, so a dump given another slot's number is a copy.
		bool setSlot(uint8_t _slot);
		// The Kit the pattern plays, 0 to 127, as on the Machinedrum
		bool setKit(uint8_t _kit);
		// A track's slide or swing on a step below the length (no accent on the Monomachine)
		bool setFlag(StepFlag _flag, uint8_t _track, uint8_t _step, bool _on);
		bool setSwingAmount(uint8_t _percent);		// 50..80

		Message toDump() const;

	private:
		uint64_t mask(size_t _offset) const;
		void setMask(size_t _offset, uint64_t _mask);
		size_t rowIndex(uint8_t _track, uint8_t _bit) const;
		size_t rowCount() const;
		uint8_t* row(size_t _row) { return m_payload.data() + rowPosition(_row); }
		static size_t rowPosition(size_t _row);

		Message m_header;					// F0 up to the pattern position, included
		std::vector<uint8_t> m_payload;		// decoded
	};
	// The same dump with another length (1..64), everything else kept, its payload encoded again
	std::optional<Message> withMmPatternLength(MessageView _message, uint8_t _length);

	// SET STATUS for the Global slot: the firmware reloads that slot, which is how
	// a Global dump written to the active slot takes effect.
	Message globalReload(MachineModel _model, uint8_t _slot);

	// CLOCK IN and TRANSPORT IN of a Global dump.
	std::optional<GlobalSync> parseGlobalSync(MachineModel _model,
		MessageView _message);
	// The same Global dump with CLOCK IN and TRANSPORT IN replaced, everything
	// else kept, and its checksum and length recomputed.
	std::optional<Message> withGlobalSync(MachineModel _model,
		MessageView _message, GlobalSync _sync);
}
