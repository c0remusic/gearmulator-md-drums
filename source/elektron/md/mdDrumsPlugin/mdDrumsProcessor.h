#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "mdDrumsEngine.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace mdDrums
{
	// MD Drums: the Machinedrum's 16 tracks as an instrument. Host notes 36-51 trigger tracks 1-16 (a Drum Rack's
	// pads), with velocity; every track's machine, 24 parameters, level and mute are host parameters. The output is
	// the dry main mix (no master effects yet), resampled when the host does not run at 44.1 kHz.
	class Processor : public juce::AudioProcessor
	{
	public:
		static constexpr int FirstNote = 36;

		Processor();
		~Processor() override;

		bool hasEngine() const { return m_engine != nullptr; }
		const Engine* engine() const { return m_engine.get(); }

		void prepareToPlay(double _sampleRate, int _maximumBlockSize) override;
		void releaseResources() override {}
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi) override;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }

		const juce::String getName() const override { return "MD Drums"; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return false; }
		bool isMidiEffect() const override { return false; }
		double getTailLengthSeconds() const override { return 0.0; }

		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}

		void getStateInformation(juce::MemoryBlock& _destData) override;
		void setStateInformation(const void* _data, int _sizeInBytes) override;

	private:
		struct Track
		{
			std::atomic<float>* machine = nullptr;
			std::array<std::atomic<float>*, Engine::ParamCount> params{};
			std::atomic<float>* level = nullptr;
			std::atomic<float>* mute = nullptr;

			// What the engine was last given, -1 for nothing yet.
			int appliedMachine = -1;
			std::array<int, Engine::ParamCount> appliedParams{};
			int appliedLevel = -1;
			int appliedMute = -1;
		};

		juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
		void applyParameters();
		void render(float* _left, float* _right, int _count);

		std::unique_ptr<Engine> m_engine;
		juce::AudioProcessorValueTreeState m_state;
		std::array<Track, Engine::TrackCount> m_tracks;

		// Host rate != 44.1 kHz: the engine renders ahead into these, and the interpolators read them.
		double m_ratio = 1.0;	// engine samples per host sample
		std::array<juce::LagrangeInterpolator, 2> m_interpolators;
		std::array<std::vector<float>, 2> m_pending;
		size_t m_pendingCount = 0;
	};
}
