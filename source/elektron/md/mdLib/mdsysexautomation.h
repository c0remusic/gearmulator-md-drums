#pragma once

#include "mdautomation.h"

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
	};

	// The first 32 steps of a Machinedrum pattern, as far as the editor shows them.
	struct PatternDump
	{
		uint8_t slot = 0;      // 0..127, A01..H16
		uint8_t length = 16;   // steps, 1..64
		std::array<uint32_t, 16> trigs{};      // bit n: a trig on step n + 1
		std::array<uint32_t, 16> lockMasks{};  // bit p: parameter p (0..23) has a lock row
		// The 64 lock rows: row k belongs to the k-th set bit of the masks, in
		// track then parameter order; 128 and above means no lock on that step.
		std::vector<std::array<uint8_t, 32>> lockRows;

		bool hasTrig(uint8_t _track, uint8_t _step) const;
		// Locked value of parameter _parameter (0..23: synthesis, effects, routing)
		// on a step, or nullopt.
		std::optional<uint8_t> lock(uint8_t _track, uint8_t _parameter, uint8_t _step) const;
	};

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
		// Sets the length, 1 to the steps the dump holds (32, or 64 in the long form).
		// Trigs and locks past the length stay in the dump.
		bool setLength(uint8_t _length);

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

		Message m_header;                  // F0 up to the pattern position
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
	// ASSIGN MACHINE ($5B) for a track of the live Kit. Machinedrum ids 128 and up
	// go out as id - 128 with the UW flag; the Monomachine form asks for no page
	// initialisation. Empty for a track or machine the model does not have.
	std::optional<Message> assignMachine(MachineModel _model, uint8_t _track, uint16_t _machine);
	// Machinedrum: one parameter (0..7) of a master effect of the live Kit, $5D to $60.
	// Empty for a parameter or value out of range.
	std::optional<Message> masterEffectChange(MasterEffect _effect, uint8_t _parameter, uint8_t _value);
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

	// What a pattern chain needs of a Monomachine pattern dump ($67). Its payload follows MCL's
	// MNMPattern; the trigs, notes and locks are not read.
	struct MmPatternDump
	{
		uint8_t slot = 0;			// 0..127, A01..H16
		uint8_t length = 16;		// steps, 1..64
		bool doubleTempo = false;
		uint8_t kit = 0;
	};
	std::optional<MmPatternDump> parseMmPatternDump(MessageView _message);
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
