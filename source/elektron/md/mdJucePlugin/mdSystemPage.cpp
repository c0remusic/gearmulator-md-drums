#include "mdSystemPage.h"

#include "mdController.h"
#include "mdEditor.h"
#include "mdPluginProcessor.h"

#include "jucePluginEditorLib/pluginEditorState.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include "juce_events/juce_events.h"

namespace mdJucePlugin
{
	namespace
	{
		// A click that opens a window or a file chooser acts once the click is done, as from the menu
		template<typename Action>
		void later(Editor& _editor, Action _action)
		{
			juce::MessageManager::callAsync([lifetime = _editor.getLifetimeToken(), editor = &_editor, _action]
			{
				if(!lifetime.expired())
					_action(*editor);
			});
		}
	}

	SystemPage::SystemPage(Editor& _editor, AudioPluginAudioProcessor& _processor, Controller& _controller, Rml::Element& _document)
		: m_editor(_editor)
		, m_processor(_processor)
		, m_controller(_controller)
		, m_model(_processor.getModel())
	{
		m_root = _document.GetElementById("mdEdPageSystem");
		m_globalState = _document.GetElementById("mdSysGlobalState");
		m_follow = _document.GetElementById("mdSysFollowTempo");
		m_followState = _document.GetElementById("mdSysFollowState");
		m_parallel = _document.GetElementById("mdSysParallel");
		m_parallelState = _document.GetElementById("mdSysParallelState");
		m_ram = _document.GetElementById("mdSysRamComplete");
		m_ramState = _document.GetElementById("mdSysRamState");
		m_sysex = _document.GetElementById("mdSysSysex");
		m_sysexState = _document.GetElementById("mdSysSysexState");

		auto& config = m_processor.getConfig();
		if(m_follow)
		{
			juceRmlUi::EventListener::Add(m_follow, Rml::EventId::Click, [this, &config](Rml::Event&)
			{
				config.setValue(AudioPluginAudioProcessor::FollowHostTempoConfigKey, !m_processor.getFollowHostTempoSetting());
				config.saveIfNeeded();
				m_processor.applyFollowHostTempoSetting(true);
				m_dirty = true;
			});
		}
		if(m_parallel)
		{
			juceRmlUi::EventListener::Add(m_parallel, Rml::EventId::Click, [this, &config](Rml::Event&)
			{
				config.setValue(AudioPluginAudioProcessor::ParallelTransportConfigKey, !m_processor.getParallelTransportSetting());
				config.saveIfNeeded();
				m_processor.applyParallelTransportSetting();
				m_dirty = true;
			});
		}
		if(m_ram)
		{
			juceRmlUi::EventListener::Add(m_ram, Rml::EventId::Click, [this](Rml::Event&)
			{
				if(!m_processor.isRamRecordingModeAvailable())
					return;
				m_processor.setRamRecordingMode(m_processor.getRamRecordingMode() == md::RamRecordingMode::CompleteTail
					? md::RamRecordingMode::Original : md::RamRecordingMode::CompleteTail);
				m_dirty = true;
			});
		}
		if(m_sysex)
		{
			// Cancel a transfer that can be, resume one waiting for the machine, or choose a file
			juceRmlUi::EventListener::Add(m_sysex, Rml::EventId::Click, [this](Rml::Event&)
			{
				if(m_editor.isUserSysexTransferActive())
				{
					if(m_editor.canCancelUserSysexTransfer())
						m_editor.cancelUserSysexTransfer();
				}
				else if(m_editor.canResumeUserSysexTransfer())
					m_editor.resumeUserSysexTransfer();
				else
					later(m_editor, [](Editor& _editor) { _editor.chooseUserSysexFile(); });
				m_dirty = true;
			});
		}
		if(auto* storage = _document.GetElementById("mdSysStorage"))
		{
			juceRmlUi::EventListener::Add(storage, Rml::EventId::Click, [this](Rml::Event&)
			{
				later(m_editor, [](Editor& _editor) { _editor.chooseStorageImage(); });
			});
		}
		if(auto* settings = _document.GetElementById("mdSysSettings"))
		{
			juceRmlUi::EventListener::Add(settings, Rml::EventId::Click, [this](Rml::Event&)
			{
				later(m_editor, [](Editor& _editor) { _editor.showSettings(true); });
			});
		}
		for(size_t i = 0; i < Scales.size(); ++i)
		{
			m_scales[i] = _document.GetElementById("mdSysScale" + std::to_string(Scales[i]));
			if(!m_scales[i])
				continue;
			// As the settings window does: the config, then the window takes the scale
			juceRmlUi::EventListener::Add(m_scales[i], Rml::EventId::Click, [this, &config, scale = Scales[i]](Rml::Event&)
			{
				config.setValue("scale", juce::var(scale));
				config.saveIfNeeded();
				if(const auto* state = m_processor.getEditorState())
					state->evSetGuiScale(scale);
				m_dirty = true;
			});
		}
		m_scaleState = _document.GetElementById("mdSysScaleState");
		m_capture = _document.GetElementById("mdSysCapture");
		m_diagnosticsState = _document.GetElementById("mdSysDiagnosticsState");
		if(m_capture)
		{
			juceRmlUi::EventListener::Add(m_capture, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_processor.setPerformanceDiagnosticsEnabled(!m_processor.performanceDiagnosticsActive());
				m_dirty = true;
			});
		}
		if(auto* logs = _document.GetElementById("mdSysLogs"))
		{
			juceRmlUi::EventListener::Add(logs, Rml::EventId::Click, [this](Rml::Event&)
			{
				later(m_editor, [folder = m_processor.performanceDiagnosticsFolder()](Editor&)
				{
					if(folder.createDirectory().wasOk())
						folder.revealToUser();
				});
			});
		}
	}

	std::string SystemPage::globalLine(const md::MachineModel _model, const uint8_t _global, const bool _known, const uint8_t _baseChannel)
	{
		std::string text = _global < 8 ? "Global " + std::to_string(_global + 1) + " · " : std::string();
		if(!_known)
			return text + "MIDI : en attente du Global";
		if(_baseChannel == 0x7f)
			return text + "MIDI : aucun canal (NONE), l'automation attend";
		const int first = _baseChannel + 1;
		const int last = first + (_model == md::MachineModel::Monomachine ? 6 : 4) - 1;
		text += "MIDI : canal de base " + std::to_string(first);
		if(last <= 16)
			text += ", pistes sur les canaux " + std::to_string(first) + " à " + std::to_string(last);
		return text;
	}

	std::string SystemPage::followLine(const md::MachineModel _model, const md::HostSync::State _state, const bool _wanted)
	{
		using State = md::HostSync::State;
		switch(_state)
		{
		case State::Unknown:
			return "en attente du démarrage de la machine";
		case State::Applying:
			return _wanted ? "réglage de la machine pour suivre l'hôte…" : "retour au tempo propre de la machine…";
		case State::Following:
			return "la machine suit le tempo et le transport de l'hôte";
		case State::NotFollowing:
			return "la machine tourne sur son propre tempo";
		case State::Failed:
			return _model == md::MachineModel::Monomachine
				? "la machine n'a pas pris le réglage : GLOBAL > CONTROL > CONTROL IN sur la machine"
				: "la machine n'a pas pris le réglage : GLOBAL > SYNC sur la machine";
		}
		return {};
	}

	std::string SystemPage::diagnosticsLine(const std::optional<synthLib::PerformanceReport::Status> _state, const bool _folderError)
	{
		using Status = synthLib::PerformanceReport::Status;
		if(_folderError)
			return "dossier des journaux impossible à créer";
		switch(_state.value_or(Status::Idle))
		{
		case Status::Starting:
			return "démarrage de la capture…";
		case Status::Recording:
			return "capture en cours : 10 minutes ou 8 Mio au plus";
		case Status::Stopped:
			return "rapport enregistré ; JOURNAUX… ouvre son dossier";
		case Status::LimitReached:
			return "limite atteinte : rapport enregistré, capture arrêtée";
		case Status::Error:
			return "rapport non écrit : vérifier l'espace libre et les droits";
		case Status::Idle:
			break;
		}
		return "aucune capture ; CAPTURE mesure l'émulation dans un rapport";
	}

	int SystemPage::currentScale() const
	{
		return juce::roundToInt(m_processor.getConfig().getDoubleValue("scale", 100));
	}

	bool SystemPage::setText(Rml::Element* _element, std::string& _shown, const std::string& _text)
	{
		if(!_element || _text == _shown)
			return false;
		_shown = _text;
		_element->SetInnerRML(Rml::StringUtilities::EncodeRml(_text));
		return true;
	}

	bool SystemPage::setChecked(Rml::Element* _button, int& _shown, const bool _checked)
	{
		if(!_button || static_cast<int>(_checked) == _shown)
			return false;
		_shown = _checked;
		juceRmlUi::ElemButton::setChecked(_button, _checked);
		return true;
	}

	bool SystemPage::update(const double _nowMilliseconds)
	{
		// Some states take the device lock: only while the page is shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_dirty = true;
			return false;
		}
		if(!m_dirty && _nowMilliseconds - m_lastRefresh < RefreshMilliseconds)
			return false;
		m_dirty = false;
		m_lastRefresh = _nowMilliseconds;
		bool changed = false;

		changed |= setText(m_globalState, m_shownGlobal, globalLine(m_model, m_controller.getCurrentGlobal(),
			m_controller.hasAutomationGlobalSnapshot() || m_controller.isAutomationSynchronized(), m_controller.getAutomationBaseChannel()));

		const bool follow = m_processor.getFollowHostTempoSetting();
		changed |= setChecked(m_follow, m_shownFollowChecked, follow);
		changed |= setText(m_followState, m_shownFollow, followLine(m_model, m_processor.getHostSyncState(), follow));

		const bool wanted = m_processor.getParallelTransportSetting();
		const bool active = m_processor.isParallelTransportActive();
		const bool latency = m_processor.getPlugin().getLatencyBlocks() > 0;
		changed |= setChecked(m_parallel, m_shownParallelChecked, wanted);
		std::string transport;
		if(wanted && active)
			transport = latency ? "actif : l'émulation tourne sur ses propres fils"
				: "actif ; une latence de 1 ou 2 blocs sortirait l'émulation du fil audio de l'hôte";
		else if(wanted)
			transport = "démarre une fois la machine prête";
		else if(active)
			transport = "reste actif jusqu'au rechargement du plug-in";
		else
			transport = "toute la machine tourne sur un seul cœur";
		changed |= setText(m_parallelState, m_shownParallel, transport);

		if(m_ram)
		{
			const bool available = m_processor.isRamRecordingModeAvailable();
			const bool complete = available && m_processor.getRamRecordingMode() == md::RamRecordingMode::CompleteTail;
			changed |= setChecked(m_ram, m_shownRamChecked, complete);
			m_ram->SetClass("mdEdUnread", !available);
			changed |= setText(m_ramState, m_shownRam, !available ? std::string("indisponible pour cette machine")
				: complete ? "queues complètes : les enregistrements RAM gardent leur fin"
				: "finalisation d'origine, comme la machine");
		}

		std::string sysex;
		std::string button = "FICHIER…";
		if(m_editor.isUserSysexTransferActive())
		{
			const bool cancellable = m_editor.canCancelUserSysexTransfer();
			sysex = cancellable ? "transfert en cours" : "transfert en cours, il se termine";
			button = cancellable ? "ANNULER" : "EN COURS";
		}
		else if(m_editor.canResumeUserSysexTransfer())
		{
			sysex = "transfert en attente : la machine est prête";
			button = "REPRENDRE";
		}
		else
			sysex = "aucun transfert ; envoie un fichier .syx à la machine";
		changed |= setText(m_sysexState, m_shownSysex, sysex);
		changed |= setText(m_sysex, m_shownSysexButton, button);

		// A size dragged with the window's corner selects no scale; the right column shows it
		const auto scale = currentScale();
		if(scale != m_shownScaleChoice)
		{
			m_shownScaleChoice = scale;
			for(size_t i = 0; i < Scales.size(); ++i)
			{
				if(m_scales[i])
					m_scales[i]->SetClass("mdEdSelected", Scales[i] == scale);
			}
			changed = true;
		}
		changed |= setText(m_scaleState, m_shownScale, std::to_string(scale) + " %");

		changed |= setChecked(m_capture, m_shownCaptureChecked, m_processor.performanceDiagnosticsActive());
		changed |= setText(m_diagnosticsState, m_shownDiagnostics,
			diagnosticsLine(m_processor.performanceDiagnosticsState(), m_processor.performanceDiagnosticsFolderError()));
		return changed;
	}
}
