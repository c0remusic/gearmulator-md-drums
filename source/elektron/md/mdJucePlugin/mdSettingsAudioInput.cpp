#include "mdSettingsAudioInput.h"

#include "mdPluginProcessor.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginEditorLib/settingsPlugin.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "juce_audio_utils/juce_audio_utils.h"
#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

#include "RmlUi/Core/Element.h"

namespace mdJucePlugin
{
	namespace
	{
		juce::StandalonePluginHolder* standaloneHolder(juce::AudioProcessor& _processor)
		{
			auto* const holder = juce::StandalonePluginHolder::getInstance();
			return holder && holder->processor.get() == &_processor ? holder : nullptr;
		}
	}

	SettingsAudioInput::SettingsAudioInput(jucePluginEditorLib::Processor& _processor,
		Rml::Element* _root)
		: m_processor(_processor)
		, m_status(juceRmlUi::helper::findChild(_root, "audioInputStatus"))
		, m_settings(juceRmlUi::helper::findChild(_root, "btAudioInputSettings"))
		, m_transportStatus(juceRmlUi::helper::findChild(_root, "parallelTransportStatus", false))
		, m_hostSyncStatus(juceRmlUi::helper::findChild(_root, "hostSyncStatus", false))
	{
		juceRmlUi::EventListener::AddClick(m_settings, [this]
		{
			if(auto* const holder = standaloneHolder(m_processor))
				holder->showAudioSettingsDialog();
		});
		if(m_hostSyncStatus)
		{
			jucePluginEditorLib::SettingsPlugin::createToggleButton(_root, "btFollowHostTempo",
				m_processor.getConfig(), AudioPluginAudioProcessor::FollowHostTempoConfigKey, [this](bool)
				{
					static_cast<AudioPluginAudioProcessor&>(m_processor).applyFollowHostTempoSetting(true);
					updateHostSyncStatus();
				}, false);
		}
		// Both machines' pages carry the transport switch; the toggle writes
		// the config value, the processor pushes it to the device.
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		if(processor.supportsParallelTransport())
		{
			jucePluginEditorLib::SettingsPlugin::createToggleButton(_root, "btParallelTransport",
				m_processor.getConfig(), AudioPluginAudioProcessor::ParallelTransportConfigKey, [this](bool)
				{
					static_cast<AudioPluginAudioProcessor&>(m_processor).applyParallelTransportSetting();
					updateTransportStatus();
				}, true);
		}
		timerCallback();
		startTimerHz(2);
	}

	SettingsAudioInput::~SettingsAudioInput()
	{
		stopTimer();
	}

	void SettingsAudioInput::timerCallback()
	{
		auto* const holder = standaloneHolder(m_processor);
		std::string status;
		if(holder)
		{
			if(static_cast<bool>(holder->getMuteInputValue().getValue()))
				status = "Audio input is muted. Open Audio/MIDI Settings and clear Mute audio input to use inputs A/B.";
			else if(auto* const device = holder->deviceManager.getCurrentAudioDevice();
				!device || device->getActiveInputChannels().isZero())
				status = "No audio input channels are active. Select an input device and channels in Audio/MIDI Settings.";
			else
				status = "Audio input is enabled. Route inputs A/B through an input or FX machine and trigger its track to hear it.";
		}
		else
			status = "Your DAW supplies inputs A/B. Route an audio source to this plug-in's stereo input, then select and trigger an input or FX machine.";

		if(status != m_lastStatus)
		{
			m_status->SetInnerRML(Rml::StringUtilities::EncodeRml(status));
			m_settings->SetProperty(Rml::PropertyId::Display,
				holder ? Rml::Style::Display::Block : Rml::Style::Display::None);
			m_lastStatus = std::move(status);
		}
		updateTransportStatus();
		updateHostSyncStatus();
	}

	void SettingsAudioInput::updateHostSyncStatus()
	{
		if(!m_hostSyncStatus)
			return;
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		const bool wanted = processor.getFollowHostTempoSetting();
		using State = md::HostSync::State;
		std::string status;
		switch(processor.getHostSyncState())
		{
		case State::Unknown:
			status = "Waiting for the machine to boot.";
			break;
		case State::Applying:
			status = wanted ? "Setting the machine to follow the host..." : "Giving the machine its own tempo back...";
			break;
		case State::Following:
			status = "The machine follows the host's tempo and transport.";
			break;
		case State::NotFollowing:
			status = "The machine runs on its own tempo.";
			break;
		case State::Failed:
			status = processor.getModel() == md::MachineModel::Monomachine
				? "The machine did not take the setting. Set it on the machine: GLOBAL > CONTROL > CONTROL IN."
				: "The machine did not take the setting. Set it on the machine: GLOBAL > SYNC.";
			break;
		}
		if(status != m_lastHostSyncStatus)
		{
			m_hostSyncStatus->SetInnerRML(Rml::StringUtilities::EncodeRml(status));
			m_lastHostSyncStatus = std::move(status);
		}
	}

	void SettingsAudioInput::updateTransportStatus()
	{
		if(!m_transportStatus)
			return;
		auto& processor = static_cast<AudioPluginAudioProcessor&>(m_processor);
		const bool wanted = processor.getParallelTransportSetting();
		const bool active = processor.isParallelTransportActive();
		const bool latency = processor.getPlugin().getLatencyBlocks() > 0;
		std::string status;
		if(wanted && active)
			status = latency ? "Parallel transport is running."
				: "Parallel transport is running. Set the latency to 1 or 2 blocks to move the emulation off the host's audio thread.";
		else if(wanted)
			status = "Parallel transport starts once the machine has finished booting.";
		else if(active)
			status = "Parallel transport stays on until the plug-in is reloaded.";
		else
			status = "The whole machine runs on one CPU core.";
		if(status != m_lastTransportStatus)
		{
			m_transportStatus->SetInnerRML(Rml::StringUtilities::EncodeRml(status));
			m_lastTransportStatus = std::move(status);
		}
	}
}
