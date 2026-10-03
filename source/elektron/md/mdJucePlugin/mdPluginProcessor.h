#pragma once

#include "jucePluginEditorLib/pluginProcessor.h"
#include "mdChainControl.h"
#include "mdLiveDevice.h"
#include "mdOutputMeters.h"
#include "mdLib/mdhostsync.h"
#include "mdLib/mdmmpatternwriter.h"
#include "mdLib/mdtypes.h"
#include "synthLib/performanceReport.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <mutex>
#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor,
		private juce::Timer
	{
	public:
		struct EphemeralConfig final
		{
			// Tests may explicitly isolate the emulated machine from persistent
			// factory/storage caches. A disengaged value preserves normal discovery;
			// an engaged empty value disables the device home path entirely.
			std::optional<std::string> deviceHomePath;
		};

	    AudioPluginAudioProcessor();
		explicit AudioPluginAudioProcessor(md::MachineModel _model);
		AudioPluginAudioProcessor(md::MachineModel _model, bool _allowMcpServer);
		AudioPluginAudioProcessor(md::MachineModel _model, EphemeralConfig,
			bool _allowMcpServer = false);
		AudioPluginAudioProcessor(md::MachineModel _model,
			std::vector<uint8_t> _initialPatchRam, bool _allowMcpServer = true);
	    ~AudioPluginAudioProcessor() override;

		md::MachineModel getModel() const { return m_model; }
		static md::MachineModel getCompiledProductModel();
		static bool hasEmbeddedProductResource(std::string_view _filename);
		juce::File getInstalledFactoryStorageImage() const;
		juce::File getStorageRecoveryImage() const;
		bool loadStorageImage(const juce::File& _source, juce::String& _result);
		bool serviceFactoryInitialization();
		bool serviceProjectStateRestore();
		std::string getProjectStateRestoreError();
		void setPerformanceDiagnosticsEnabled(bool _enabled);
		bool performanceDiagnosticsActive() const;
		std::string performanceDiagnosticsStatus() const;
		// The capture's state for the editor's own text; nullopt without a report
		std::optional<synthLib::PerformanceReport::Status> performanceDiagnosticsState() const;
		bool performanceDiagnosticsFolderError() const { return m_performanceFolderError; }
		juce::File performanceDiagnosticsFolder() const;
		juce::File performanceDiagnosticsFile() const { return m_performanceReportFile; }
		void setRamRecordingMode(md::RamRecordingMode _mode);
		md::RamRecordingMode getRamRecordingMode() const
		{
			return static_cast<md::RamRecordingMode>(
				m_ramRecordingMode.load(std::memory_order_relaxed));
		}
		bool isRamRecordingModeAvailable();

		// Parallel transport setting (Machinedrum only; on by default). The
		// config value is the source of truth; apply pushes it to the device.
		static constexpr const char* ParallelTransportConfigKey = "parallelTransport";
		bool supportsParallelTransport() const { return true; }
		bool getParallelTransportSetting();
		void applyParallelTransportSetting();
		// The device follows the setting now, or will after a reload.
		bool isParallelTransportActive();
		// Default plug-in latency for a new configuration: two blocks let the
		// machine render ahead on its own threads (see md::AsyncRender).
		static constexpr int DefaultLatencyBlocks = 2;

		// The device without its lock, for the editor, the controller and the timers here
		LiveDevice& getLiveDevice() { return m_liveDevice; }

		// "Follow host tempo" (off by default): the machine takes its tempo and
		// start/stop from the host's MIDI clock (see md::HostSync). On, it is kept
		// that way; turned off by the user, it gets its factory setting back; off at
		// startup, the machine's own setting is left alone.
		static constexpr const char* FollowHostTempoConfigKey = "followHostTempo";
		bool getFollowHostTempoSetting();
		void applyFollowHostTempoSetting(bool _changedByUser);
		md::HostSync::State getHostSyncState() const { return m_hostSyncControl->getState(); }

		// Levels of the three output buses after each block, for the editor's meters
		OutputMeters& getOutputMeters() { return m_outputMeters; }

		// The project's pattern chain, played while the machine follows the host
		ChainControl& getChainControl() { return m_chainControl; }

		// The editor's Monomachine pattern writes, which the Device drives through SYSEX RECV
		md::MmPatternWriteControl& getMmPatternWriteControl() { return *m_mmPatternWriteControl; }
		using jucePluginEditorLib::Processor::processBlock;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midiMessages) override;

	    jucePluginEditorLib::PluginEditorState* createEditorState() override;
	    synthLib::Device* createDevice() override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

	    pluginLib::Controller* createController() override;
		void saveChunkData(baseLib::BinaryStream& _stream) override;
		void loadChunkData(baseLib::ChunkReader& _reader) override;
		bool loadCustomData(const std::vector<uint8_t>& _sourceBuffer) override;

	private:
		static BusesProperties createBusesProperties();
		bool isBusesLayoutSupported(const BusesLayout& _layout) const override;
		AudioPluginAudioProcessor(md::MachineModel _model,
			std::vector<uint8_t> _initialPatchRam, bool _allowMcpServer,
			bool _ephemeralConfig,
			std::optional<std::string> _deviceHomePath = std::nullopt);
		bool serviceDeferredStateRestore();
		bool serviceStateRestoreFailure();
		void recordStandaloneStartupDiagnostics();
		void reportProjectStateRestoreFailure(const std::string& _error);
		// Hands the chain to the device as the host sync allows, and reads the lengths it lacks
		void serviceChain();
		void timerCallback() override;

		std::unique_ptr<synthLib::PerformanceReport> m_performanceReport;
		juce::File m_performanceReportFile;
		bool m_performanceFolderError = false;
		const md::MachineModel m_model;
		const std::vector<uint8_t> m_initialPatchRam;
		const std::optional<std::string> m_deviceHomePath;
		std::mutex m_storageLoadMutex;
		uint64_t m_reportedRestoreFailureGeneration = 0;
		juce::File m_startupDiagnosticsFile;
		double m_startupDiagnosticsStartMilliseconds = 0.0;
		bool m_startupDiagnosticsEnabled = false;
		std::atomic<uint8_t> m_ramRecordingMode{
			static_cast<uint8_t>(md::RamRecordingMode::Original)};
		bool m_ramRecordingModeChunkSeen = false;
		const std::shared_ptr<md::HostSyncControl> m_hostSyncControl = std::make_shared<md::HostSyncControl>();
		const std::shared_ptr<md::MmPatternWriteControl> m_mmPatternWriteControl = std::make_shared<md::MmPatternWriteControl>();
		LiveDevice m_liveDevice{*this};
		OutputMeters m_outputMeters;
		ChainControl m_chainControl{m_model};
		std::optional<uint8_t> m_chainLengthAsked;	// the pattern whose dump was asked for, and when
		double m_chainLengthAskedAt = 0.0;
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
