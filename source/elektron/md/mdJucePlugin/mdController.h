#pragma once

#include "jucePluginLib/controller.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mdautomationsync.h"
#include "mdLib/mdmachines.h"
#include "mdLib/mdsysexautomation.h"
#include "mdLib/mdtypes.h"
#include "mdRealtimeQueue.h"

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	struct ControllerAutomationTestAccess;

	class Controller : public pluginLib::Controller
	{
	public:
		explicit Controller(AudioPluginAudioProcessor& _p);
		~Controller() override;

		void onStateLoaded() override;

		uint8_t getPartCount() const override;

		bool parseSysexMessage(const pluginLib::SysEx&,
			synthLib::MidiEventSource) override;
		bool parseControllerMessage(const synthLib::SMidiEvent& _event) override;
		bool parseMidiMessage(const synthLib::SMidiEvent& _event) override;
		void processRealtimeParameterChanges(size_t _maximumChanges) override;
		void processOfflineControllerWork() override
		{
			processPendingMidiMessages();
		}

		void sendParameterChange(const pluginLib::Parameter& _parameter,
			pluginLib::ParamValue _value, pluginLib::Parameter::Origin _origin) override;

		bool isAutomationSynchronized() const
		{
			return m_automationReady.load(std::memory_order_acquire);
		}
		uint8_t getAutomationBaseChannel() const
		{
			return m_baseChannel.load(std::memory_order_acquire);
		}
		bool hasAutomationGlobalSnapshot() const
		{
			return m_haveGlobal.load(std::memory_order_acquire);
		}
		bool hasAutomationKitSnapshot() const
		{
			return m_haveKit.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationChangeCount() const
		{
			return m_transmittedAutomationChanges.load(std::memory_order_acquire);
		}
		uint64_t getTransmittedAutomationDigest() const
		{
			return m_transmittedAutomationDigest.load(std::memory_order_acquire);
		}
		uint64_t getRealtimeAutomationOverflowCount() const
		{
			return m_realtimeAutomationOverflows.load(std::memory_order_acquire);
		}
		uint64_t getSynchronizationRequestCount() const
		{
			return m_synchronizationRequests.load(std::memory_order_acquire);
		}
		int getLastFirmwareKitValue(const pluginLib::Parameter& _parameter) const;

		// Machine of a track as last read from a Kit dump or assigned through
		// assignMachine, md::machines::g_unknown when neither happened yet. A machine
		// changed on the front panel is not seen: a Kit dump request returns the
		// stored Kit, not the live one.
		uint16_t getTrackMachine(uint8_t _part) const;
		// Increments whenever a track's machine changes.
		uint64_t getMachineRevision() const { return m_machineRevision.load(std::memory_order_acquire); }
		// Sends ASSIGN MACHINE for a track of the live Kit. The firmware then sets
		// values the plug-in cannot read back (a Kit request returns the stored Kit):
		// the Machinedrum loads the machine's defaults on its synthesis, effects and
		// routing pages, the Monomachine adapts its synthesis page. Those values become
		// unknown (isValueKnown) instead of being overwritten from the cache.
		// False for a track or machine the model does not have.
		bool assignMachine(uint8_t _part, uint16_t _machine);
		// False while the firmware holds a value the plug-in could not read, until an
		// applied Kit dump, a value the firmware sends, or a host or editor write
		// makes it known again.
		bool isValueKnown(uint8_t _page, uint8_t _part, uint8_t _index) const;
		// Increments whenever a value becomes unknown or known again.
		uint64_t getValueStateRevision() const { return m_valueStateRevision.load(std::memory_order_acquire); }

		// The Global, Kit and pattern the firmware has selected, 0xff until a status
		// reply told them. Status is polled every 5 s, so a selection made on the
		// front panel shows within that time.
		uint8_t getCurrentGlobal() const { return m_currentGlobal.load(std::memory_order_acquire); }
		uint8_t getCurrentKit() const { return m_currentKit.load(std::memory_order_acquire); }
		uint8_t getCurrentPattern() const { return m_currentPattern.load(std::memory_order_acquire); }
		// Name of the current Kit, empty until its dump arrived. A Kit dump request
		// returns the stored Kit: a name changed on the front panel and not saved
		// does not show.
		std::string getKitName() const;
		// Increments whenever the current Kit, its name or the current pattern changes.
		uint64_t getSelectionRevision() const { return m_selectionRevision.load(std::memory_order_acquire); }

		// Machinedrum: a master effect parameter of the live Kit as last read from a
		// Kit dump or set through setMasterEffect, nullopt while unknown. As for the
		// machines, an applied dump replaces the values and an inspection dump of the
		// stored Kit only fills unknown ones.
		std::optional<uint8_t> getMasterEffect(md::automation::sysex::MasterEffect _effect, uint8_t _parameter) const;
		// Sends one master effect parameter ($5D to $60) and keeps it. False on the
		// Monomachine, out of range, or while the firmware is not ready.
		bool setMasterEffect(md::automation::sysex::MasterEffect _effect, uint8_t _parameter, uint8_t _value);
		// Increments whenever a master effect value changes or becomes known.
		uint64_t getMasterEffectRevision() const { return m_masterEffectRevision.load(std::memory_order_acquire); }

		// Machinedrum: the output of a track as last read from the Global dump (read
		// again with every 5 s poll) or set through setTrackOutput, nullopt while unknown.
		std::optional<md::automation::sysex::TrackOutput> getTrackOutput(uint8_t _track) const;
		// Sends SET TRACK ROUTING ($5C), which the firmware keeps in its Global, and
		// keeps it. False on the Monomachine, out of range, or while the firmware is
		// not ready.
		bool setTrackOutput(uint8_t _track, md::automation::sysex::TrackOutput _output);
		// Increments whenever a track output changes or becomes known.
		uint64_t getRoutingRevision() const { return m_routingRevision.load(std::memory_order_acquire); }

		// BIBLIO: the stored Kits, name and machines, read one by one with Kit requests
		// ($53) once readKitLibrary is called; a slot not answered within 2 s is
		// skipped. Read only: nothing is loaded.
		struct LibraryKit
		{
			bool read = false;
			std::string name;
			std::vector<uint16_t> machines;
		};
		// Starts, or starts again, reading every Kit. False while the firmware is not ready.
		bool readKitLibrary();
		bool isReadingKitLibrary() const { return m_libraryReading.load(std::memory_order_acquire); }
		size_t getKitLibrarySize() const { return m_model == md::MachineModel::Monomachine ? 128 : 64; }
		// Slots answered or skipped since the reading started
		size_t getKitLibraryProgress() const { return m_libraryProgress.load(std::memory_order_acquire); }
		std::optional<LibraryKit> getLibraryKit(uint8_t _slot) const;
		// Increments whenever a library Kit is read or the reading starts or ends
		uint64_t getKitLibraryRevision() const { return m_libraryRevision.load(std::memory_order_acquire); }

		// Machinedrum only: asks the firmware for the current pattern number, then
		// for that pattern's dump. False on the Monomachine or while the firmware is
		// not ready. A pattern selected through SET STATUS is read again by itself.
		bool requestPattern();
		// The pattern as last read, nullopt before the first dump.
		std::optional<md::automation::sysex::PatternDump> getPattern() const;
		// Increments whenever a new pattern dump is stored or the pattern is edited.
		uint64_t getPatternRevision() const { return m_patternRevision.load(std::memory_order_acquire); }

		// Machinedrum only: edit the pattern as last read. The edit shows at once in
		// getPattern(); sendPattern() writes it to the firmware. False without a
		// pattern, or when md::automation::sysex::MdPatternEditor refuses the edit.
		bool setPatternTrig(uint8_t _track, uint8_t _step, bool _on);
		bool setPatternLock(uint8_t _track, uint8_t _parameter, uint8_t _step, std::optional<uint8_t> _value);
		// Writes the edited pattern back to its slot ($67), then reads it again: the
		// reply tells whether the firmware kept it. False when there is nothing to send.
		bool sendPattern();
		enum class PatternWrite : uint8_t
		{
			None,       // nothing written yet
			Pending,    // written, waiting for the pattern to be read back
			Written,    // read back as sent
			Refused     // read back different from what was sent
		};
		PatternWrite getPatternWrite() const { return m_patternWrite.load(std::memory_order_acquire); }
		void requestAutomationState();
		std::vector<uint8_t> createAutomationSnapshot() const;
		bool restoreAutomationSnapshot(const std::vector<uint8_t>& _snapshot);

	private:
		friend struct ControllerAutomationTestAccess;
		bool editPattern(const std::function<bool(md::automation::sysex::MdPatternEditor&)>& _edit);
		struct Address
		{
			uint8_t page = 0;
			uint8_t track = 0;
			uint8_t index = 0;

			bool operator<(const Address& _other) const
			{
				if(page != _other.page) return page < _other.page;
				if(track != _other.track) return track < _other.track;
				return index < _other.index;
			}
		};

		struct AutomationSlot
		{
			Address address;
			// One atomic publication is the source of truth for this address. The low
			// byte is the value, the middle bits identify the publication, and the top
			// bit means that the value still needs to reach the firmware. Queue entries
			// are only delivery hints and may safely be stale or absent.
			std::atomic<uint64_t> publication{0};
			// Publications older than this revision are intentionally superseded. UI
			// edits and overflow advance the floor; ordinary DAW writes do not, so
			// every queued host value retains its FIFO delivery semantics.
			std::atomic<uint64_t> deliveryFloorRevision{0};
			// Exact publication whose queue hint was dropped. A versioned marker avoids
			// clearing a newer producer's recovery obligation after a concurrent scan.
			std::atomic<uint64_t> scanPublication{0};
			// Raw value from the most recently accepted stored-Kit dump. This is
			// diagnostic truth, distinct from the live/session publication above.
			std::atomic<uint16_t> lastFirmwareKitValue{0x100};
			// The firmware changed this value without telling (machine assignment);
			// the publication above still holds the value from before.
			std::atomic<bool> valueUnknown{false};
		};

		struct QueuedAutomationChange
		{
			md::automation::ParameterChange change;
			size_t slotIndex = 0;
			uint64_t publication = 0;
		};

		static constexpr size_t RealtimeAutomationCapacity = 4096;
		static constexpr uint64_t PublicationDirty = uint64_t{1} << 63;
		static constexpr uint64_t PublicationValueMask = 0x7f;
		static constexpr uint64_t PublicationRevisionMask =
			~(PublicationDirty | uint64_t{0xff});
		static_assert(std::atomic<uint64_t>::is_always_lock_free,
			"realtime automation requires lock-free 64-bit atomics");

		pluginLib::Parameter* createParameter(pluginLib::Controller& _controller,
			const pluginLib::Description& _description, uint8_t _part, int _uid,
			const pluginLib::Parameter::PartFormatter& _formatter) override;
		void requestKitState();
		void requestAutomationState(bool _forceApplyKitDump);
		void transmitParameterChange(const md::automation::ParameterChange& _change);
		bool transmitRealtimeParameterChange(
			const md::automation::ParameterChange& _change);
		void drainRealtimeParameterChanges(size_t _maximumChanges, bool _realtime);
		bool deliverAutomationPublication(const QueuedAutomationChange& _queued,
			bool _realtime);
		uint64_t createPublication(uint8_t _value, bool _dirty);
		void publishAutomationIntent(const md::automation::ParameterChange& _change,
			bool _supersedeEarlier = false);
		uint8_t publishFirmwareValue(const Address& _address, uint8_t _value,
			uint64_t _kitRequestRevision = 0);
		static uint8_t publicationValue(uint64_t _publication);
		static uint64_t publicationRevision(uint64_t _publication);
		static bool publicationIsDirty(uint64_t _publication);
		AutomationSlot* findAutomationSlot(const Address& _address);
		const AutomationSlot* findAutomationSlot(const Address& _address) const;
		void markValueKnown(AutomationSlot& _slot);
		void completeSynchronizationIfReady();
		bool firmwareReadyForAutomation() const;
		void applyKitParameters(const std::vector<md::automation::ParameterChange>& _changes);
		// An applied dump replaces every track's machine; an inspection dump only
		// fills tracks whose machine is still unknown.
		void storeKitMachines(const std::vector<uint16_t>& _machines, bool _authoritative);
		// Same policy for the master effects
		void storeMasterEffects(const md::automation::sysex::MasterEffects& _effects, bool _authoritative);
		// Asks for one library Kit, or ends the reading past the last slot
		void requestLibraryKit(size_t _slot, uint64_t _now);
		void sendEditorSysex(const md::automation::sysex::Message& _message) const;
		void onControllerTimer() override;
		void sendMissingSynchronizationRequests();
		void sendSynchronizationRequest(const pluginLib::SysEx& _message) const;
		static uint64_t milliseconds();

		const md::MachineModel m_model;
		std::atomic<uint8_t> m_baseChannel{0x7f};
		std::atomic<bool> m_haveGlobal{false};
		std::atomic<bool> m_haveKit{false};
		std::atomic<bool> m_automationReady{false};
		std::atomic<uint64_t> m_lastStatePollMs{0};
		std::atomic<uint64_t> m_kitDumpRequestRevision{0};
		std::atomic<bool> m_forceApplyRequestedKitDump{false};
		// Unfulfilled baseline/reload intent survives retry status barriers and
		// is consumed only by an accepted Kit dump. False means inspection only.
		std::atomic<bool> m_applyRequestedKitDump{true};
		// The timer/offline consumer is serialized by pluginLib::Controller, but
		// explicit state loads and program changes may request a resync from another
		// non-realtime thread. Keep the protocol trackers and their coupled request
		// flags single-owner across both entry paths. Recursive locking is deliberate:
		// parsing SET STATUS delegates to requestKitState(), and request helpers delegate
		// to sendMissingSynchronizationRequests(). This mutex is never taken by the
		// realtime parameter-publication path.
		std::recursive_mutex m_synchronizationLock;
		md::automation::DumpRequestTracker m_globalSynchronization{true};
		md::automation::DumpRequestTracker m_kitSynchronization{false};
		std::atomic<uint64_t> m_synchronizationEpoch{0};
		std::atomic<uint64_t> m_nextAutomationRevision{1};
		std::atomic<uint64_t> m_transmittedAutomationChanges{0};
		std::atomic<uint64_t> m_transmittedAutomationDigest{14695981039346656037ull};
		std::atomic<uint8_t> m_currentGlobal{0xff};
		std::atomic<uint8_t> m_currentKit{0xff};
		std::atomic<uint8_t> m_currentPattern{0xff};
		mutable std::mutex m_kitNameMutex;
		std::string m_kitName;                      // of m_currentKit, under m_kitNameMutex
		std::atomic<uint64_t> m_selectionRevision{0};
		std::array<std::atomic<uint16_t>, md::automation::machinedrum::TrackCount> m_trackMachines{};
		std::atomic<uint64_t> m_machineRevision{0};
		std::atomic<uint64_t> m_valueStateRevision{0};
		// [effect * 8 + parameter], 0xff while unknown
		std::array<std::atomic<uint8_t>, md::automation::sysex::MasterEffectCount
			* md::automation::sysex::MasterEffectParameters> m_masterEffects{};
		std::atomic<uint64_t> m_masterEffectRevision{0};
		// TrackOutput per track, 0xff while unknown
		std::array<std::atomic<uint8_t>, md::automation::machinedrum::TrackCount> m_trackOutputs{};
		std::atomic<uint64_t> m_routingRevision{0};
		// $5C messages sent, and their count when the pending Global request went out:
		// a Global dump requested before a routing write does not show it yet.
		uint64_t m_routingWrites = 0;
		uint64_t m_routingWritesAtGlobalRequest = 0;
		mutable std::mutex m_libraryMutex;
		std::vector<LibraryKit> m_library;          // under m_libraryMutex
		size_t m_libraryWaiting = 0;                // slot asked for, under m_synchronizationLock
		uint64_t m_libraryRequestMs = 0;
		std::atomic<bool> m_libraryReading{false};
		std::atomic<size_t> m_libraryProgress{0};
		std::atomic<uint64_t> m_libraryRevision{0};
		mutable std::mutex m_patternMutex;
		std::optional<md::automation::sysex::PatternDump> m_pattern;
		// The dump behind m_pattern, edits included; both under m_patternMutex.
		md::automation::sysex::Message m_patternDump;
		bool m_patternEdited = false;               // edits not sent yet
		uint32_t m_patternWritesInFlight = 0;       // sent, not read back yet
		std::optional<md::automation::sysex::PatternDump> m_patternSent;
		std::atomic<PatternWrite> m_patternWrite{PatternWrite::None};
		std::atomic<uint64_t> m_patternRevision{0};
		std::atomic<bool> m_patternWanted{false};
		std::atomic<uint8_t> m_patternRequestedSlot{0xff};
		std::deque<AutomationSlot> m_automationSlots;
		std::map<Address, size_t> m_automationSlotIndices;
		RealtimeQueue<QueuedAutomationChange,
			RealtimeAutomationCapacity> m_realtimeAutomationChanges;
		size_t m_dirtyScanPosition = 0;
		bool m_minimumBudgetRecoveryTurn = true;
		std::atomic_flag m_realtimeAutomationDrain = ATOMIC_FLAG_INIT;
		std::atomic<uint64_t> m_realtimeAutomationOverflows{0};
		mutable std::atomic<uint64_t> m_synchronizationRequests{0};
		bool m_syntheticFirmwareReadyForTests = false;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Controller)
	};
}
