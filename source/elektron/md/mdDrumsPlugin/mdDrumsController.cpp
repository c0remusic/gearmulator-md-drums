#include "mdDrumsController.h"

#include "mdDrumsProcessor.h"

#include "mdProtocol/mdautomation.h"
#include "mdProtocol/mdmachines.h"

#include "synthLib/plugin.h"

namespace mdDrums
{
	namespace
	{
		namespace pages = messages::pages;

		// Where each page's parameters sit among a Track's 34, in the host's order: Machine, SYN1-8, the effects, the
		// routing, the LFO, Level, Mute, Solo, Out
		constexpr int g_machineAt = 0, g_synAt = 1, g_lfoAt = 25, g_levelAt = 30, g_muteAt = 31, g_mixerAt = 32;

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
			if(slot >= 0)
				m_addresses[static_cast<size_t>(slot)] = {index.page, index.partNum, index.paramNum};
		}

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
		for(auto* parameter : findSynthParam(change->track, change->page, change->index))
			parameter->setValueFromSynth(change->value, pluginLib::Parameter::Origin::PresetChange);
		return true;
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
