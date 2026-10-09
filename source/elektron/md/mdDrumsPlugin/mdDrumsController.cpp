#include "mdDrumsController.h"

#include "mdDrumsProcessor.h"

#include "mdProtocol/mdautomation.h"
#include "mdProtocol/mdmachines.h"

#include "synthLib/plugin.h"

#include <algorithm>
#include <cstring>

namespace mdDrums
{
	namespace
	{
		namespace pages = messages::pages;

		// Where each page's parameters sit among a Track's 34, in the host's order: Machine, SYN1-8, the effects, the
		// routing, the LFO, Level, Mute, Solo, Out
		constexpr int g_machineAt = 0, g_synAt = 1, g_lfoAt = 25, g_levelAt = 30, g_muteAt = 31, g_mixerAt = 32;

		// What each relay aims at on the Track shown: page and index; the relay Track (0) chooses the Track itself
		struct RelayTarget
		{
			uint8_t page;
			uint8_t index;
		};
		constexpr std::array<RelayTarget, Controller::RelayCount> g_relayTargets{{
			{pages::Focus, 0}, {pages::Machine, 0}, {pages::Level, 0}, {pages::Mute, 0}, {pages::Mixer, 0},
			{pages::Mixer, 1}, {pages::Lfo, 2}, {pages::Lfo, 3},
			{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7},
			{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {1, 7},
			{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {2, 7}}};

		size_t lowestBit(uint64_t _bits)
		{
			size_t bit = 0;
			while(!(_bits & 1))
			{
				_bits >>= 1;
				++bit;
			}
			return bit;
		}

		// A host parameter that publishes its value without locking (host automation may come on the audio thread),
		// and that names SYN1-8 after the Track's machine ("PTCH 64")
		class DrumsParameter final : public pluginLib::Parameter
		{
		public:
			DrumsParameter(pluginLib::Controller& _controller, const pluginLib::Description& _description,
				const uint8_t _part, const int _uid, const pluginLib::Parameter::PartFormatter& _formatter)
				: pluginLib::Parameter(_controller, _description, _part, _uid, _formatter), m_drums(_controller)
			{
			}

			// The master effects and the relays without a prefix ("ECHO TIME", "FLTF" on Push 2's display), the Tracks'
			// with theirs ("Track 1 FLTF")
			juce::String getName(const int _maximumStringLength) const override
			{
				if(!getDescription().isNonPartSensitive())
					return pluginLib::Parameter::getName(_maximumStringLength);
				return juce::String(getDescription().displayName).substring(0, _maximumStringLength);
			}

			juce::String getText(const float _normalisedValue, const int _maximumLength) const override
			{
				const auto& description = getDescription();
				if(description.page != pages::Synthesis)
					return pluginLib::Parameter::getText(_normalisedValue, _maximumLength);
				const auto value = juce::roundToInt(convertFrom0to1(_normalisedValue));
				const auto* machine = m_drums.getParameter("Machine", getPart());
				const auto* names = machine ? md::machines::parameterNames(md::MachineModel::Machinedrum,
					static_cast<uint16_t>(machine->getUnnormalizedValue())) : nullptr;
				if(!names)
					return juce::String(value);
				const auto& name = (*names)[description.index];
				return name.empty() ? juce::String("-- ") + juce::String(value)
					: juce::String(std::string(name)) + " " + juce::String(value);
			}

		protected:
			bool shouldSendRepeatedHostValues() const override { return true; }

		private:
			pluginLib::Controller& m_drums;
		};
	}

	Controller::Controller(Processor& _processor) : pluginLib::Controller(_processor, "parameterDescriptions_mddrums.json")
	{
		registerParams(_processor, [](const uint8_t _part, const bool _nonPartSensitive)
		{
			return _nonPartSensitive ? juce::String("Master") : juce::String("Track ") + juce::String(_part + 1);
		});

		for(const auto& [index, parameters] : getExposedParameters())
		{
			const auto slot = slotOf(index.page, index.partNum, index.paramNum);
			if(slot < 0)
				continue;
			m_addresses[static_cast<size_t>(slot)] = {index.page, index.partNum, index.paramNum};
			if(!parameters.empty())
				m_parameters[static_cast<size_t>(slot)] = parameters.front();
		}
		for(uint8_t i = 0; i < RelayCount; ++i)
		{
			const auto relays = findSynthParam(0, pages::Focus, i);
			m_relays[i] = relays.empty() ? nullptr : relays.front();
		}
		m_relayShown.fill(-1);

		// The parameters start as the Device does (factory Kit 1); a loaded state replaces them in onStateLoaded
		std::vector<uint8_t> state;
		if(_processor.getPlugin().getState(state, synthLib::StateTypeGlobal))
			applyStateBytes(state);
	}

	Controller::~Controller()
	{
		stopControllerTimer();
	}

	int Controller::slotOf(const uint8_t _page, const uint8_t _part, const uint8_t _index)
	{
		if(_page == pages::Master)
			return _index < MasterParameterCount ? static_cast<int>(TrackCount * TrackParameterCount + _index) : -1;
		if(_part >= TrackCount)
			return -1;
		int local = -1;
		switch(_page)
		{
		case pages::Synthesis:
		case pages::Effects:
		case pages::Routing:	local = _index < 8 ? g_synAt + _page * 8 + _index : -1; break;
		case pages::Level:		local = _index == 0 ? g_levelAt : -1; break;
		case pages::Mute:		local = _index == 0 ? g_muteAt : -1; break;
		case pages::Machine:	local = _index == 0 ? g_machineAt : -1; break;
		case pages::Lfo:		local = _index < 5 ? g_lfoAt + _index : -1; break;
		case pages::Mixer:		local = _index < 2 ? g_mixerAt + _index : -1; break;
		default:				break;
		}
		return local < 0 ? -1 : static_cast<int>(_part * TrackParameterCount + static_cast<size_t>(local));
	}

	pluginLib::Parameter* Controller::createParameter(pluginLib::Controller& _controller,
		const pluginLib::Description& _description, const uint8_t _part, const int _uid,
		const pluginLib::Parameter::PartFormatter& _formatter)
	{
		return new DrumsParameter(_controller, _description, _part, _uid, _formatter);
	}

	void Controller::sendParameterChange(const pluginLib::Parameter& _parameter, const pluginLib::ParamValue _value,
		pluginLib::Parameter::Origin)
	{
		// Any thread: the value in its slot, the slot marked; the audio thread sends it
		const auto& description = _parameter.getDescription();
		if(description.page == pages::Focus)
		{
			relayChanged(description.index, _value);
			return;
		}
		const auto slot = slotOf(static_cast<uint8_t>(description.page), _parameter.getPart(), description.index);
		if(slot < 0)
			return;
		m_values[static_cast<size_t>(slot)].store(_value, std::memory_order_relaxed);
		m_dirty[static_cast<size_t>(slot) / 64].fetch_or(1ull << (slot % 64), std::memory_order_release);
	}

	void Controller::processRealtimeParameterChanges(const size_t _maximum)
	{
		size_t sent = 0;
		for(size_t word = 0; word < m_dirty.size(); ++word)
		{
			auto bits = m_dirty[word].exchange(0, std::memory_order_acquire);
			while(bits)
			{
				const auto bit = lowestBit(bits);
				bits &= bits - 1;
				const auto slot = word * 64 + bit;
				const auto& address = m_addresses[slot];
				const auto value = m_values[slot].load(std::memory_order_relaxed);
				const auto message = messages::parameterMessage(address.page, address.part, address.index, value);
				if(!message)
					continue;
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
				event.assignRawData(message->bytes.data(), message->size, synthLib::MidiEventSource::Editor, 0);
				if(sent >= _maximum || !getProcessor().tryAddRealtimeMidiEvent(event))
				{
					// The rest waits for the next block, this one included
					m_dirty[word].fetch_or(bits | (1ull << bit), std::memory_order_release);
					return;
				}
				++sent;
			}
		}

		// The Tracks the editor plays, after every change published before them
		auto tracks = m_auditions.exchange(0, std::memory_order_acquire);
		while(tracks)
		{
			const auto track = lowestBit(tracks);
			tracks &= tracks - 1;
			synthLib::SMidiEvent note(synthLib::MidiEventSource::Editor, synthLib::M_NOTEON,
				static_cast<uint8_t>(FirstNote + track), m_auditionVelocity[track].load(std::memory_order_relaxed));
			if(sent >= _maximum || !getProcessor().tryAddRealtimeMidiEvent(note))
			{
				m_auditions.fetch_or(static_cast<uint32_t>(tracks | (1ull << track)), std::memory_order_release);
				return;
			}
			++sent;
		}
	}

	void Controller::audition(const uint8_t _track, const uint8_t _velocity)
	{
		if(_track >= TrackCount)
			return;
		m_auditionVelocity[_track].store(std::clamp<uint8_t>(_velocity, 1, 127), std::memory_order_relaxed);
		m_auditions.fetch_or(1u << _track, std::memory_order_release);
	}

	bool Controller::parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource)
	{
		return false;
	}

