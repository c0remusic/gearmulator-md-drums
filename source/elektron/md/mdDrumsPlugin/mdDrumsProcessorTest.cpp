// MD Drums' processor as a host drives it, at 44.1 and 48 kHz: a note 36-51 sounds its track within two engine
// blocks of its sample position, other notes do nothing, the machine parameter changes the sound, and the state
// round-trips.

#include "mdDrumsProcessor.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Result
	{
		int first = -1;		// host samples from the note to the first one at or above -60 dBFS
		float peak = 0;		// main output
		double energy = 0;
		float ownPeak = 0;	// the third channel, track 1's own output when the host enables it
	};

	// A second of silence, then _note at sample _offset of a 512-sample block, and a second after it.
	Result play(mdDrums::Processor& _processor, const int _note, const int _offset, const double _rate)
	{
		constexpr int block = 512;
		juce::AudioBuffer<float> buffer(_processor.getTotalNumOutputChannels(), block);
		juce::MidiBuffer midi;
		const auto blocks = static_cast<int>(_rate / block);
		for(int i = 0; i < blocks; ++i)
		{
			midi.clear();
			_processor.processBlock(buffer, midi);
		}
		Result result;
		int position = 0;
		for(int i = 0; i < blocks; ++i)
		{
			midi.clear();
			if(i == 0)
				midi.addEvent(juce::MidiMessage::noteOn(1, _note, static_cast<juce::uint8>(100)), _offset);
			_processor.processBlock(buffer, midi);
			for(int s = 0; s < block; ++s)
			{
				const auto value = std::max(std::abs(buffer.getSample(0, s)), std::abs(buffer.getSample(1, s)));
				if(result.first < 0 && value >= 0.001f)
					result.first = position + s - _offset;
				result.peak = std::max(result.peak, value);
				result.energy += double(value) * value;
				if(buffer.getNumChannels() > 2)
					result.ownPeak = std::max(result.ownPeak, std::abs(buffer.getSample(2, s)));
			}
			position += block;
		}
		return result;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		for(const double rate : {44100.0, 48000.0})
		{
			mdDrums::Processor processor;
			if(!processor.hasEngine())
			{
				std::cout << "mdDrumsProcessorTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
				return 77;
			}
			processor.setRateAndBufferSizeDetails(rate, 512);
			processor.prepareToPlay(rate, 512);

			const auto kick = play(processor, 36, 200, rate);
			std::printf("%.0f Hz: note 36 at sample 200: first audible %d samples later, peak %.4f\n", rate,
				kick.first, kick.peak);
			require(kick.first >= 0 && kick.first < 2 * 32 * rate / 44100.0 + 8, "note 36 took more than two blocks");
			require(kick.peak > 0.01f, "note 36 is too quiet");

			const auto none = play(processor, 60, 200, rate);
			require(none.peak == 0.0f, "a note outside 36-51 sounded");

			// Track 1 to EFM-BD (id 32): the same note sounds otherwise.
			auto* machine = dynamic_cast<juce::AudioParameterInt*>(processor.getParameters()[0]);
			require(machine != nullptr, "track 1's machine is not an integer parameter");
			require(machine->getName(64) == "T1 Machine", "track 1's machine is not the first parameter");
			const auto before = play(processor, 36, 0, rate);
			*machine = 32;
			const auto after = play(processor, 36, 0, rate);
			std::printf("%.0f Hz: track 1 TRX-BD energy %.3f, EFM-BD energy %.3f (%s)\n", rate, before.energy,
				after.energy, machine->getCurrentValueAsText().toRawUTF8());
			require(std::abs(after.energy - before.energy) > before.energy * 0.05, "the machine parameter did nothing");

			// SYN1 shows its name on the track's machine.
			const auto syn1 = processor.getParameters()[1]->getCurrentValueAsText();
			std::printf("%.0f Hz: T1 SYN1 on EFM-BD reads \"%s\"\n", rate, syn1.toRawUTF8());
			require(syn1.containsAnyOf("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), "SYN1 does not show its name");

			// Track 1's own output: the host enables it, track 1 leaves the main mix and plays there.
			{
				auto layout = processor.getBusesLayout();
				layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
				require(processor.setBusesLayout(layout), "the host cannot enable track 1's output");
				processor.prepareToPlay(rate, 512);
				const auto separated = play(processor, 36, 0, rate);
				std::printf("%.0f Hz: track 1 on its own output: main peak %.4f, own output peak %.4f\n", rate,
					separated.peak, separated.ownPeak);
				require(separated.peak == 0.0f, "track 1 still plays in the main mix");
				require(separated.ownPeak > 0.01f, "track 1 is silent on its own output");
				layout.outputBuses.getReference(1) = juce::AudioChannelSet::disabled();
				require(processor.setBusesLayout(layout), "the host cannot disable track 1's output");
				processor.prepareToPlay(rate, 512);
			}

			juce::MemoryBlock state;
			processor.getStateInformation(state);
			mdDrums::Processor restored;
			restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			const auto* restoredMachine = dynamic_cast<juce::AudioParameterInt*>(restored.getParameters()[0]);
			require(restoredMachine && restoredMachine->get() == 32, "the state lost track 1's machine");
		}
		std::cout << "mdDrumsProcessorTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsProcessorTest: " << _error.what() << '\n';
		return 1;
	}
}
