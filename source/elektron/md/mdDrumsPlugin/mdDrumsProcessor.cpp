#include "mdDrumsProcessor.h"

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"

#include "mdDrumsDevice.h"
#include "mdDrumsMessages.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"
#include "jucePluginLib/processorPropertiesInit.h"

#include "baseLib/binarystream.h"
#include "synthLib/deviceException.h"
#include "synthLib/os.h"

#include <cmath>

namespace mdDrums
{
	namespace
	{
		constexpr const char* g_flashName = "elektron_sps1-1uw_os1.63.bin";

		juce::PropertiesFile::Options configOptions(const bool _forTests)
		{
			juce::PropertiesFile::Options options;
			options.applicationName = _forTests ? juce::String("MDDrumsTest_") + juce::Uuid().toString()
				: juce::String("MDDrums");
			options.filenameSuffix = ".settings";
			options.folderName = options.applicationName;
			options.osxLibrarySubFolder = "Application Support/" + options.applicationName;
			options.doNotSave = _forTests;
			if(_forTests)
				options.millisecondsBeforeSaving = -1;
			return options;
		}

		pluginLib::Processor::Properties properties()
		{
			const auto compiled = pluginLib::initProcessorProperties();
			// Bus 0 is Main (device channels 0 and 1), bus n is Out n (channel n + 1): the host may enable any of them
			std::vector<size_t> outputs{0};
			for(size_t bus = 1; bus < Device::OutputCount - 1; ++bus)
				outputs.push_back(bus + 1);
			return {compiled.name, compiled.vendor, compiled.isSynth, compiled.wantsMidiInput, compiled.producesMidiOut,
				compiled.isMidiEffect, compiled.plugin4CC, compiled.lv2Uri, compiled.binaryData, {}, {}, outputs};
		}
	}

	juce::AudioProcessor::BusesProperties Processor::buses()
	{
		auto properties = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
		for(int track = 1; track <= Device::OutputCount - 2; ++track)
			properties = properties.withOutput(juce::String::formatted("Out %02d", track), juce::AudioChannelSet::mono(),
				false);
		return properties;
	}

	Processor::Processor(const bool _forTests)
		: jucePluginEditorLib::Processor(buses(), configOptions(_forTests), properties(), !_forTests,
			_forTests ? ConfigMode::Ephemeral : ConfigMode::Persistent)
	{
		getController();
	}

	Processor::~Processor()
	{
		destroyEditorState();
	}

	jucePluginEditorLib::PluginEditorState* Processor::createEditorState()
	{
		return new EditorState(*this);
	}

	std::vector<std::string> Processor::romFolders() const
	{
		std::vector<std::string> folders{getPublicRomFolder(), synthLib::getModulePath()};
		return folders;
	}

	synthLib::Device* Processor::createDevice()
	{
		std::vector<std::filesystem::path> folders;
		for(const auto& folder : romFolders())
			folders.emplace_back(folder);
		auto flash = Engine::findFlashImage(folders);
		if(flash.empty())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
				std::string("MD Drums needs the Machinedrum UW's 8 MB flash image with OS 1.63, ") + g_flashName + ".");

		synthLib::DeviceCreateParams params;
		params.romName = g_flashName;
		params.romData = std::move(flash);
		params.homePath = getDataFolder();
		return new Device(params);
	}

	pluginLib::Controller* Processor::createController()
	{
		return new Controller(*this);
	}

	void Processor::processBpm(const float _bpm)
	{
		if(std::abs(_bpm - m_bpm) < 0.005f)
			return;
		const auto message = messages::tempo(_bpm);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.assignRawData(message.bytes.data(), message.size, synthLib::MidiEventSource::Editor, 0);
		if(tryAddRealtimeMidiEvent(event))
			m_bpm = _bpm;
	}

	void Processor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		// Before the stack's own "MIDI" reader (the first one registered wins): the Device's state, also handed to the
		// Controller, which then follows it without touching the Device
		_cr.add("MIDI", 1, [this](baseLib::BinaryStream& _stream, uint32_t)
		{
			std::vector<uint8_t> state;
			_stream.read(state);
			getPlugin().setState(state);
			dynamic_cast<Controller&>(getController()).setLoadedState(std::move(state));
		});
		jucePluginEditorLib::Processor::loadChunkData(_cr);
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layouts) const
	{
		if(_layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() || !_layouts.inputBuses.isEmpty())
			return false;
		for(int bus = 1; bus < _layouts.outputBuses.size(); ++bus)
		{
			const auto& set = _layouts.outputBuses.getReference(bus);
			if(!set.isDisabled() && set != juce::AudioChannelSet::mono())
				return false;
		}
		return true;
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new mdDrums::Processor();
}
