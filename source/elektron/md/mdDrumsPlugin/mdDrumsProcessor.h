#pragma once

#include "mdDrumsTelemetry.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include <memory>

namespace mdDrums
{
	// MD Drums on the TUS plug-in stack (ADR 0001): a jucePluginEditorLib::Processor over mdDrums::Device, which speaks
	// the Machinedrum's MIDI (ADR 0002), with a Controller from parameterDescriptions_mddrums.json and an RmlUi skin.
	// Host notes 36-51 trigger Tracks 1-16; the outputs are Main, then "Out 01" to "Out 16", each optional, a Track's
	// Out parameter deciding whether it leaves Main whatever the host does with the bus.
	class Processor final : public jucePluginEditorLib::Processor
	{
	public:
		// _forTests: a configuration that is neither read nor saved, and no MCP server (which Windows would ask the
		// user's firewall about for every new test executable)
		explicit Processor(bool _forTests = false);
		~Processor() override;

		jucePluginEditorLib::PluginEditorState* createEditorState() override;
		synthLib::Device* createDevice() override;
		pluginLib::Controller* createController() override;

		// The host's tempo, sent to the Device as MD Drums' own message when it changes
		void processBpm(float _bpm) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		// Where the flash image was looked for, for the dialog when it is missing
		std::vector<std::string> romFolders() const;

		// What the editor shows of the sound, which every Device this Processor creates writes (ticket 16)
		Telemetry& getTelemetry() const { return *m_telemetry; }

	private:
		// Main, then the 16 Outs, off until the host enables them
		static BusesProperties buses();
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;

		std::shared_ptr<Telemetry> m_telemetry = std::make_shared<Telemetry>();
		float m_bpm = 0.0f;
	};
}
