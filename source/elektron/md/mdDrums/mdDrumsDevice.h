#pragma once

#include "mdDrumsEngine.h"
#include "mdDrumsMessages.h"
#include "mdDrumsTelemetry.h"

#include "synthLib/device.h"

#include <array>
#include <memory>

namespace mdDrums
{
	// MD Drums' synthLib::Device: a Machinedrum seen through its MIDI (ADR 0002), the engines without the OS behind it.
	// It takes notes 36-51 on any channel from anyone, and from the editor (source Editor) the Machinedrum's CCs on
	// channels 1 to 4 (24 parameters, level, mute), ASSIGN MACHINE ($5B), the master effects ($5D-$60), SET LFO PARAM
	// ($62), SET TRIG GROUP and SET MUTE GROUP ($65, $66) and Kit dumps ($52), plus MD Drums' own messages (Solo, Out,
	// tempo, mixer). What the host sends besides notes is ignored. It renders synchronously, cutting its block at each
	// event's offset, and holds everything the sound depends on: its state is its Kit and its mixer.
	class Device final : public synthLib::Device
	{
	public:
		static constexpr int FirstNote = 36;
		static constexpr int OutputCount = Engine::OutputCount;	// Main left and right, then Out 01 to 16
		// Every Hit sounds 31 samples after its note (Engine::SampleAccurateDelay), then its voice starts: 4 samples for
		// most machines (TRX-SD, -CH, EFM, E12, P-I; 12 for ROM, 36 for TRX-BD, which sounds a block later; mdDrumsEngineTest)
		static constexpr uint32_t InternalLatency = Engine::SampleAccurateDelay + 4;

		// _params.romData: the 8 MB Machinedrum UW flash image with OS 1.63. Throws synthLib::DeviceException without
		// a usable one. _telemetry takes its outputs' peaks and its Hits (none without it); the Device shares it, so that it
		// stays valid while the Device lives, whoever goes first.
		explicit Device(const synthLib::DeviceCreateParams& _params, std::shared_ptr<Telemetry> _telemetry = {});
		~Device() override;

		float getSamplerate() const override { return static_cast<float>(Engine::SampleRate); }
		bool isValid() const override { return m_valid; }
#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return OutputCount; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 0; }

		uint32_t getDefaultLatencyBlocks() const override { return 0; }
		uint32_t getInternalLatencyMidiToOutput() const override { return InternalLatency; }

		void process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size,
			const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut) override;

		const md::automation::sysex::MdKit& kit() const { return m_kit; }
		const messages::Mixer& mixer() const { return m_mixer; }
		// The OS's tables the Tracks' effects read (Engine::fxTables), for the editor's Filter and EQ screen
		std::shared_ptr<const md::engine::TrackFx::Tables> fxTables() const { return m_engine ? m_engine->fxTables() : nullptr; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		void apply(const synthLib::SMidiEvent& _event);
		void applyControlChange(const synthLib::SMidiEvent& _event);
		void applySysex(const synthLib::SMidiEvent& _event);
		void applyKit();
		void applyMachine(uint8_t _track, uint8_t _machine);
		void reportMachines();
		void applyMixer();
		void render(const synthLib::TAudioOutputs& _outputs, size_t _offset, size_t _count);
		void capture(const std::array<float*, Engine::OutputCount>& _outputs, size_t _count);
		void queueOut(const synthLib::SMidiEvent& _event);

		std::unique_ptr<Engine> m_engine;
		std::shared_ptr<Telemetry> m_telemetry;
		bool m_valid = false;

		// The Hit captures being written (Telemetry::beginCapture): from the sample a Hit sounds, a column of 8 samples at
		// a time, until the capture is full or the Track's next Hit starts another
		struct Capture
		{
			bool active = false;
			int64_t start = 0;	// the sample the Hit sounds
			int filled = 0;		// samples in the column being made
			float low = 0.0f, high = 0.0f;
		};
		std::array<Capture, Engine::TrackCount> m_captures{};
		int64_t m_position = 0;	// samples rendered so far
		md::automation::sysex::MdKit m_kit;
		messages::Mixer m_mixer;

		// What the Device says back: a new machine's SYN1-8 as CCs, for the editor, once the block is done (the values
		// the block left, should the editor have set some after the machine)
		static constexpr size_t OutCapacity = 16 * 8;
		std::array<synthLib::SMidiEvent, OutCapacity> m_out{};
		size_t m_outCount = 0;
		uint16_t m_machinesToReport = 0;
	};
}