	void Controller::onControllerTimer()
	{
		// An offline render may service the Controller from its own thread: juce::Value only on the message thread
		const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
		if(!messageManager || !messageManager->isThisTheMessageThread())
			return;
		for(const auto& [index, parameters] : getExposedParameters())
			for(auto* parameter : parameters)
				parameter->flushRealtimeValueToUi();
		syncRelays();
	}

	void Controller::relayChanged(const uint8_t _index, const int _value)
	{
		if(_index >= RelayCount)
			return;
		if(_index == 0)
		{
			m_pendingPart.store(std::clamp(_value, 0, TrackCount - 1), std::memory_order_release);
			return;
		}
		const auto& target = g_relayTargets[_index];
		const auto slot = slotOf(target.page, m_shownPart.load(std::memory_order_acquire), target.index);
		if(slot < 0)
			return;
		// To the Device at the next block, as host automation goes; to the parameter on the message thread
		m_values[static_cast<size_t>(slot)].store(_value, std::memory_order_relaxed);
		m_dirty[static_cast<size_t>(slot) / 64].fetch_or(1ull << (slot % 64), std::memory_order_release);
		m_relaySlot[_index].store(slot, std::memory_order_relaxed);
		m_relayPending.fetch_or(1u << _index, std::memory_order_release);
	}

