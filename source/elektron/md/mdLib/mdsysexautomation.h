#pragma once

#include "mdautomation.h"

#include <array>
#include <cstdint>
#include <optional>
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

	struct GlobalDump
	{
		uint8_t slot;
		uint8_t baseChannel;
	};

	struct KitDump
	{
		uint8_t slot;
		std::vector<ParameterChange> parameters;
		// Machine id per track (see mdmachines.h), empty when the dump is too short to hold them.
		std::vector<uint16_t> machines;
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
