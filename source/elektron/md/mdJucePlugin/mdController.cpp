#include "mdController.h"

#include "mdPluginProcessor.h"
#include "mdLib/mdautomation.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdsysexautomation.h"

#include <algorithm>
#include <bitset>
#include <chrono>
#include <set>

namespace mdJucePlugin
{
	namespace
	{
		// What the step grid shows of a pattern: trigs, lock rows and length.
		bool samePattern(const md::automation::sysex::PatternDump& _a, const md::automation::sysex::PatternDump& _b)
		{
			return _a.length == _b.length && _a.trigs == _b.trigs && _a.lockMasks == _b.lockMasks && _a.lockRows == _b.lockRows;
		}

		// The Monomachine encodes a dump its own way (a byte more or less): the pattern, not the bytes
		bool sameMmPattern(const md::automation::sysex::MmPatternDump& _a, const md::automation::sysex::MmPatternDump& _b)
		{
			return _a.length == _b.length && _a.kit == _b.kit && _a.trigs == _b.trigs && _a.ampTrigs == _b.ampTrigs
				&& _a.notes == _b.notes && _a.lockMasks == _b.lockMasks && _a.lockRows == _b.lockRows;
		}

		constexpr uint64_t g_dumpRequestRetryMs = 2000;
		constexpr uint8_t g_snapshotVersion = 2;
		constexpr uint8_t g_snapshotComplete = 1u << 0;
		constexpr uint8_t g_snapshotEntryPending = 1u << 0;

		class AutomationParameter final : public pluginLib::Parameter
		{
		public:
			using pluginLib::Parameter::Parameter;

		protected:
			bool shouldSendRepeatedHostValues() const override { return true; }
		};

		pluginLib::SysEx toPluginSysex(
			const md::automation::sysex::Message& _message)
		{
			return pluginLib::SysEx(_message.begin(), _message.end());
		}
	}

	Controller::Controller(AudioPluginAudioProcessor& _p)
		: pluginLib::Controller(_p, _p.getModel() == md::MachineModel::Monomachine
			? "parameterDescriptions_mm.json" : "parameterDescriptions_md.json")
		, m_model(_p.getModel())
	{
		for(auto& machine : m_trackMachines)
			machine.store(md::machines::g_unknown, std::memory_order_relaxed);
		for(auto& value : m_masterEffects)
			value.store(0xff, std::memory_order_relaxed);
		for(auto& output : m_trackOutputs)
			output.store(0xff, std::memory_order_relaxed);
		registerParams(_p, [](const uint8_t _part, const bool _nonPartSensitive)
		{
			return _nonPartSensitive ? juce::String("Global")
				: juce::String("Track ") + juce::String(_part + 1);
		});
		for(const auto& [address, parameters] : getExposedParameters())
		{
			(void)parameters;
			const Address automationAddress{address.page, address.partNum,
				address.paramNum};
			const auto slotIndex = m_automationSlots.size();
			m_automationSlots.emplace_back();
			auto& slot = m_automationSlots.back();
			slot.address = automationAddress;
			m_automationSlotIndices.emplace(automationAddress, slotIndex);
		}

		// Give mute (which is not part of a Kit dump) a defined initial cache value.
		// The other values are replaced by the firmware snapshot below.
		for(const auto& [address, parameters] : getExposedParameters())
		{
			for(auto* const parameter : parameters)
				parameter->setValueFromSynth(parameter->getDefault(),
					pluginLib::Parameter::Origin::PresetChange);
			if(auto* const slot = findAutomationSlot(
				{address.page, address.partNum, address.paramNum}))
			{
				slot->publication.store(createPublication(static_cast<uint8_t>(
					std::clamp(parameters.front()->getDefault(), 0, 127)), false),
					std::memory_order_release);
			}
		}

		requestAutomationState();
	}

	Controller::~Controller()
	{
		// Stop the base Timer while all derived synchronization members still exist.
		// Waiting until pluginLib::Controller's destructor would leave a teardown
		// window in which its callback can dispatch into partially destroyed state.
		stopControllerTimer();
	}

	uint8_t Controller::getPartCount() const
	{
		return m_model == md::MachineModel::Monomachine
			? md::automation::monomachine::TrackCount
			: md::automation::machinedrum::TrackCount;
	}

	pluginLib::Parameter* Controller::createParameter(
		pluginLib::Controller& _controller,
		const pluginLib::Description& _description, const uint8_t _part,
		const int _uid, const pluginLib::Parameter::PartFormatter& _formatter)
	{
		return new AutomationParameter(_controller, _description, _part, _uid,
			_formatter);
	}

	void Controller::onStateLoaded()
	{
		// Loading/replacing the emulated device establishes a new authoritative
		// baseline even when it selects the same numbered Kit as the old device.
		// Restored AUTO publications remain dirty and therefore still win when the
		// correlated Kit dump is applied.
		requestAutomationState(true);
	}

	std::vector<uint8_t> Controller::createAutomationSnapshot() const
	{
		const auto epoch = m_synchronizationEpoch.load(std::memory_order_acquire);
		const auto synchronized = m_automationReady.load(std::memory_order_acquire)
			&& m_haveGlobal.load(std::memory_order_acquire)
			&& m_haveKit.load(std::memory_order_acquire)
			&& m_currentKit.load(std::memory_order_acquire) != 0xff;
		std::vector<uint64_t> publications;
		publications.reserve(getExposedParameters().size());
		bool hasPendingIntent = false;
		bool hasUnknownValue = false;
		for(const auto& [address, parameters] : getExposedParameters())
		{
			if(parameters.empty())
				return {};
			const auto* const slot = findAutomationSlot(
				{address.page, address.partNum, address.paramNum});
			if(slot == nullptr)
				return {};
			const auto publication = slot->publication.load(std::memory_order_acquire);
			publications.push_back(publication);
			hasPendingIntent |= publicationIsDirty(publication);
			hasUnknownValue |= slot->valueUnknown.load(std::memory_order_acquire);
		}
		// A value the firmware changed unseen is no baseline: restoring the cached
		// one would overwrite what the firmware holds.
		const auto complete = synchronized && !hasUnknownValue;
		// Before the first coherent firmware snapshot, persist only meaningful host/UI
		// intent. This avoids turning constructor defaults into writes merely because a
		// host saved while the machine was still booting.
		if(!complete && !hasPendingIntent)
			return {};
		std::vector<uint8_t> result;
		result.reserve(7 + getExposedParameters().size() * 5);
		result.push_back(g_snapshotVersion);
		result.push_back(static_cast<uint8_t>(m_model));
		result.push_back(getAutomationBaseChannel());
		result.push_back(m_currentKit.load(std::memory_order_acquire));
		const auto count = static_cast<uint16_t>(getExposedParameters().size());
		result.push_back(static_cast<uint8_t>(count >> 8));
		result.push_back(static_cast<uint8_t>(count & 0xff));
		result.push_back(complete ? g_snapshotComplete : 0);
		size_t publicationIndex = 0;
		for(const auto& [address, parameters] : getExposedParameters())
		{
			(void)parameters;
			const auto publication = publications[publicationIndex++];
			result.push_back(address.page);
			result.push_back(address.partNum);
			result.push_back(address.paramNum);
			result.push_back(publicationValue(publication));
			result.push_back(publicationIsDirty(publication)
				? g_snapshotEntryPending : 0);
		}
		if(complete && (!m_automationReady.load(std::memory_order_acquire)
			|| epoch != m_synchronizationEpoch.load(std::memory_order_acquire)))
			return {};
		return result;
	}

	bool Controller::restoreAutomationSnapshot(
		const std::vector<uint8_t>& _snapshot)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(_snapshot.size() < 6 || (_snapshot[0] != 1
			&& _snapshot[0] != g_snapshotVersion)
			|| _snapshot[1] != static_cast<uint8_t>(m_model))
			return false;
		const auto version = _snapshot[0];
		const auto headerSize = version == 1 ? size_t{6} : size_t{7};
		const auto entrySize = version == 1 ? size_t{4} : size_t{5};
		if(_snapshot.size() < headerSize)
			return false;
		const auto count = static_cast<size_t>((_snapshot[4] << 8) | _snapshot[5]);
		const auto kitCount = m_model == md::MachineModel::Monomachine ? 128 : 64;
		const auto complete = version == 1
			|| (_snapshot[6] & g_snapshotComplete) != 0;
		if(_snapshot.size() != headerSize + count * entrySize
			|| count != getExposedParameters().size()
			|| (version == g_snapshotVersion && (_snapshot[6] & ~g_snapshotComplete))
			|| !(_snapshot[2] < 16 || _snapshot[2] == 0x7f)
			|| (complete && _snapshot[3] >= kitCount)
			|| (!complete && _snapshot[3] != 0xff && _snapshot[3] >= kitCount))
			return false;
		std::set<Address> addresses;
		bool hasPendingIntent = false;
		for(size_t position = headerSize; position < _snapshot.size();
			position += entrySize)
		{
			const Address address{_snapshot[position], _snapshot[position + 1],
				_snapshot[position + 2]};
			if(_snapshot[position + 3] > 127 || !addresses.insert(address).second
				|| (version == g_snapshotVersion
					&& (_snapshot[position + 4] & ~g_snapshotEntryPending))
				|| findAutomationSlot(address) == nullptr)
				return false;
			hasPendingIntent |= version == 1
				|| (_snapshot[position + 4] & g_snapshotEntryPending) != 0;
		}
		if(!complete && !hasPendingIntent)
			return false;

		m_automationReady.store(false, std::memory_order_release);
		m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
		if(complete)
		{
			m_baseChannel.store(_snapshot[2], std::memory_order_release);
			m_currentKit.store(_snapshot[3], std::memory_order_release);
		}
		else
		{
			// A partial snapshot deliberately contains no baseline for clean entries.
			// Forget a prior session's Kit identity so the next correlated dump rebuilds
			// that baseline; dirty entries below still survive publishFirmwareValue().
			m_currentKit.store(0xff, std::memory_order_release);
		}
		for(size_t position = headerSize; position < _snapshot.size();
			position += entrySize)
		{
			const auto pending = complete || version == 1
				|| (_snapshot[position + 4] & g_snapshotEntryPending) != 0;
			if(!pending)
				continue;
			const Address address{_snapshot[position], _snapshot[position + 1],
				_snapshot[position + 2]};
			const auto& parameters = findSynthParam(_snapshot[position + 1],
				_snapshot[position], _snapshot[position + 2]);
			if(parameters.empty())
				return false;
			for(auto* const parameter : parameters)
				parameter->setValueFromSynth(_snapshot[position + 3],
					pluginLib::Parameter::Origin::PresetChange);
			publishAutomationIntent({address.page, address.track, address.index,
				_snapshot[position + 3]}, true);
		}
		// A direct restore is a complete state transition, not merely a parser
		// helper. Reset/request here so standalone and wrapper callers cannot leave
		// the controller with Ready trackers but invalidated snapshot flags.
		requestAutomationState();
		getProcessor().updateHostDisplay(
			juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
		return true;
	}

	void Controller::requestAutomationState()
	{
		requestAutomationState(false);
	}