	void Controller::syncRelays()
	{
		// The Track shown: the relay Track's request, else the editor's
		if(const auto part = m_pendingPart.exchange(-1, std::memory_order_acq_rel); part >= 0)
			setCurrentPart(static_cast<uint8_t>(part));
		const auto part = getCurrentPart();
		m_shownPart.store(part, std::memory_order_release);

		// What a relay was set to, given to its parameter (which the Device has by now, or at the next block)
		auto pending = m_relayPending.exchange(0, std::memory_order_acq_rel);
		while(pending)
		{
			const auto i = lowestBit(pending);
			pending &= pending - 1;
			const auto slot = m_relaySlot[i].load(std::memory_order_relaxed);
			const auto value = m_values[static_cast<size_t>(slot)].load(std::memory_order_relaxed);
			if(auto* parameter = m_parameters[static_cast<size_t>(slot)])
				parameter->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);
			m_relayShown[i] = value;
		}

		// What the Track shown holds, given to the relays whose parameter moved since (a relay just set waits for its
		// value to come back here, so that a turn is never undone)
		for(size_t i = 0; i < RelayCount; ++i)
		{
			auto* relay = m_relays[i];
			if(!relay)
				continue;
			int value = part;
			if(i > 0)
			{
				const auto slot = slotOf(g_relayTargets[i].page, part, g_relayTargets[i].index);
				const auto* parameter = slot >= 0 ? m_parameters[static_cast<size_t>(slot)] : nullptr;
				if(!parameter)
					continue;
				value = parameter->getUnnormalizedValue();
			}
			if(value == m_relayShown[i])
				continue;
			relay->setValueFromSynth(value, pluginLib::Parameter::Origin::Midi);
			m_relayShown[i] = value;
		}
	}

	void Controller::preparePushList()
	{
		std::vector<pluginLib::Parameter*> list(m_relays.begin(), m_relays.end());
		for(uint8_t i = 0; i < MasterParameterCount; ++i)
			for(auto* parameter : findSynthParam(0, pages::Master, i))
				list.push_back(parameter);
		for(auto* parameter : list)
		{
			if(!parameter)
				continue;
			parameter->beginChangeGesture();
			parameter->juce::AudioProcessorParameter::setValueNotifyingHost(parameter->getValue());
			parameter->endChangeGesture();
		}
	}

	bool Controller::parseControllerMessage(const synthLib::SMidiEvent& _event)
	{
		// The Device's own word: a new machine's SYN1-8. The host's CCs are no parameter changes here.
		if(_event.source != synthLib::MidiEventSource::Device)
			return false;
		const auto change = md::automation::decodeParameterChange(md::MachineModel::Machinedrum,
			{_event.a, _event.b, _event.c}, 0);
		if(!change)
			return false;
		// The host learns the new values as it does a Kit's, without a gesture (ticket 10)
		for(auto* parameter : findSynthParam(change->track, change->page, change->index))
		{
			parameter->setValueFromSynth(change->value, pluginLib::Parameter::Origin::PresetChange);
			parameter->sendValueChangedMessageToListeners(parameter->getValue());
		}
		return true;
	}

