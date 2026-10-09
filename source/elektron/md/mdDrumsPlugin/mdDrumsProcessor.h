#pragma once

#include "mdDrumsBank.h"
#include "mdDrumsTelemetry.h"

#include "MachineRunner.h"
#include "TrackFx.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include <atomic>
#include <memory>
#include <vector>

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
		void saveChunkData(baseLib::BinaryStream& _s) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		// Where the flash image was looked for, for the dialog when it is missing
		std::vector<std::string> romFolders() const;

		// What the editor shows of the sound, which every Device this Processor creates writes (ticket 16)
		Telemetry& getTelemetry() const { return *m_telemetry; }
		// The OS's tables the Device's Tracks' effects read, for the Filter and EQ screen; none without a Device
		std::shared_ptr<const md::engine::TrackFx::Tables> getFxTables() const { return std::atomic_load(&m_fxTables); }
		// The OS's machines (their SYN1-8 defaults), for the Hit screen's preview; none without a Device
		using Machines = std::vector<md::engine::MachineInfo>;
		std::shared_ptr<const Machines> getMachines() const { return std::atomic_load(&m_machines); }
		// The host's tempo as last sent to the Device; 0 before the host gives one (the engine then runs at 125)
		float getBpm() const { return m_bpm.load(std::memory_order_relaxed); }
		// The Kits' Bank (ticket 10 of the editor map), "Bank.syx" in the plug-in's data folder, made with the Device; none
		// without one. A test Processor's is a file of its own in the temporary folder, removed with it.
		Bank* getBank() const { return m_bank.get(); }

	private:
		// Main, then the 16 Outs, off until the host enables them
		static BusesProperties buses();
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;

		std::shared_ptr<Telemetry> m_telemetry = std::make_shared<Telemetry>();
		std::shared_ptr<const md::engine::TrackFx::Tables> m_fxTables;
		std::shared_ptr<const Machines> m_machines;
		std::atomic<float> m_bpm{0.0f};
		bool m_forTests = false;
		std::unique_ptr<Bank> m_bank;
	};
}