	void Controller::requestAutomationState(const bool _forceApplyKitDump)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		m_automationReady.store(false, std::memory_order_release);
		m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
		m_haveGlobal.store(false, std::memory_order_release);
		m_haveKit.store(false, std::memory_order_release);
		m_currentGlobal.store(0xff, std::memory_order_release);
		// Retain the selected Kit identity. A request for the same slot returns the
		// stored Kit, not its unsaved live edit buffer, so applying it would roll
		// back host/front-panel changes already observed in this session.
		m_forceApplyRequestedKitDump.store(_forceApplyKitDump,
			std::memory_order_release);
		m_globalSynchronization.reset();
		m_kitSynchronization.reset();
		m_kitDumpRequestRevision.store(0, std::memory_order_release);
		sendMissingSynchronizationRequests();
	}

	uint16_t Controller::getTrackMachine(const uint8_t _part) const
	{
		return _part < getPartCount() ? m_trackMachines[_part].load(std::memory_order_acquire)
			: md::machines::g_unknown;
	}

	void Controller::storeKitMachines(const std::vector<uint16_t>& _machines,
		const bool _authoritative)
	{
		const auto count = std::min<size_t>(_machines.size(), getPartCount());
		for(size_t track = 0; track < count; ++track)
		{
			auto& stored = m_trackMachines[track];
			const auto previous = stored.load(std::memory_order_acquire);
			if(!_authoritative && previous != md::machines::g_unknown)
				continue;
			if(previous == _machines[track])
				continue;
			stored.store(_machines[track], std::memory_order_release);
			m_machineRevision.fetch_add(1, std::memory_order_acq_rel);
		}
	}

	bool Controller::assignMachine(const uint8_t _part, const uint16_t _machine)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(_part >= getPartCount())
			return false;
		const auto message = md::automation::sysex::assignMachine(m_model, _part, _machine);
		if(!message)
			return false;

		sendEditorSysex(*message);

		// What the firmware does to the track's values (mdEditorFirmwareTest): the
		// Machinedrum loads the new machine's defaults on its synthesis, effects and
		// routing pages and keeps the level; the Monomachine, told not to initialise,
		// keeps its pages but adapts some synthesis values to the new machine. A value
		// not delivered yet reaches the firmware after the assignment and stays known.
		const auto lastChangedPage = m_model == md::MachineModel::Monomachine
			? md::automation::monomachine::Synthesis : md::automation::machinedrum::Routing;
		bool changed = false;
		for(auto& slot : m_automationSlots)
		{
			if(slot.address.track != _part || slot.address.page > lastChangedPage
				|| publicationIsDirty(slot.publication.load(std::memory_order_acquire)))
				continue;
			changed |= !slot.valueUnknown.exchange(true, std::memory_order_acq_rel);
		}
		if(changed)
			m_valueStateRevision.fetch_add(1, std::memory_order_acq_rel);

		if(m_trackMachines[_part].exchange(_machine, std::memory_order_acq_rel) != _machine)
			m_machineRevision.fetch_add(1, std::memory_order_acq_rel);
		// Until the live Kit shows it, the machine it still shows is not taken for a front panel change
		m_assignmentMs[_part] = std::max<uint64_t>(milliseconds(), 1);
		return true;
	}

	void Controller::storeMasterEffects(const md::automation::sysex::MasterEffects& _effects, const bool _authoritative)
	{
		bool changed = false;
		for(uint8_t effect = 0; effect < _effects.size(); ++effect)
		{
			for(uint8_t parameter = 0; parameter < _effects[effect].size(); ++parameter)
			{
				auto& stored = m_masterEffects[effect * md::automation::sysex::MasterEffectParameters + parameter];
				const auto previous = stored.load(std::memory_order_acquire);
				if((!_authoritative && previous != 0xff) || previous == _effects[effect][parameter])
					continue;
				stored.store(_effects[effect][parameter], std::memory_order_release);
				changed = true;
			}
		}
		if(changed)
			m_masterEffectRevision.fetch_add(1, std::memory_order_acq_rel);
	}

	std::optional<uint8_t> Controller::getMasterEffect(const md::automation::sysex::MasterEffect _effect,
		const uint8_t _parameter) const
	{
		const auto effect = static_cast<uint8_t>(_effect);
		if(effect >= md::automation::sysex::MasterEffectCount || _parameter >= md::automation::sysex::MasterEffectParameters)
			return std::nullopt;
		const auto value = m_masterEffects[effect * md::automation::sysex::MasterEffectParameters + _parameter]
			.load(std::memory_order_acquire);
		return value <= 0x7f ? std::optional<uint8_t>(value) : std::nullopt;
	}

	bool Controller::setMasterEffect(const md::automation::sysex::MasterEffect _effect, const uint8_t _parameter,
		const uint8_t _value)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(m_model != md::MachineModel::Machinedrum || !firmwareReadyForAutomation())
			return false;
		const auto message = md::automation::sysex::masterEffectChange(_effect, _parameter, _value);
		if(!message)
			return false;
		sendEditorSysex(*message);
		m_masterEffectEditMs = milliseconds();
		auto& stored = m_masterEffects[static_cast<uint8_t>(_effect) * md::automation::sysex::MasterEffectParameters + _parameter];
		if(stored.exchange(_value, std::memory_order_acq_rel) != _value)
			m_masterEffectRevision.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	namespace
	{
		constexpr uint64_t g_lfoKnown = uint64_t{1} << 40;

		uint64_t packLfo(const md::LfoSettings& _lfo)
		{
			return g_lfoKnown | _lfo.track | uint64_t{_lfo.parameter} << 8 | uint64_t{_lfo.shape1} << 16
				| uint64_t{_lfo.shape2} << 24 | uint64_t{_lfo.update} << 32;
		}

		md::LfoSettings unpackLfo(const uint64_t _packed)
		{
			return {static_cast<uint8_t>(_packed), static_cast<uint8_t>(_packed >> 8), static_cast<uint8_t>(_packed >> 16),
				static_cast<uint8_t>(_packed >> 24), static_cast<uint8_t>(_packed >> 32)};
		}
	}

	void Controller::storeLfo(const uint8_t _track, const md::LfoSettings& _lfo, const bool _authoritative)
	{
		if(_track >= m_trackLfos.size())
			return;
		auto& stored = m_trackLfos[_track];
		const auto previous = stored.load(std::memory_order_acquire);
		if((!_authoritative && previous) || previous == packLfo(_lfo))
			return;
		stored.store(packLfo(_lfo), std::memory_order_release);
		m_lfoRevision.fetch_add(1, std::memory_order_acq_rel);
	}

	std::optional<md::LfoSettings> Controller::getTrackLfo(const uint8_t _part) const
	{
		if(_part >= m_trackLfos.size())
			return std::nullopt;
		const auto packed = m_trackLfos[_part].load(std::memory_order_acquire);
		return packed ? std::optional<md::LfoSettings>(unpackLfo(packed)) : std::nullopt;
	}

	bool Controller::setTrackLfo(const uint8_t _part, const uint8_t _field, const uint8_t _value)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(m_model != md::MachineModel::Machinedrum || !firmwareReadyForAutomation())
			return false;
		const auto message = md::automation::sysex::lfoChange(_part, _field, _value);
		if(!message)
			return false;
		sendEditorSysex(*message);
		m_lfoEditMs[_part] = milliseconds();
		// A field of an LFO still unknown is sent, not kept: the others are not known
		if(auto lfo = getTrackLfo(_part))
		{
			uint8_t* fields[] = {&lfo->track, &lfo->parameter, &lfo->shape1, &lfo->shape2, &lfo->update};
			*fields[_field] = _value;
			storeLfo(_part, *lfo, true);
		}
		return true;
	}

	bool Controller::readLibrary()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(!firmwareReadyForAutomation())
			return false;
		{
			const std::lock_guard lock(m_libraryMutex);
			m_library.assign(getKitLibrarySize(), LibraryKit{});
			m_libraryPatterns.assign(PatternLibrarySize, LibraryPattern{});
		}
		m_libraryProgress.store(0, std::memory_order_release);
		m_libraryReading.store(true, std::memory_order_release);
		m_libraryRevision.fetch_add(1, std::memory_order_acq_rel);
		// From the machine's RAM where its layout is known: everything at the Device's next block. No item is
		// waited for meanwhile.
		if(libraryReadable())
		{
			m_libraryWaiting = getKitLibrarySize() + PatternLibrarySize;
			m_libraryRead = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLibraryControl().request();
			return true;
		}
		m_libraryRead = 0;
		requestLibraryItem(0, milliseconds());
		return true;
	}

	bool Controller::libraryReadable() const
	{
		if(m_libraryReadableForTests)
			return *m_libraryReadableForTests;
		const auto status = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLiveDevice().status();
		return status && status->librarySupported;
	}

	void Controller::serviceLibraryRead()
	{
		auto& control = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLibraryControl();
		if(!m_libraryRead || control.getReadId() < m_libraryRead)
			return;
		uint32_t id = 0;
		const auto library = control.read(id);
		m_libraryRead = 0;
		if(library.kitCount != getKitLibrarySize())
		{
			// The machine's layout is not the one expected: ask for every item instead
			requestLibraryItem(0, milliseconds());
			return;
		}
		{
			const std::lock_guard lock(m_libraryMutex);
			for(uint8_t slot = 0; slot < library.kitCount && slot < m_library.size(); ++slot)
			{
				const auto& kit = library.kits[slot];
				m_library[slot] = LibraryKit{true, library.name(slot),
					std::vector<uint16_t>(kit.machines.begin(), kit.machines.begin() + library.tracks)};
			}
			for(uint8_t slot = 0; slot < m_libraryPatterns.size(); ++slot)
			{
				const auto& pattern = library.patterns[slot];
				if(pattern.length)
					m_libraryPatterns[slot] = LibraryPattern{true, pattern.length, pattern.kit, pattern.trigs};
			}
		}
		// Every pattern's length, for the chain
		auto& chain = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getChainControl();
		for(uint8_t slot = 0; slot < library.patterns.size(); ++slot)
		{
			if(library.patterns[slot].length)
				chain.setLength(slot, library.patterns[slot].length);
		}
		m_libraryProgress.store(getKitLibrarySize() + PatternLibrarySize, std::memory_order_release);
		endLibraryReading();
	}

	void Controller::endLibraryReading()
	{
		m_libraryReading.store(false, std::memory_order_release);
		m_libraryDone.store(true, std::memory_order_release);
		m_libraryRevision.fetch_add(1, std::memory_order_acq_rel);
	}

	void Controller::requestLibraryItem(const size_t _item, const uint64_t _now)
	{
		// One request at a time: a Kit dump takes the MIDI line about 0.4 s, a pattern dump up to 1.8 s
		const auto kits = getKitLibrarySize();
		if(_item >= kits + PatternLibrarySize)
		{
			endLibraryReading();
			return;
		}
		m_libraryWaiting = _item;
		m_libraryRequestMs = _now;
		if(_item < kits)
			sendEditorSysex(md::automation::sysex::kitRequest(m_model, static_cast<uint8_t>(_item)));
		else
			sendEditorSysex(md::automation::sysex::patternRequest(m_model, static_cast<uint8_t>(_item - kits)));
	}

	void Controller::storeLibraryPattern(const uint8_t _slot, const LibraryPattern& _pattern)
	{
		// Any dump keeps the library up to date once a reading began: a copy read back, a write
		{
			const std::lock_guard lock(m_libraryMutex);
			if(_slot >= m_libraryPatterns.size())
				return;
			m_libraryPatterns[_slot] = _pattern;
		}
		m_libraryRevision.fetch_add(1, std::memory_order_acq_rel);
		const auto kits = getKitLibrarySize();
		if(!m_libraryReading.load(std::memory_order_acquire) || m_libraryWaiting < kits || _slot != m_libraryWaiting - kits)
			return;
		m_libraryProgress.fetch_add(1, std::memory_order_acq_rel);
		requestLibraryItem(m_libraryWaiting + 1, milliseconds());
	}

	std::optional<Controller::LibraryKit> Controller::getLibraryKit(const uint8_t _slot) const
	{
		const std::lock_guard lock(m_libraryMutex);
		if(_slot >= m_library.size())
			return std::nullopt;
		return m_library[_slot];
	}

	std::optional<Controller::LibraryPattern> Controller::getLibraryPattern(const uint8_t _slot) const
	{
		const std::lock_guard lock(m_libraryMutex);
		if(_slot >= m_libraryPatterns.size())
			return std::nullopt;
		return m_libraryPatterns[_slot];
	}

	std::optional<md::automation::sysex::TrackOutput> Controller::getTrackOutput(const uint8_t _track) const
	{
		if(_track >= m_trackOutputs.size())
			return std::nullopt;
		const auto output = m_trackOutputs[_track].load(std::memory_order_acquire);
		return output <= static_cast<uint8_t>(md::automation::sysex::TrackOutput::Main)
			? std::optional<md::automation::sysex::TrackOutput>(static_cast<md::automation::sysex::TrackOutput>(output))
			: std::nullopt;
	}

	bool Controller::setTrackOutput(const uint8_t _track, const md::automation::sysex::TrackOutput _output)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(m_model != md::MachineModel::Machinedrum || !firmwareReadyForAutomation())
			return false;
		const auto message = md::automation::sysex::trackRouting(_track, _output);
		if(!message)
			return false;
		sendEditorSysex(*message);
		++m_routingWrites;
		if(m_trackOutputs[_track].exchange(static_cast<uint8_t>(_output), std::memory_order_acq_rel) != static_cast<uint8_t>(_output))
			m_routingRevision.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	bool Controller::isValueKnown(const uint8_t _page, const uint8_t _part, const uint8_t _index) const
	{
		const auto* const slot = findAutomationSlot({_page, _part, _index});
		return slot == nullptr || !slot->valueUnknown.load(std::memory_order_acquire);
	}

	void Controller::markValueKnown(AutomationSlot& _slot)
	{
		// Lock-free: host automation calls this from the audio thread.
		if(_slot.valueUnknown.load(std::memory_order_acquire)
			&& _slot.valueUnknown.exchange(false, std::memory_order_acq_rel))
			m_valueStateRevision.fetch_add(1, std::memory_order_acq_rel);
	}

	void Controller::sendEditorSysex(const md::automation::sysex::Message& _message) const
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex = toPluginSysex(_message);
		sendMidiEvent(event);
	}

	bool Controller::requestPattern()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(!firmwareReadyForAutomation())
			return false;
		m_patternWanted.store(true, std::memory_order_release);
		sendEditorSysex(md::automation::sysex::statusRequest(m_model,
			md::automation::sysex::StatusParameter::Pattern));
		return true;
	}

	bool Controller::requestPatternDump(const uint8_t _slot)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(_slot >= 128 || !firmwareReadyForAutomation())
			return false;
		sendEditorSysex(md::automation::sysex::patternRequest(m_model, _slot));
		return true;
	}

	std::optional<md::automation::sysex::PatternDump> Controller::getPattern() const
	{
		const std::lock_guard lock(m_patternMutex);
		return m_pattern;
	}

	std::optional<md::automation::sysex::MmPatternDump> Controller::getMmPattern() const
	{
		const std::lock_guard lock(m_patternMutex);
		return m_mmPattern;
	}

	std::string Controller::getKitName() const
	{
		const std::lock_guard lock(m_kitNameMutex);
		return m_kitName;
	}

	bool Controller::editPattern(const std::function<bool(md::automation::sysex::MdPatternEditor&)>& _edit)
	{
		if(m_model != md::MachineModel::Machinedrum)
			return false;
		{
			const std::lock_guard lock(m_patternMutex);
			if(m_patternDump.empty())
				return false;
			auto editor = md::automation::sysex::MdPatternEditor::fromDump(m_patternDump);
			if(!editor || !_edit(*editor))
				return false;
			auto dump = editor->toDump();
			auto pattern = md::automation::sysex::parseMdPatternDump(dump);
			if(!pattern)
				return false;
			m_patternDump = std::move(dump);
			m_pattern = std::move(*pattern);
			m_patternEdited = true;
		}
		m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	bool Controller::editMmPattern(const std::function<bool(md::automation::sysex::MmPatternEditor&)>& _edit)
	{
		if(m_model != md::MachineModel::Monomachine)
			return false;
		{
			const std::lock_guard lock(m_patternMutex);
			if(m_patternDump.empty() || !m_mmPattern)
				return false;
			auto editor = md::automation::sysex::MmPatternEditor::fromDump(m_patternDump);
			if(!editor || !_edit(*editor))
				return false;
			auto dump = editor->toDump();
			auto pattern = md::automation::sysex::parseMmPatternDump(dump);
			if(!pattern)
				return false;
			m_patternDump = std::move(dump);
			m_mmPattern = std::move(*pattern);
			m_patternEdited = true;
		}
		m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	bool Controller::setMmPatternTrig(const uint8_t _track, const uint8_t _step, const std::optional<uint8_t> _note)
	{
		return editMmPattern([&](md::automation::sysex::MmPatternEditor& _editor)
		{
			return _editor.setTrig(_track, _step, _note);
		});
	}

	bool Controller::setMmPatternLock(const uint8_t _track, const uint8_t _bit, const uint8_t _step,
		const std::optional<uint8_t> _value)
	{
		return editMmPattern([&](md::automation::sysex::MmPatternEditor& _editor)
		{
			return _editor.setLock(_track, _bit, _step, _value);
		});
	}

	bool Controller::setPatternTrig(const uint8_t _track, const uint8_t _step, const bool _on)
	{
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setTrig(_track, _step, _on);
		});
	}

	bool Controller::setPatternLock(const uint8_t _track, const uint8_t _parameter, const uint8_t _step,
		const std::optional<uint8_t> _value)
	{
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setLock(_track, _parameter, _step, _value);
		});
	}

	bool Controller::setPatternFlag(const md::automation::sysex::StepFlag _flag, const std::optional<uint8_t> _track,
		const uint8_t _step, const bool _on)
	{
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setFlag(_flag, _track, _step, _on);
		});
	}

	bool Controller::setPatternFlagPerTrack(const md::automation::sysex::StepFlag _flag, const bool _perTrack)
	{
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setFlagPerTrack(_flag, _perTrack);
		});
	}

	bool Controller::setPatternAccentAmount(const uint8_t _amount)
	{
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setAccentAmount(_amount);
		});
	}

	bool Controller::setMmPatternFlag(const md::automation::sysex::StepFlag _flag, const uint8_t _track,
		const uint8_t _step, const bool _on)
	{
		return editMmPattern([&](md::automation::sysex::MmPatternEditor& _editor)
		{
			return _editor.setFlag(_flag, _track, _step, _on);
		});
	}

	bool Controller::setPatternSwingAmount(const uint8_t _percent)
	{
		if(m_model == md::MachineModel::Monomachine)
		{
			return editMmPattern([&](md::automation::sysex::MmPatternEditor& _editor)
			{
				return _editor.setSwingAmount(_percent);
			});
		}
		return editPattern([&](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setSwingAmount(_percent);
		});
	}

	bool Controller::clearPattern()
	{
		if(m_model == md::MachineModel::Monomachine)
		{
			return editMmPattern([](md::automation::sysex::MmPatternEditor& _editor)
			{
				_editor.clear();
				return true;
			});
		}
		return editPattern([](md::automation::sysex::MdPatternEditor& _editor)
		{
			_editor.clear();
			return true;
		});
	}

	bool Controller::setPatternLength(const uint8_t _length)
	{
		if(m_model == md::MachineModel::Monomachine)
		{
			return editMmPattern([_length](md::automation::sysex::MmPatternEditor& _editor)
			{
				return _editor.setLength(_length);
			});
		}
		return editPattern([_length](md::automation::sysex::MdPatternEditor& _editor)
		{
			return _editor.setLength(_length);
		});
	}

	void Controller::sendPatternSoon()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto now = milliseconds();
		if(!m_patternWriteFirstMs)
			m_patternWriteFirstMs = now;
		m_patternWriteLastMs = now;
	}

	void Controller::servicePatternWrite(const uint64_t _now)
	{
		// A Monomachine write not read back in time (its machine replaced, say): the next one may go
		if(m_model == md::MachineModel::Monomachine && _now - m_mmWriteMs > MmWriteTimeoutMilliseconds)
		{
			const std::lock_guard lock(m_patternMutex);
			if(m_patternWritesInFlight > 0)
			{
				m_patternWritesInFlight = 0;
				m_patternWrite.store(PatternWrite::None, std::memory_order_release);
			}
		}
		if(m_livePatternWrite)
			serviceLivePatternWrite();
		if(!m_patternWriteFirstMs)
		{
			if(m_model != md::MachineModel::Monomachine)
				verifyPatternWrite(_now);
			return;
		}
		// Written in the RAM, an edit costs the machine nothing: at once, as a grid edit
		if(!livePatternWritable() && _now - m_patternWriteLastMs < PatternWritePauseMilliseconds
			&& _now - m_patternWriteFirstMs < PatternWriteMaxDelayMilliseconds)
			return;
		(void)sendPattern();
	}

	bool Controller::livePatternWritable() const
	{
		if(m_model != md::MachineModel::Machinedrum)
			return false;
		const auto status = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLiveDevice().status();
		return status && status->livePatternSupported;
	}

	void Controller::serviceLivePatternWrite()
	{
		const auto& control = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLivePatternControl();
		if(control.getDone() < m_livePatternWrite)
			return;
		const bool refused = control.getRefused() >= m_livePatternWrite;
		m_livePatternWrite = 0;
		if(!refused)
		{
			// Edits made since wait for their own write
			if(!m_patternWriteFirstMs)
				m_patternWrite.store(PatternWrite::Written, std::memory_order_release);
			return;
		}
		// The machine's pattern changed under the edit (edited on the machine, another pattern selected): read
		// again, and the next edit goes as a dump
		{
			const std::lock_guard lock(m_patternMutex);
			m_patternMachineDump.clear();
			m_patternMachineSlot = NoPattern;
		}
		m_patternWrite.store(PatternWrite::Refused, std::memory_order_release);
		(void)requestPattern();
	}

	void Controller::verifyPatternWrite(const uint64_t _now)
	{
		uint8_t slot = 0;
		{
			const std::lock_guard lock(m_patternMutex);
			if(!m_patternVerifyDueMs || _now < m_patternVerifyDueMs || !m_patternSent)
				return;
			m_patternVerifyDueMs = 0;
			slot = m_patternSent->slot;
			++m_patternWritesInFlight;
		}
		m_patternRequestedSlot.store(slot, std::memory_order_release);
		m_patternWanted.store(true, std::memory_order_release);
		sendEditorSysex(md::automation::sysex::patternRequest(m_model, slot));
	}

	void Controller::writeMmDump(const md::automation::sysex::Message& _dump, const uint8_t _slot)
	{
		// The Monomachine reloads the Kit of the selected pattern when it takes it, as the Machinedrum does:
		// the live Kit is saved first (mmPatternWriteFirmwareTest)
		const auto kit = m_currentKit.load(std::memory_order_acquire);
		if(_slot == m_currentPattern.load(std::memory_order_acquire) && kit < getKitLibrarySize())
			sendEditorSysex(md::automation::sysex::kitSave(m_model, kit));
		static_cast<AudioPluginAudioProcessor&>(getProcessor()).getMmPatternWriteControl().request({_dump},
			{md::automation::sysex::patternRequest(m_model, _slot)});
		m_mmWriteMs = milliseconds();
	}

	bool Controller::sendPattern()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(!firmwareReadyForAutomation())
			return false;
		if(m_model == md::MachineModel::Monomachine)
		{
			// A write at a time: the edits wait for the one under way, the controller timer sends them after it
			md::automation::sysex::Message dump;
			uint8_t slot = 0;
			{
				const std::lock_guard lock(m_patternMutex);
				// Nothing left to write (the edits gave way to another pattern's dump, say): no write waits
				if(!m_patternEdited || !m_mmPattern)
				{
					m_patternWriteFirstMs = 0;
					return false;
				}
				if(m_patternWritesInFlight > 0)
				{
					if(!m_patternWriteFirstMs)
						m_patternWriteFirstMs = m_patternWriteLastMs = milliseconds();
					return false;
				}
				linkCurrentKit();
				dump = m_patternDump;
				slot = m_mmPattern->slot;
				m_patternEdited = false;
				m_mmPatternSent = m_mmPattern;
				++m_patternWritesInFlight;
			}
			m_patternWriteFirstMs = 0;
			m_patternWrite.store(PatternWrite::Pending, std::memory_order_release);
			m_patternRequestedSlot.store(slot, std::memory_order_release);
			m_patternWanted.store(true, std::memory_order_release);
			writeMmDump(dump, slot);
			return true;
		}
		// This write carries the edits a sendPatternSoon() waits with
		m_patternWriteFirstMs = 0;
		md::automation::sysex::Message dump;
		md::automation::sysex::Message from;
		uint8_t slot = 0;
		// A copy onto the pattern shown ends with its read-back: asked for at once. An edit's waits for the
		// writes to pause (verifyPatternWrite).
		const bool readBack = m_patternCopyShown && getPatternCopy().state == PatternCopy::Writing;
		const bool writable = !readBack && livePatternWritable();
		{
			const std::lock_guard lock(m_patternMutex);
			if(!m_patternEdited || !m_pattern)
				return false;
			linkCurrentKit();
			dump = m_patternDump;
			slot = m_pattern->slot;
			m_patternEdited = false;
			m_patternSent = m_pattern;
			// The pattern playing, its Kit the live one, edited from what its RAM holds: the bytes the edits
			// changed go there, as a grid edit (md::LivePatternControl). Nothing reloads, nothing is read back.
			if(writable && slot == m_currentPattern.load(std::memory_order_acquire) && m_patternMachineSlot == slot
				&& m_pattern->kit == m_currentKit.load(std::memory_order_acquire))
			{
				from = std::move(m_patternMachineDump);
				m_patternVerifyDueMs = 0;
			}
			else
			{
				if(readBack)
					++m_patternWritesInFlight;
				m_patternVerifyDueMs = readBack ? 0 : milliseconds() + PatternVerifyMilliseconds;
			}
			// What the machine holds once it takes the write
			m_patternMachineDump = dump;
			m_patternMachineSlot = slot;
		}
		m_patternWrite.store(PatternWrite::Pending, std::memory_order_release);
		if(!from.empty())
		{
			m_livePatternWrite = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLivePatternControl()
				.request(std::move(from), std::move(dump));
			return true;
		}
		if(readBack)
		{
			m_patternRequestedSlot.store(slot, std::memory_order_release);
			m_patternWanted.store(true, std::memory_order_release);
		}
		// The Machinedrum reloads the pattern's Kit as stored when it takes the pattern, stopped or playing:
		// values changed since the Kit was saved (SON) would go back. Saving the live Kit first keeps them
		// (mdEditorFirmwareTest, checkLiveKitAcrossPatternWrite).
		const auto kit = m_currentKit.load(std::memory_order_acquire);
		if(kit < 64)
			sendEditorSysex(md::automation::sysex::kitSave(m_model, kit));
		sendEditorSysex(dump);
		if(readBack)
			sendEditorSysex(md::automation::sysex::patternRequest(m_model, slot));
		return true;
	}

	bool Controller::copyPattern(const uint8_t _from, const uint8_t _to)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto copy = getPatternCopy();
		if(_from >= PatternLibrarySize || _to >= PatternLibrarySize || _from == _to
			|| copy.state == PatternCopy::Reading || copy.state == PatternCopy::Writing || !firmwareReadyForAutomation())
			return false;
		m_patternCopy.store(uint64_t{static_cast<uint8_t>(PatternCopy::Reading)} | uint64_t{_from} << 8 | uint64_t{_to} << 16
			| uint64_t{copy.serial + 1} << 24, std::memory_order_release);
		m_patternCopyMs = milliseconds();
		// The pattern shown is its own source, its edits not written yet included
		md::automation::sysex::Message shown;
		{
			const std::lock_guard lock(m_patternMutex);
			if((m_pattern && m_pattern->slot == _from) || (m_mmPattern && m_mmPattern->slot == _from))
				shown = m_patternDump;
		}
		if(shown.empty())
			sendEditorSysex(md::automation::sysex::patternRequest(m_model, _from));
		else
			writePatternCopy(shown);
		return true;
	}

	Controller::PatternCopyState Controller::getPatternCopy() const
	{
		const auto packed = m_patternCopy.load(std::memory_order_acquire);
		return {static_cast<PatternCopy>(packed & 0xff), static_cast<uint8_t>(packed >> 8), static_cast<uint8_t>(packed >> 16),
			static_cast<uint32_t>(packed >> 24)};
	}

	void Controller::setPatternCopyState(const PatternCopy _state)
	{
		const auto packed = m_patternCopy.load(std::memory_order_acquire);
		m_patternCopy.store((packed & ~uint64_t{0xff}) | static_cast<uint8_t>(_state), std::memory_order_release);
	}

	void Controller::writePatternCopy(const md::automation::sysex::Message& _source)
	{
		const auto to = getPatternCopy().to;
		// Onto the current pattern, the copy plays the live Kit, as an edit of it does (linkCurrentKit): with the
		// source's, the machine would load that Kit, unknown to the controller
		const auto kit = m_currentKit.load(std::memory_order_acquire);
		const bool keepKit = to == m_currentPattern.load(std::memory_order_acquire) && kit < getKitLibrarySize();
		if(m_model == md::MachineModel::Monomachine)
		{
			auto editor = md::automation::sysex::MmPatternEditor::fromDump(_source);
			md::automation::sysex::Message dump;
			std::optional<md::automation::sysex::MmPatternDump> copy;
			if(editor && editor->setSlot(to) && (!keepKit || editor->setKit(kit)))
			{
				dump = editor->toDump();
				copy = md::automation::sysex::parseMmPatternDump(dump);
			}
			if(!copy)
			{
				setPatternCopyState(PatternCopy::Failed);
				return;
			}
			setPatternCopyState(PatternCopy::Writing);
			m_patternCopyMs = milliseconds();
			m_patternCopyShown = false;
			{
				const std::lock_guard lock(m_patternMutex);
				m_mmPatternCopySent = std::move(copy);
				// Onto the pattern shown: the copy replaces it, its edits not written yet dropped; the copy
				// read back shows
				if(m_mmPattern && m_mmPattern->slot == to)
				{
					m_patternEdited = false;
					m_patternWriteFirstMs = 0;
				}
			}
			writeMmDump(dump, to);
			return;
		}
		auto editor = md::automation::sysex::MdPatternEditor::fromDump(_source);
		md::automation::sysex::Message dump;
		std::optional<md::automation::sysex::PatternDump> copy;
		if(editor && editor->setSlot(to) && (!keepKit || editor->setKit(kit)))
		{
			dump = editor->toDump();
			copy = md::automation::sysex::parseMdPatternDump(dump);
		}
		if(!copy)
		{
			setPatternCopyState(PatternCopy::Failed);
			return;
		}
		setPatternCopyState(PatternCopy::Writing);
		m_patternCopyMs = milliseconds();
		// Onto the pattern shown: it replaces it as an edit does, and is written as one
		{
			const std::lock_guard lock(m_patternMutex);
			m_patternCopyShown = m_pattern && m_pattern->slot == to;
			if(m_patternCopyShown)
			{
				m_patternDump = dump;
				m_pattern = *copy;
				m_patternEdited = true;
			}
		}
		if(m_patternCopyShown)
		{
			m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
			if(!sendPattern())
				setPatternCopyState(PatternCopy::Failed);
			return;
		}
		m_patternCopySent = std::move(copy);
		// The selected pattern reloads its Kit as stored when it is written (see sendPattern)
		if(keepKit)
			sendEditorSysex(md::automation::sysex::kitSave(m_model, kit));
		sendEditorSysex(dump);
		sendEditorSysex(md::automation::sysex::patternRequest(m_model, to));
	}

	void Controller::servicePatternCopy(const uint64_t _now)
	{
		const auto state = getPatternCopy().state;
		const auto timeout = m_model == md::MachineModel::Monomachine ? MmWriteTimeoutMilliseconds : PatternCopyTimeoutMilliseconds;
		if((state == PatternCopy::Reading || state == PatternCopy::Writing) && _now - m_patternCopyMs > timeout)
			setPatternCopyState(PatternCopy::Failed);
	}

	void Controller::linkCurrentKit()
	{
		const auto kit = m_currentKit.load(std::memory_order_acquire);
		const auto current = m_currentPattern.load(std::memory_order_acquire);
		if(kit >= getKitLibrarySize())
			return;
		if(m_mmPattern && m_mmPattern->slot == current && m_mmPattern->kit != kit)
		{
			auto editor = md::automation::sysex::MmPatternEditor::fromDump(m_patternDump);
			if(!editor || !editor->setKit(kit))
				return;
			auto dump = editor->toDump();
			if(auto pattern = md::automation::sysex::parseMmPatternDump(dump))
			{
				m_patternDump = std::move(dump);
				m_mmPattern = std::move(*pattern);
			}
		}
		else if(m_pattern && m_pattern->slot == current && m_pattern->kit != kit)
		{
			auto editor = md::automation::sysex::MdPatternEditor::fromDump(m_patternDump);
			if(!editor || !editor->setKit(kit))
				return;
			auto dump = editor->toDump();
			if(auto pattern = md::automation::sysex::parseMdPatternDump(dump))
			{
				m_patternDump = std::move(dump);
				m_pattern = std::move(*pattern);
			}
		}
	}

	bool Controller::isPatternBusy()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto copy = getPatternCopy().state;
		if(copy == PatternCopy::Reading || copy == PatternCopy::Writing)
			return true;
		if(m_model != md::MachineModel::Monomachine)
			return false;
		const std::lock_guard lock(m_patternMutex);
		return m_patternWriteFirstMs != 0 || m_patternWritesInFlight > 0;
	}

	bool Controller::loadKit(const uint8_t _slot)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(_slot >= getKitLibrarySize() || !firmwareReadyForAutomation() || isPatternBusy()
			|| m_patternLoad.load(std::memory_order_acquire) < PatternLibrarySize)
			return false;
		// Edits waiting go first, saved with the Kit they were made with (sendPattern)
		if(m_patternWriteFirstMs)
			(void)sendPattern();
		sendEditorSysex(md::automation::sysex::kitLoad(m_model, _slot));
		// The machine plays the Kit from here on, a SAVE KIT sent after goes to its slot; its name comes with its
		// dump, which requestKitState applies
		if(m_currentKit.exchange(_slot, std::memory_order_acq_rel) != _slot)
		{
			{
				const std::lock_guard lock(m_kitNameMutex);
				m_kitName.clear();
			}
			m_selectionRevision.fetch_add(1, std::memory_order_acq_rel);
		}
		requestKitState();
		// The current pattern plays it too: read again, the pattern shown says so
		if(m_patternRevision.load(std::memory_order_acquire) > 0)
			requestPattern();
		return true;
	}

	bool Controller::loadPattern(const uint8_t _slot)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(_slot >= PatternLibrarySize || !firmwareReadyForAutomation() || isPatternBusy())
			return false;
		if(m_patternWriteFirstMs)
			(void)sendPattern();
		sendEditorSysex(md::automation::sysex::patternSelect(m_model, _slot));
		const auto now = milliseconds();
		m_patternLoad.store(_slot, std::memory_order_release);
		m_patternLoadMs = m_patternLoadPollMs = now;
		m_patternLoadSeenMs = 0;
		// Stopped, the machine takes it at once: the status answers with it, and the pattern shown follows (the
		// status handler); playing, servicePatternLoad asks until it does
		if(!getPlayingStep())
			requestPattern();
		return true;
	}

	void Controller::servicePatternLoad(const uint64_t _now)
	{
		const auto slot = m_patternLoad.load(std::memory_order_acquire);
		if(slot >= PatternLibrarySize)
			return;
		// The pattern is the current one: its Kit is the live one once the pattern plays, which the status
		// answers before (PatternLoadSettleMilliseconds); stopped, at once
		if(m_currentPattern.load(std::memory_order_acquire) == slot)
		{
			if(!m_patternLoadSeenMs)
				m_patternLoadSeenMs = _now;
			if(getPlayingStep() && _now - m_patternLoadSeenMs < PatternLoadSettleMilliseconds)
				return;
			m_patternLoad.store(0xff, std::memory_order_release);
			requestKitState();
			return;
		}
		if(_now - m_patternLoadMs > PatternLoadTimeoutMilliseconds)
		{
			m_patternLoad.store(0xff, std::memory_order_release);
			return;
		}
		// Playing: the machine takes it at the end of the pattern playing
		if(_now - m_patternLoadPollMs >= PatternLoadPollMilliseconds)
		{
			m_patternLoadPollMs = _now;
			sendEditorSysex(md::automation::sysex::statusRequest(m_model, md::automation::sysex::StatusParameter::Pattern));
		}
	}

	bool Controller::saveKit()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto kit = m_currentKit.load(std::memory_order_acquire);
		if(kit >= getKitLibrarySize() || !firmwareReadyForAutomation() || isPatternBusy()
			|| m_patternLoad.load(std::memory_order_acquire) < PatternLibrarySize)
			return false;
		sendEditorSysex(md::automation::sysex::kitSave(m_model, kit));
		// The stored Kit is the live one now: the library shows it so
		{
			std::vector<uint16_t> machines;
			for(uint8_t track = 0; track < getPartCount(); ++track)
				machines.push_back(getTrackMachine(track));
			const auto name = getKitName();
			const std::lock_guard lock(m_libraryMutex);
			if(kit < m_library.size())
			{
				m_library[kit] = LibraryKit{true, name.empty() && m_library[kit].read ? m_library[kit].name : name, machines};
				m_libraryRevision.fetch_add(1, std::memory_order_acq_rel);
			}
		}
		return true;
	}

	void Controller::requestKitState()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		m_automationReady.store(false, std::memory_order_release);
		m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
		m_haveKit.store(false, std::memory_order_release);
		// Program/status changes explicitly reload the Kit and therefore make the
		// stored slot authoritative even when its number happens to be unchanged.
		m_forceApplyRequestedKitDump.store(true, std::memory_order_release);
		m_kitSynchronization.reset();
		m_kitDumpRequestRevision.store(0, std::memory_order_release);
		sendMissingSynchronizationRequests();
	}

	uint64_t Controller::milliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void Controller::sendMissingSynchronizationRequests()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		// Do not accumulate status requests in the plug-in MIDI queue while the
		// firmware DSPs are still booting. Once consumed, those duplicates can yield
		// late Kit dumps that overwrite host writes after synchronization completed.
		if(!firmwareReadyForAutomation())
			return;
		const auto now = milliseconds();
		const auto recoverTimedOut = [this, now](
			md::automation::DumpRequestTracker& _tracker,
			std::atomic<bool>& _haveSnapshot)
		{
			if(!_tracker.recoverTimedOutDump(now, g_dumpRequestRetryMs))
				return;
			_haveSnapshot.store(false, std::memory_order_release);
			m_automationReady.store(false, std::memory_order_release);
			m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
		};
		recoverTimedOut(m_globalSynchronization, m_haveGlobal);
		recoverTimedOut(m_kitSynchronization, m_haveKit);
		if(m_globalSynchronization.statusRequestDue(now, 500))
		{
			m_globalSynchronization.statusRequestSent(now);
			sendSynchronizationRequest(toPluginSysex(md::automation::sysex::statusRequest(m_model,
				md::automation::sysex::StatusParameter::Global)));
		}
		if(m_kitSynchronization.statusRequestDue(now, 500))
		{
			m_kitSynchronization.statusRequestSent(now);
			sendSynchronizationRequest(toPluginSysex(md::automation::sysex::statusRequest(m_model,
				md::automation::sysex::StatusParameter::Kit)));
			// The pattern number with it, so the screen does not wait for the first poll
			if(m_currentPattern.load(std::memory_order_acquire) == 0xff)
				sendEditorSysex(md::automation::sysex::statusRequest(m_model,
					md::automation::sysex::StatusParameter::Pattern));
		}
	}

	void Controller::sendSynchronizationRequest(const pluginLib::SysEx& _message) const
	{
		// Keep controller-generated state queries observable in firmware-test
		// diagnostics without changing their normal editor-to-device routing.
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex = _message;
		m_synchronizationRequests.fetch_add(1, std::memory_order_relaxed);
		sendMidiEvent(event);
	}

	void Controller::onControllerTimer()
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		// The same protocol service also runs from an offline render callback, which
		// is non-realtime but is not necessarily JUCE's message thread. Keep editor
		// Value/listener work on the message thread while still allowing headless
		// renders to drain MIDI and advance synchronization below.
		const auto* const messageManager =
			juce::MessageManager::getInstanceWithoutCreating();
		if(messageManager != nullptr && messageManager->isThisTheMessageThread())
		{
			for(const auto& [address, parameters] : getExposedParameters())
			{
				(void)address;
				for(auto* const parameter : parameters)
					parameter->flushRealtimeValueToUi();
			}
		}
		drainRealtimeParameterChanges(RealtimeAutomationCapacity, false);
		completeSynchronizationIfReady();
		const auto now = milliseconds();
		servicePatternWrite(now);
		servicePatternCopy(now);
		servicePatternLoad(now);
		serviceLibraryRead();
		// A library Kit not answered: skip it (the firmware may drop a request while busy)
		// A Kit not answered within 2 s, a pattern (a longer dump) within 4 s, is skipped
		if(m_libraryReading.load(std::memory_order_acquire) && !m_libraryRead
			&& now - m_libraryRequestMs > (m_libraryWaiting < getKitLibrarySize() ? g_dumpRequestRetryMs : 2 * g_dumpRequestRetryMs))
		{
			m_libraryProgress.fetch_add(1, std::memory_order_acq_rel);
			requestLibraryItem(m_libraryWaiting + 1, now);
		}
		// The live Kit only once synchronized; a read from before a resynchronization is forgotten
		if(m_automationReady.load(std::memory_order_acquire))
			serviceLiveKit(now);
		else
			m_previousLiveKit.reset();
		if(m_automationReady.load(std::memory_order_acquire))
		{
			// Firmware Global is authoritative for the MIDI channel, and a front-panel
			// kit selection bypasses the controller. Poll their tiny status messages;
			// only a changed kit number causes the much larger Kit dump.
			if(now - m_lastStatePollMs.load(std::memory_order_acquire) < 5000)
				return;
			m_lastStatePollMs.store(now, std::memory_order_release);
			// A controller-facing copy of firmware MIDI is deliberately allowed to
			// drop rather than block the audio thread. Retire a lost periodic status
			// response so one contention event cannot disable polling forever.
			m_globalSynchronization.recoverTimedOutStatus(
				now, g_dumpRequestRetryMs);
			m_kitSynchronization.recoverTimedOutStatus(
				now, g_dumpRequestRetryMs);
			// A status response identifies the active Global slot, but does not expose
			// edits to that Global's MIDI base channel. Refresh the selected Global too
			// so queued writes survive MIDI NONE and resume when a channel is enabled.
			if(m_globalSynchronization.canPollStatus())
			{
				m_globalSynchronization.statusRequestSent(now);
				sendSynchronizationRequest(toPluginSysex(
					md::automation::sysex::statusRequest(m_model,
						md::automation::sysex::StatusParameter::Global)));
			}
			if(m_kitSynchronization.canPollStatus())
			{
				m_kitSynchronization.statusRequestSent(now);
				sendSynchronizationRequest(toPluginSysex(
					md::automation::sysex::statusRequest(m_model,
						md::automation::sysex::StatusParameter::Kit)));
			}
			// The pattern number for the editor's screen; once a pattern is shown,
			// another one selected on the front panel is read again.
			sendEditorSysex(md::automation::sysex::statusRequest(m_model,
				md::automation::sysex::StatusParameter::Pattern));
			return;
		}
		sendMissingSynchronizationRequests();
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter,
		const pluginLib::ParamValue _value, const pluginLib::Parameter::Origin _origin)
	{
		const auto& description = _parameter.getDescription();
		const md::automation::ParameterChange change{
			description.page,
			_parameter.getPart(),
			description.index,
			static_cast<uint8_t>(std::clamp<pluginLib::ParamValue>(_value, 0, 127))
		};
		publishAutomationIntent(change,
			_origin != pluginLib::Parameter::Origin::HostAutomation
				|| !m_automationReady.load(std::memory_order_acquire));
		// UI changes use exactly the same ordered publication path as host
		// automation. A non-realtime caller may drain immediately, while a host
		// callback only performs the bounded publication and returns.
		if(_origin != pluginLib::Parameter::Origin::HostAutomation)
			drainRealtimeParameterChanges(RealtimeAutomationCapacity, false);
	}

	uint8_t Controller::publicationValue(const uint64_t _publication)
	{
		return static_cast<uint8_t>(_publication & PublicationValueMask);
	}

	uint64_t Controller::publicationRevision(const uint64_t _publication)
	{
		return (_publication & PublicationRevisionMask) >> 8;
	}

	bool Controller::publicationIsDirty(const uint64_t _publication)
	{
		return (_publication & PublicationDirty) != 0;
	}

	uint64_t Controller::createPublication(const uint8_t _value, const bool _dirty)
	{
		const auto revision = m_nextAutomationRevision.fetch_add(
			1, std::memory_order_relaxed);
		return (_dirty ? PublicationDirty : 0)
			| ((revision << 8) & PublicationRevisionMask)
			| (_value & PublicationValueMask);
	}

	void Controller::publishAutomationIntent(
		const md::automation::ParameterChange& _change,
		const bool _supersedeEarlier)
	{
		const auto found = m_automationSlotIndices.find(
			{_change.page, _change.track, _change.index});
		if(found == m_automationSlotIndices.end())
			return;

		const auto publication = createPublication(_change.value, true);
		auto& slot = m_automationSlots[found->second];
		slot.publication.exchange(publication, std::memory_order_acq_rel);
		markValueKnown(slot);
		const auto advanceDeliveryFloor = [&slot](const uint64_t _revision)
		{
			// Keep the realtime producer strictly bounded: a fixed number of strong
			// attempts can only all fail under sustained same-address contention.
			// Failure to advance merely permits an extra stale value before the latest;
			// it cannot lose or overwrite the authoritative publication.
			auto floor = slot.deliveryFloorRevision.load(std::memory_order_acquire);
			for(size_t attempt = 0; attempt < 8 && floor < _revision; ++attempt)
			{
				if(slot.deliveryFloorRevision.compare_exchange_strong(floor, _revision,
					std::memory_order_release, std::memory_order_acquire))
					return;
			}
		};
		if(_supersedeEarlier)
			advanceDeliveryFloor(publicationRevision(publication));
		if(!m_realtimeAutomationChanges.tryPush(
			{_change, found->second, publication}))
		{
			// The atomic slot remains authoritative. A bounded slot scan will deliver
			// it even when queue capacity or producer contention drops this hint. Once
			// a hint is missing, older queued values can no longer form a complete FIFO
			// stream, so explicitly coalesce them behind the recovered latest value.
			advanceDeliveryFloor(publicationRevision(publication));
			slot.scanPublication.store(publication, std::memory_order_release);
			m_realtimeAutomationOverflows.fetch_add(1, std::memory_order_relaxed);
		}
	}

	Controller::AutomationSlot* Controller::findAutomationSlot(
		const Address& _address)
	{
		const auto found = m_automationSlotIndices.find(_address);
		return found == m_automationSlotIndices.end()
			? nullptr : &m_automationSlots[found->second];
	}

	const Controller::AutomationSlot* Controller::findAutomationSlot(
		const Address& _address) const
	{
		const auto found = m_automationSlotIndices.find(_address);
		return found == m_automationSlotIndices.end()
			? nullptr : &m_automationSlots[found->second];
	}

	int Controller::getLastFirmwareKitValue(
		const pluginLib::Parameter& _parameter) const
	{
		const auto& description = _parameter.getDescription();
		const auto* const slot = findAutomationSlot({description.page,
			_parameter.getPart(), description.index});
		if(slot == nullptr)
			return -1;
		const auto value = slot->lastFirmwareKitValue.load(std::memory_order_acquire);
		return value <= 127 ? static_cast<int>(value) : -1;
	}

	uint8_t Controller::publishFirmwareValue(const Address& _address,
		const uint8_t _value, const uint64_t _kitRequestRevision)
	{
		auto* const slot = findAutomationSlot(_address);
		if(slot == nullptr)
			return _value;
		// Either the firmware's value or a newer intent the firmware gets next
		markValueKnown(*slot);

		const auto desired = createPublication(_value, false);
		auto observed = slot->publication.load(std::memory_order_acquire);
		for(;;)
		{
			// An undelivered DAW/UI intent always survives a dump. For Kit dumps,
			// direct changes observed after the request watermark survive as well.
			// The watermark is valid because both the read request and later editor/host
			// MIDI enter synthLib::Plugin's FIFO in publication order; processBlock
			// appends its bounded realtime insertions after general ingress already
			// queued for that block. Firmware therefore cannot observe the later change
			// before the earlier request.
			if(publicationIsDirty(observed)
				|| (_kitRequestRevision != 0
					&& publicationRevision(observed) > _kitRequestRevision))
				return publicationValue(observed);
			if(slot->publication.compare_exchange_weak(observed, desired,
				std::memory_order_release, std::memory_order_acquire))
				return _value;
		}
	}

	void Controller::transmitParameterChange(
		const md::automation::ParameterChange& _change)
	{
		if(const auto message = md::automation::encodeParameterChange(
			m_model, _change, getAutomationBaseChannel()))
		{
			auto digest = m_transmittedAutomationDigest.load(std::memory_order_relaxed);
			for(const auto byte : *message)
			{
				digest ^= byte;
				digest *= 1099511628211ull;
			}
			m_transmittedAutomationDigest.store(digest, std::memory_order_release);
			m_transmittedAutomationChanges.fetch_add(1, std::memory_order_release);
			if(_change.track < m_trackDeliveries.size())
				m_trackDeliveries[_change.track].fetch_add(1, std::memory_order_relaxed);
			sendMidiEvent((*message)[0], (*message)[1], (*message)[2]);
		}
	}

	bool Controller::transmitRealtimeParameterChange(
		const md::automation::ParameterChange& _change)
	{
		const auto message = md::automation::encodeParameterChange(
			m_model, _change, getAutomationBaseChannel());
		if(!message)
			return true;
		const synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor,
			(*message)[0], (*message)[1], (*message)[2]);
		if(!getProcessor().tryAddRealtimeMidiEvent(event))
			return false;

		auto digest = m_transmittedAutomationDigest.load(std::memory_order_relaxed);
		for(const auto byte : *message)
		{
			digest ^= byte;
			digest *= 1099511628211ull;
		}
		m_transmittedAutomationDigest.store(digest, std::memory_order_release);
		m_transmittedAutomationChanges.fetch_add(1, std::memory_order_release);
		if(_change.track < m_trackDeliveries.size())
			m_trackDeliveries[_change.track].fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool Controller::deliverAutomationPublication(
		const QueuedAutomationChange& _queued, const bool _realtime)
	{
		if(_queued.slotIndex >= m_automationSlots.size())
			return true;
		auto& slot = m_automationSlots[_queued.slotIndex];
		auto observed = slot.publication.load(std::memory_order_acquire);
		const auto queuedRevision = publicationRevision(_queued.publication);
		if(queuedRevision < slot.deliveryFloorRevision.load(std::memory_order_acquire))
			return true;
		if(observed != _queued.publication)
		{
			// A later ordinary DAW publication does not invalidate this FIFO entry.
			// Transmit the older value now and leave the latest dirty publication for
			// its own hint. If the latest was already delivered, its clean state proves
			// this reordered/stale hint must instead be ignored.
			if(!publicationIsDirty(observed)
				|| publicationRevision(observed) <= queuedRevision)
				return true;
		}
		else if(!publicationIsDirty(observed))
			return true;
		if(!m_automationReady.load(std::memory_order_acquire)
			|| getAutomationBaseChannel() == 0x7f)
			return true;

		if(_realtime)
		{
			if(!transmitRealtimeParameterChange(_queued.change))
				return false;
		}
		else
		{
			transmitParameterChange(_queued.change);
		}

		if(observed == _queued.publication)
		{
			// Clear the delivery bit only if no newer publication replaced this one
			// while MIDI was being queued. A failed CAS leaves that newer value dirty.
			const auto delivered = observed & ~PublicationDirty;
			slot.publication.compare_exchange_strong(observed, delivered,
				std::memory_order_release, std::memory_order_acquire);
		}
		return true;
	}

	void Controller::drainRealtimeParameterChanges(const size_t _maximumChanges,
		const bool _realtime)
	{
		if(_maximumChanges == 0
			|| m_realtimeAutomationDrain.test_and_set(std::memory_order_acquire))
			return;
		struct ClearFlag
		{
			std::atomic_flag& flag;
			~ClearFlag() { flag.clear(std::memory_order_release); }
		} clear{m_realtimeAutomationDrain};

		const auto routable = m_automationReady.load(std::memory_order_acquire)
			&& getAutomationBaseChannel() != 0x7f && !m_automationSlots.empty();
		// A successful hint is the only way to preserve an exact DAW sequence. Do not
		// consume it while firmware routing is unavailable; synchronization completion
		// (or a later MIDI-channel enable) will drain the intact FIFO. Failed hints have
		// their separate recovery marker and remain safe if this queue fills meanwhile.
		if(!routable)
			return;
		// Always reserve part of a routable callback for recovery-marked slots.
		// A full queue can contain thousands of stale hints for one address; without
		// this reservation, an unhinted latest value could wait for all of them.
		size_t reservedScan = routable ? std::min(m_automationSlots.size(),
			std::max<size_t>(1, _maximumChanges / 4)) : size_t{0};
		if(routable && _maximumChanges == 1)
		{
			// A one-event caller cannot serve the FIFO and recovery scan in one pass.
			// Alternate them so neither source can starve; larger budgets serve both.
			reservedScan = m_minimumBudgetRecoveryTurn ? 1 : 0;
			m_minimumBudgetRecoveryTurn = !m_minimumBudgetRecoveryTurn;
		}
		const auto queueLimit = _maximumChanges - reservedScan;
		size_t processed = 0;
		bool queueEmpty = false;
		while(processed < queueLimit)
		{
			QueuedAutomationChange queued;
			if(!m_realtimeAutomationChanges.tryPop(queued))
			{
				queueEmpty = true;
				break;
			}
			if(!deliverAutomationPublication(queued, _realtime))
				return;
			++processed;
		}

		// Queue overflow and producer contention only drop hints. Scan a bounded
		// rotating slice for explicit recovery markers, even while hints remain. Do
		// not deliver ordinary dirty slots from the scan: jumping their healthy FIFO
		// hints would incorrectly coalesce explicit host writes. If the queue
		// empties early, spend the unused budget on the scan. Thus a dirty slot is
		// reconsidered within ceil(slot-count / reserved-scan) callbacks regardless
		// of stale queue depth, while total callback work never exceeds the caller's
		// explicit maximum.
		const auto scanLimit = std::min(m_automationSlots.size(),
			queueEmpty ? _maximumChanges - processed : reservedScan);
		size_t inspected = 0;
		while(inspected < scanLimit)
		{
			if(m_dirtyScanPosition >= m_automationSlots.size())
				m_dirtyScanPosition = 0;
			const auto slotIndex = m_dirtyScanPosition++;
			auto& slot = m_automationSlots[slotIndex];
			auto recovery = slot.scanPublication.load(std::memory_order_acquire);
			if(recovery != 0)
			{
				const auto publication = slot.publication.load(std::memory_order_acquire);
				if(publicationIsDirty(publication))
				{
					const auto& address = slot.address;
					if(!deliverAutomationPublication({
						{address.page, address.track, address.index,
							publicationValue(publication)}, slotIndex, publication}, _realtime))
						return;
				}
				// Retain the marker until the FIFO has actually been observed empty, so a
				// later same-slot host write cannot disappear behind the known stale backlog.
				// CAS still prevents an older scan from clearing a replacement marker.
				if(queueEmpty && !publicationIsDirty(
					slot.publication.load(std::memory_order_acquire)))
					slot.scanPublication.compare_exchange_strong(recovery, 0,
						std::memory_order_release, std::memory_order_acquire);
			}
			++inspected;
		}
	}

	void Controller::processRealtimeParameterChanges(const size_t _maximumChanges)
	{
		drainRealtimeParameterChanges(_maximumChanges, true);
	}

	void Controller::applyKitParameters(
		const std::vector<md::automation::ParameterChange>& _changes)
	{
		const auto requestRevision = m_kitDumpRequestRevision.load(
			std::memory_order_acquire);
		for(const auto& change : _changes)
		{
			if(auto* const slot = findAutomationSlot(
				{change.page, change.track, change.index}))
				slot->lastFirmwareKitValue.store(change.value,
					std::memory_order_release);
			const auto value = publishFirmwareValue(
				{change.page, change.track, change.index}, change.value,
				requestRevision);
			const auto& parameters = findSynthParam(change.track, change.page,
				change.index);
			for(auto* const parameter : parameters)
				parameter->setValueFromSynth(value,
					pluginLib::Parameter::Origin::PresetChange);
		}
		getProcessor().updateHostDisplay(
			juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
	}

	void Controller::completeSynchronizationIfReady()
	{
		// Requests are withheld while the DSPs boot or project restore is pending.
		// Once both strictly correlated replies arrive, those replies themselves are
		// the readiness proof; consulting asynchronous hardware state again here can
		// only delay publication of an otherwise coherent snapshot.
		if(!m_haveGlobal.load(std::memory_order_acquire)
			|| !m_haveKit.load(std::memory_order_acquire))
			return;
		if(!m_automationReady.load(std::memory_order_acquire))
		{
			// This function also services every ready timer tick. Only a transition
			// starts the poll interval; moving it on every tick starves polling.
			m_lastStatePollMs.store(milliseconds(), std::memory_order_release);
			m_automationReady.store(true, std::memory_order_release);
		}
		if(getAutomationBaseChannel() == 0x7f)
		{
			// MIDI NONE is a valid firmware setting. The cache may become ready for
			// reads, but pending DAW intent must remain intact until a routable Global
			// dump is observed.
			return;
		}

		// Once ready is visible, one serialized non-realtime drain delivers both
		// queued hints and every dirty slot missed because the queue was full.
		drainRealtimeParameterChanges(
			RealtimeAutomationCapacity + m_automationSlots.size(), false);
	}

	std::optional<uint8_t> Controller::getPlayingStep() const
	{
		if(m_syntheticPlayingStepForTests)
			return *m_syntheticPlayingStepForTests;
		const auto status = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLiveDevice().status();
		if(!status || !status->sequencerPlaying || status->sequencerStep == md::MachineStatus::Values::NoStep)
			return std::nullopt;
		return status->sequencerStep;
	}

	std::optional<md::LiveKit> Controller::readLiveKit() const
	{
		if(m_syntheticLiveKitForTests)
			return *m_syntheticLiveKitForTests;
		return static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLiveDevice().liveKit();
	}

	void Controller::serviceLiveKit(const uint64_t _now)
	{
		if(_now - m_liveKitPollMs < LiveKitPollMilliseconds)
			return;
		m_liveKitPollMs = _now;

		// The tracks no CC went to during the last two reads: one sent before may already show
		for(size_t track = 0; track < m_trackDeliveries.size(); ++track)
		{
			const auto deliveries = m_trackDeliveries[track].load(std::memory_order_relaxed);
			if(deliveries != m_seenDeliveries[track])
			{
				m_seenDeliveries[track] = deliveries;
				m_quietReads[track] = 0;
			}
			else if(m_quietReads[track] < 2)
				++m_quietReads[track];
		}

		auto kit = readLiveKit();
		if(!kit || kit->tracks != getPartCount())
		{
			m_previousLiveKit.reset();
			return;
		}
		// A read not 50 ms of emulation newer than the last (the host not calling, the machine busy) waits
		// for one that is; one older comes from another machine
		if(m_previousLiveKit && kit->frame >= m_previousLiveKit->frame
			&& kit->frame - m_previousLiveKit->frame < md::g_samplerate / 20)
			return;
		const auto previous = std::exchange(m_previousLiveKit, kit);
		if(!previous || kit->frame < previous->frame)
			return;

		// The Machinedrum's LFOs and master effects, once two reads agree and the editor's last change to
		// them is old enough for the firmware to show it
		if(kit->machinedrum && previous->machinedrum)
		{
			const auto settled = [&](const uint64_t _editMs) { return _now - _editMs >= 3 * LiveKitPollMilliseconds; };
			for(uint8_t track = 0; track < kit->tracks; ++track)
			{
				if(kit->lfos[track] == previous->lfos[track] && settled(m_lfoEditMs[track]))
					storeLfo(track, kit->lfos[track], true);
			}
			if(kit->masterEffects == previous->masterEffects && settled(m_masterEffectEditMs))
				storeMasterEffects(md::automation::sysex::masterEffectsFromKit(kit->masterEffects.data()), true);
		}

		const bool mm = m_model == md::MachineModel::Monomachine;
		// The pages an assignment gives the machine's values (assignMachine), and the last page the live
		// Kit holds (the level's)
		const auto lastMachinePage = mm ? md::automation::monomachine::Synthesis : md::automation::machinedrum::Routing;
		const auto lastLivePage = mm ? md::automation::monomachine::Level : md::automation::machinedrum::Level;
		for(uint8_t track = 0; track < kit->tracks; ++track)
		{
			if(m_quietReads[track] < 2 || !kit->sameTrack(*previous, track, static_cast<uint8_t>(lastMachinePage + 1)))
				continue;
			const uint16_t machine = kit->machines[track];
			const auto stored = m_trackMachines[track].load(std::memory_order_acquire);
			// The editor's assignment: no read taken before the firmware applied it, even the same machine
			// again, and its machine expected until it shows or the firmware gave up on it
			if(m_assignmentMs[track])
			{
				const auto age = _now - m_assignmentMs[track];
				if(age < 3 * LiveKitPollMilliseconds || (machine != stored && age < AssignmentGraceMilliseconds))
					continue;
				m_assignmentMs[track] = 0;
			}
			// A machine changed on the front panel, or by a Kit loaded there: taken with its pages' values.
			// The first machine seen is only stored: the values the plug-in restores may still be on their way.
			const bool changed = machine != stored;
			if(changed)
			{
				m_trackMachines[track].store(machine, std::memory_order_release);
				m_machineRevision.fetch_add(1, std::memory_order_acq_rel);
			}
			const bool adopted = changed && stored != md::machines::g_unknown;
			for(auto& slot : m_automationSlots)
			{
				const auto& address = slot.address;
				if(address.track != track || address.page > lastLivePage)
					continue;
				if(!slot.valueUnknown.load(std::memory_order_acquire) && !(adopted && address.page <= lastMachinePage))
					continue;
				// An edit on its way to the firmware wins
				if(publicationIsDirty(slot.publication.load(std::memory_order_acquire)))
					continue;
				const auto value = publishFirmwareValue(address, kit->value(track, address.page, address.index));
				for(auto* const parameter : findSynthParam(track, address.page, address.index))
					parameter->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);
			}
		}
	}

	bool Controller::firmwareReadyForAutomation() const
	{
		if(m_syntheticFirmwareReadyForTests)
			return true;
		// Every request asks: through the device lock, each one paused the rendering. Another kind of
		// device (remote) is taken as ready.
		const auto status = static_cast<AudioPluginAudioProcessor&>(getProcessor()).getLiveDevice().status();
		return !status || status->firmwareReady;
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx& _message,
		synthLib::MidiEventSource)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		if(const auto status = md::automation::sysex::parseStatusResponse(
			m_model, _message))
		{
			switch(status->parameter)
			{
			case md::automation::sysex::StatusParameter::Global:
			{
				const auto now = milliseconds();
				const auto observation =
					m_globalSynchronization.observeStatus(status->value);
				if(!observation.accepted)
					return true;
				m_currentGlobal.store(status->value, std::memory_order_release);
				if(observation.requestDump)
				{
					m_automationReady.store(false, std::memory_order_release);
					m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
					m_haveGlobal.store(false, std::memory_order_release);
					m_globalSynchronization.dumpRequestSent(now);
					m_routingWritesAtGlobalRequest = m_routingWrites;
					sendSynchronizationRequest(toPluginSysex(
						md::automation::sysex::globalRequest(m_model, status->value)));
				}
				return true;
			}
			case md::automation::sysex::StatusParameter::Kit:
			{
				const auto now = milliseconds();
				const auto observation =
					m_kitSynchronization.observeStatus(status->value);
				if(!observation.accepted)
					return true;
				const auto previousKit = m_currentKit.exchange(status->value,
					std::memory_order_acq_rel);
				if(previousKit != status->value)
				{
					// The name follows with the new Kit's dump
					{
						const std::lock_guard lock(m_kitNameMutex);
						m_kitName.clear();
					}
					m_selectionRevision.fetch_add(1, std::memory_order_acq_rel);
				}
				if(observation.requestDump)
				{
					const auto forceApply = m_forceApplyRequestedKitDump.exchange(
						false, std::memory_order_acq_rel);
					// A status barrier after a timeout must not turn an unapplied
					// baseline/reload into a same-slot inspection. Retain its purpose
					// until an accepted dump consumes it, not merely a status reply.
					m_applyRequestedKitDump.store(
						m_applyRequestedKitDump.load(std::memory_order_acquire)
						|| previousKit == 0xff
						|| previousKit != status->value
						|| forceApply, std::memory_order_release);
					m_automationReady.store(false, std::memory_order_release);
					m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
					m_haveKit.store(false, std::memory_order_release);
					// Keep the original watermark on same-slot retries as well: a
					// direct edit observed since that request must still beat the stored
					// dump. A new selection or explicit resync starts a new watermark.
					if(previousKit != status->value
						|| m_kitDumpRequestRevision.load(std::memory_order_acquire) == 0)
					{
						const auto next = m_nextAutomationRevision.load(
							std::memory_order_acquire);
						m_kitDumpRequestRevision.store(next > 0 ? next - 1 : 0,
							std::memory_order_release);
					}
					m_kitSynchronization.dumpRequestSent(now);
					sendSynchronizationRequest(toPluginSysex(
						md::automation::sysex::kitRequest(m_model, status->value)));
				}
				return true;
			}
			case md::automation::sysex::StatusParameter::Pattern:
				// Edits waiting to be written belong to the pattern shown: written before another one replaces it
				if(m_patternWriteFirstMs)
				{
					bool otherSlot = false;
					{
						const std::lock_guard lock(m_patternMutex);
						otherSlot = m_pattern && m_pattern->slot != status->value;
					}
					if(otherSlot)
						(void)sendPattern();
				}
				if(m_currentPattern.exchange(status->value, std::memory_order_acq_rel) != status->value)
					m_selectionRevision.fetch_add(1, std::memory_order_acq_rel);
				// A status reply leads to a dump when requestPattern asked for one, or
				// when the pattern shown is no longer the current one.
				if(!m_patternWanted.load(std::memory_order_acquire))
				{
					const std::lock_guard lock(m_patternMutex);
					if((m_pattern && m_pattern->slot != status->value) || (m_mmPattern && m_mmPattern->slot != status->value))
						m_patternWanted.store(true, std::memory_order_release);
				}
				if(m_patternWanted.load(std::memory_order_acquire))
				{
					m_patternRequestedSlot.store(status->value, std::memory_order_release);
					sendEditorSysex(md::automation::sysex::patternRequest(m_model, status->value));
				}
				return true;
			}
		}

		if(const auto global = md::automation::sysex::parseGlobalDump(
			m_model, _message))
		{
			if(!m_globalSynchronization.acceptDump(global->slot))
				return true;
			// A periodic refresh may update the base channel while an otherwise-ready
			// snapshot is being serialized. Invalidate first so the snapshot either
			// observes the old generation in full or is rejected and retried.
			m_automationReady.store(false, std::memory_order_release);
			m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
			m_baseChannel.store(global->baseChannel, std::memory_order_release);
			m_haveGlobal.store(true, std::memory_order_release);
			// The Global is live: its routing replaces what was known, unless a routing
			// write went out after this dump was requested.
			if(global->trackOutputs && m_routingWrites == m_routingWritesAtGlobalRequest)
			{
				bool changed = false;
				for(size_t track = 0; track < m_trackOutputs.size(); ++track)
				{
					const auto output = static_cast<uint8_t>((*global->trackOutputs)[track]);
					changed |= m_trackOutputs[track].exchange(output, std::memory_order_acq_rel) != output;
				}
				if(changed)
					m_routingRevision.fetch_add(1, std::memory_order_acq_rel);
			}
			completeSynchronizationIfReady();
			return true;
		}

		if(const auto kit = md::automation::sysex::parseKitDump(
			m_model, _message))
		{
			// The library's Kit, read whatever the synchronization makes of the same dump
			if(m_libraryReading.load(std::memory_order_acquire) && m_libraryWaiting < getKitLibrarySize()
				&& kit->slot == m_libraryWaiting)
			{
				{
					const std::lock_guard lock(m_libraryMutex);
					if(kit->slot < m_library.size())
						m_library[kit->slot] = LibraryKit{true, kit->name, kit->machines};
				}
				m_libraryProgress.fetch_add(1, std::memory_order_acq_rel);
				m_libraryRevision.fetch_add(1, std::memory_order_acq_rel);
				requestLibraryItem(m_libraryWaiting + 1, milliseconds());
			}
			if(!m_kitSynchronization.acceptDump(kit->slot))
				return true;
			if(kit->slot == m_currentKit.load(std::memory_order_acquire))
			{
				const std::lock_guard lock(m_kitNameMutex);
				if(m_kitName != kit->name)
				{
					m_kitName = kit->name;
					m_selectionRevision.fetch_add(1, std::memory_order_acq_rel);
				}
			}
			const auto storeLfos = [&](const bool _authoritative)
			{
				if(!kit->lfos)
					return;
				for(uint8_t track = 0; track < kit->lfos->size(); ++track)
					storeLfo(track, (*kit->lfos)[track], _authoritative);
			};
			if(m_applyRequestedKitDump.exchange(false, std::memory_order_acq_rel))
			{
				applyKitParameters(kit->parameters);
				storeKitMachines(kit->machines, true);
				if(kit->masterEffects)
					storeMasterEffects(*kit->masterEffects, true);
				storeLfos(true);
			}
			else
			{
				storeKitMachines(kit->machines, false);
				if(kit->masterEffects)
					storeMasterEffects(*kit->masterEffects, false);
				storeLfos(false);
				// Even when the stored dump must not replace the live cache, retain its
				// raw values for firmware-backed diagnostics.
				for(const auto& change : kit->parameters)
				{
					if(auto* const slot = findAutomationSlot(
						{change.page, change.track, change.index}))
						slot->lastFirmwareKitValue.store(change.value,
							std::memory_order_release);
				}
			}
			m_haveKit.store(true, std::memory_order_release);
			m_kitDumpRequestRevision.store(0, std::memory_order_release);
			completeSynchronizationIfReady();
			return true;
		}

		if(m_model == md::MachineModel::Machinedrum)
		{
			if(auto pattern = md::automation::sysex::parseMdPatternDump(_message))
			{
				// Any pattern's length, for the chain
				static_cast<AudioPluginAudioProcessor&>(getProcessor()).getChainControl().setLength(pattern->slot, pattern->length);
				// The library's pattern and its trigs within its length: the dump holds them all, 64 steps
				// in its long form
				LibraryPattern stored{true, pattern->length, pattern->kit, std::nullopt};
				const auto inLength = pattern->length >= 64 ? ~uint64_t{0} : (uint64_t{1} << pattern->length) - 1;
				uint16_t trigs = 0;
				for(const auto mask : pattern->trigs)
					trigs += static_cast<uint16_t>(std::bitset<64>(mask & inLength).count());
				stored.trigs = trigs;
				storeLibraryPattern(pattern->slot, stored);
				// A copy's source, or the copy read back from its slot
				const auto copy = getPatternCopy();
				if(copy.state == PatternCopy::Reading && pattern->slot == copy.from)
					writePatternCopy(md::automation::sysex::Message(_message.begin(), _message.end()));
				else if(copy.state == PatternCopy::Writing && !m_patternCopyShown && pattern->slot == copy.to)
				{
					setPatternCopyState(m_patternCopySent && samePattern(*pattern, *m_patternCopySent)
						&& pattern->kit == m_patternCopySent->kit ? PatternCopy::Copied : PatternCopy::Refused);
				}
				if(m_patternWanted.load(std::memory_order_acquire)
					&& pattern->slot == m_patternRequestedSlot.load(std::memory_order_acquire))
				{
					{
						const std::lock_guard lock(m_patternMutex);
						// Writes are read back; only the reply to the last read-back asked
						// for tells what the firmware kept. Another pattern's dump ends the
						// wait: what was written is no longer the pattern shown.
						if((m_patternWritesInFlight > 0 || m_patternVerifyDueMs) && m_patternSent
							&& m_patternSent->slot != pattern->slot)
						{
							m_patternWritesInFlight = 0;
							m_patternVerifyDueMs = 0;
							m_patternWrite.store(PatternWrite::None, std::memory_order_release);
						}
						if(m_patternWritesInFlight > 0)
						{
							if(--m_patternWritesInFlight > 0)
								return true;
							// Written again since this read-back was asked for: the next one tells, and
							// this one, older than the write, must not replace the pattern shown
							if(m_patternVerifyDueMs)
								return true;
							m_patternWrite.store(m_patternSent && samePattern(*pattern, *m_patternSent)
								? PatternWrite::Written : PatternWrite::Refused, std::memory_order_release);
						}
						// Edits not sent yet stay on top of the same pattern, and so does what they started from.
						if(!m_patternEdited || !m_pattern || m_pattern->slot != pattern->slot)
						{
							m_patternMachineSlot = pattern->slot;
							m_pattern = std::move(*pattern);
							m_patternDump.assign(_message.begin(), _message.end());
							m_patternMachineDump = m_patternDump;
							m_patternEdited = false;
						}
					}
					// A copy onto the pattern shown ends with its write
					const auto write = m_patternWrite.load(std::memory_order_acquire);
					if(m_patternCopyShown && getPatternCopy().state == PatternCopy::Writing
						&& (write == PatternWrite::Written || write == PatternWrite::Refused))
						setPatternCopyState(write == PatternWrite::Written ? PatternCopy::Copied : PatternCopy::Refused);
					m_patternWanted.store(false, std::memory_order_release);
					m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
				}
				return true;
			}
		}
		else if(auto pattern = md::automation::sysex::parseMmPatternDump(_message))
		{
			// Any pattern's length, for the chain
			static_cast<AudioPluginAudioProcessor&>(getProcessor()).getChainControl().setLength(pattern->slot, pattern->length);
			// The library's pattern
			uint16_t trigs = 0;
			for(uint8_t track = 0; track < md::automation::sysex::MmPatternDump::TrackCount; ++track)
			{
				for(uint8_t step = 0; step < pattern->length; ++step)
					trigs += pattern->hasTrig(track, step) ? 1 : 0;
			}
			storeLibraryPattern(pattern->slot, LibraryPattern{true, pattern->length, pattern->kit, trigs});
			// A copy's source, or the copy read back from its slot; onto the pattern shown, the copy shows
			const auto copy = getPatternCopy();
			if(copy.state == PatternCopy::Reading && pattern->slot == copy.from)
				writePatternCopy(md::automation::sysex::Message(_message.begin(), _message.end()));
			else if(copy.state == PatternCopy::Writing && pattern->slot == copy.to)
			{
				bool shown = false;
				{
					const std::lock_guard lock(m_patternMutex);
					setPatternCopyState(m_mmPatternCopySent && sameMmPattern(*pattern, *m_mmPatternCopySent)
						? PatternCopy::Copied : PatternCopy::Refused);
					if(m_mmPattern && m_mmPattern->slot == pattern->slot && !m_patternEdited)
					{
						m_mmPattern = *pattern;
						m_patternDump.assign(_message.begin(), _message.end());
						shown = true;
					}
				}
				if(shown)
					m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
			}
			if(m_patternWanted.load(std::memory_order_acquire)
				&& pattern->slot == m_patternRequestedSlot.load(std::memory_order_acquire))
			{
				{
					const std::lock_guard lock(m_patternMutex);
					// A write read back, as on the Machinedrum: another pattern's dump ends the wait
					if(m_patternWritesInFlight > 0 && m_mmPatternSent && m_mmPatternSent->slot != pattern->slot)
					{
						m_patternWritesInFlight = 0;
						m_patternWrite.store(PatternWrite::None, std::memory_order_release);
					}
					if(m_patternWritesInFlight > 0)
					{
						--m_patternWritesInFlight;
						m_patternWrite.store(m_mmPatternSent && sameMmPattern(*pattern, *m_mmPatternSent)
							? PatternWrite::Written : PatternWrite::Refused, std::memory_order_release);
					}
					// Edits not sent yet stay on top of the same pattern
					if(!m_patternEdited || !m_mmPattern || m_mmPattern->slot != pattern->slot)
					{
						m_mmPattern = std::move(*pattern);
						m_patternDump.assign(_message.begin(), _message.end());
						m_patternEdited = false;
					}
				}
				m_patternWanted.store(false, std::memory_order_release);
				m_patternRevision.fetch_add(1, std::memory_order_acq_rel);
			}
			return true;
		}

		// External SET STATUS messages can change the active Global, selected Kit,
		// or Pattern without going through the controller. Refresh after the firmware
		// consumes the same queued MIDI event.
		if(const auto status = md::automation::sysex::parseSetStatus(m_model, _message))
		{
			if(status->parameter == md::automation::sysex::StatusParameter::Global)
			{
				m_automationReady.store(false, std::memory_order_release);
				m_synchronizationEpoch.fetch_add(1, std::memory_order_acq_rel);
				m_haveGlobal.store(false, std::memory_order_release);
				m_globalSynchronization.reset();
				sendMissingSynchronizationRequests();
			}
			else if(status->parameter == md::automation::sysex::StatusParameter::Kit
				|| status->parameter == md::automation::sysex::StatusParameter::Pattern)
			{
				requestKitState();
				// A pattern already shown follows the new selection.
				if(status->parameter == md::automation::sysex::StatusParameter::Pattern
					&& m_patternRevision.load(std::memory_order_acquire) > 0)
					requestPattern();
			}
		}
		return false;
	}

	bool Controller::parseControllerMessage(const synthLib::SMidiEvent& _event)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto change = md::automation::decodeParameterChange(m_model,
			{_event.a, _event.b, _event.c}, getAutomationBaseChannel());
		if(!change)
			return false;

		const auto& parameters = findSynthParam(change->track, change->page,
			change->index);
		if(parameters.empty())
			return false;
		const auto value = publishFirmwareValue(
			{change->page, change->track, change->index}, change->value);
		const auto origin = midiEventSourceToParameterOrigin(_event.source);
		for(auto* const parameter : parameters)
			parameter->setValueFromSynth(value, origin);
		return true;
	}

	bool Controller::parseMidiMessage(const synthLib::SMidiEvent& _event)
	{
		const std::lock_guard synchronizationLock(m_synchronizationLock);
		const auto handled = pluginLib::Controller::parseMidiMessage(_event);
		// Device-origin Program Change is outgoing firmware MIDI (for example from
		// an MM MIDI machine), not an instruction selecting the plug-in's Kit.
		if(_event.source != synthLib::MidiEventSource::Device
			&& _event.sysex.empty() && (_event.a & 0xf0) == 0xc0)
			requestKitState();
		return handled;
	}
}