	md::automation::sysex::MdKit Controller::playedKit() const
	{
		auto kit = m_kitBase;
		for(size_t slot = 0; slot < ParameterCount; ++slot)
		{
			const auto* parameter = m_parameters[slot];
			const auto& address = m_addresses[slot];
			if(parameter && messages::isKitParameter(address.page))
				messages::setKitValue(kit, address.page, address.part, address.index, parameter->getUnnormalizedValue());
		}
		return kit;
	}

	void Controller::setPlayedSlot(const int _slot)
	{
		m_slot = std::clamp(_slot, 0, 63);
	}

	void Controller::loadKit(const md::automation::sysex::MdKit& _kit, const int _slot)
	{
		m_kitBase = _kit;
		setPlayedSlot(_slot);

		// What waits for the Device is older than the Kit
		for(size_t slot = 0; slot < ParameterCount; ++slot)
			if(messages::isKitParameter(m_addresses[slot].page))
				m_dirty[slot / 64].fetch_and(~(1ull << (slot % 64)), std::memory_order_acq_rel);

		synthLib::SMidiEvent dump(synthLib::MidiEventSource::Editor);
		const auto bytes = md::automation::sysex::mdKitDump(_kit, 0);
		dump.sysex.assign(bytes.begin(), bytes.end());
		getProcessor().addMidiEvent(dump);

		const messages::State state{_kit, {}};
		for(const auto& [index, parameters] : getExposedParameters())
		{
			if(!messages::isKitParameter(index.page))
				continue;
			const auto value = messages::parameterValue(state, index.page, index.partNum, index.paramNum);
			if(!value)
				continue;
			for(auto* parameter : parameters)
				parameter->setValueFromSynth(*value, pluginLib::Parameter::Origin::PresetChange);
		}
		getProcessor().notifyHostOfProgramChange();
	}

	void Controller::renameKit(const std::string& _name)
	{
		m_kitBase.name.fill(0);
		std::memcpy(m_kitBase.name.data(), _name.data(), std::min(_name.size(), m_kitBase.name.size()));
		const auto message = messages::kitName(m_kitBase.name);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(message.bytes.begin(), message.bytes.begin() + message.size);
		getProcessor().addMidiEvent(event);
	}

	void Controller::setLink(const uint8_t _track, const uint8_t _target)
	{
		if(_track >= TrackCount)
			return;
		const auto target = _target < TrackCount && _target != _track ? _target : Off;
		m_kitBase.links[_track] = target;
		if(const auto message = md::automation::sysex::linkChange(_track, target))
			sendToDevice(*message);
	}

	void Controller::setChoke(const uint8_t _track, const uint8_t _target)
	{
		if(_track >= TrackCount)
			return;
		const auto target = _target < TrackCount && _target != _track ? _target : Off;
		m_kitBase.chokes[_track] = target;
		if(const auto message = md::automation::sysex::chokeChange(_track, target))
			sendToDevice(*message);
	}

	void Controller::sendToDevice(const std::vector<uint8_t>& _sysex)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_sysex.begin(), _sysex.end());
		getProcessor().addMidiEvent(event);
	}

	void Controller::setLoadedState(std::vector<uint8_t> _state)
	{
		m_loadedState = std::move(_state);
	}

	void Controller::onStateLoaded()
	{
		if(m_loadedState.empty())
			return;
		if(applyStateBytes(m_loadedState))
			getProcessor().notifyHostOfProgramChange();
		m_loadedState.clear();
	}

	bool Controller::applyStateBytes(const std::vector<uint8_t>& _state)
	{
		// synthLib::Plugin writes its version and the state type before the Device's own bytes
		if(_state.size() < 2)
			return false;
		const auto state = messages::parseState(_state.data() + 2, _state.size() - 2);
		if(!state)
			return false;
		applyState(*state);
		return true;
	}

	void Controller::applyState(const messages::State& _state)
	{
		m_kitBase = _state.kit;
		for(const auto& [index, parameters] : getExposedParameters())
		{
			const auto value = messages::parameterValue(_state, index.page, index.partNum, index.paramNum);
			if(!value)
				continue;
			for(auto* parameter : parameters)
				parameter->setValueFromSynth(*value, pluginLib::Parameter::Origin::PresetChange);
		}
	}
}
