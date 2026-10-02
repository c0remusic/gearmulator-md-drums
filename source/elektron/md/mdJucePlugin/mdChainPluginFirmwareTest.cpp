// The project's pattern chain through the whole plug-in, against the real Machinedrum and Monomachine: the
// host plays, synthLib::MidiClock turns its transport into MIDI clock, the Device runs the ChainPlayer the
// processor gave it, and the machine's CURRENT PATTERN follows the chain A01, A02, A01. The machine follows
// the host (HostSync); the chain's lengths come from the pattern dumps the controller reads.
//
// The status is asked for by the host on every step and comes back through the host's MIDI output. The
// machine answers the next pattern from its commit on, 7 clock ticks before the end of the playing one on
// the Machinedrum and 1 on the Monomachine (patternChainFirmwareTest, mmPatternChainFirmwareTest): each
// change must show between that tick and the first step of the next pattern.

#include "mdAutomationTestSupport.h"
#include "mdChainControl.h"

#include <iostream>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	using namespace mdAutomationTest;
	using Message = md::automation::sysex::Message;
	using Status = md::automation::sysex::StatusParameter;
	using Source = synthLib::MidiEventSource;
	constexpr double Bpm = 150.0;
	constexpr double SampleRate = 48000.0;

	class MovingPlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo result;
			result.setIsPlaying(playing);
			result.setIsRecording(false);
			result.setBpm(Bpm);
			result.setPpqPosition(ppq);
			return result;
		}

		bool playing = false;
		double ppq = 0.0;
	};

	class ChainHarness
	{
	public:
		explicit ChainHarness(const md::MachineModel _model) : product(_model)
		{
			product.audioProcessor.setPlayHead(&head);
		}

		// Blocks of the host, the play head moving while it plays; the SysEx the plug-in sends the
		// host is kept with the position it came at
		void process(const int _blocks)
		{
			for(int block = 0; block < _blocks; ++block)
			{
				audio.clear();
				product.audioProcessor.processBlock(audio, midi);
				for(const auto metadata : midi)
				{
					const auto message = metadata.getMessage();
					if(message.isSysEx())
						replies.emplace_back(head.ppq, Message(message.getRawData(), message.getRawData() + message.getRawDataSize()));
				}
				midi.clear();
				if(head.playing)
					head.ppq += BlockSize / SampleRate * Bpm / 60.0;
			}
		}

		void send(const Message& _message)
		{
			midi.addEvent(juce::MidiMessage(_message.data(), static_cast<int>(_message.size())), 0);
		}

		template<typename Done>
		bool processUntil(const Done& _done, const int _maximumBlocks)
		{
			for(int block = 0; block < _maximumBlocks; ++block)
			{
				if(_done())
					return true;
				process(1);
			}
			return _done();
		}

		Harness product;
		MovingPlayHead head;
		juce::AudioBuffer<float> audio{2, BlockSize};
		juce::MidiBuffer midi;
		std::vector<std::pair<double, Message>> replies;
	};

	// False without the model's firmware
	bool run(const md::MachineModel _model)
	{
		const std::string name = modelName(_model);
		ChainHarness h(_model);
		if(!h.product.hasLocalFirmware())
		{
			(void)allowMissingFirmware("mdChainPluginFirmwareTest", _model);
			return false;
		}
		h.product.prepare(SampleRate);
		require(h.product.synchronize(), name + ": initial firmware synchronization failed");
		auto& processor = h.product.processor;
		auto& chain = processor.getChainControl();
		processor.getMidiRoutingMatrix().setEnabled(Source::Device, Source::Host,
			synthLib::MidiRoutingMatrix::EventType::SysEx, true);

		// The machine follows the host. The host sync waits for the factory initialisation (Machinedrum),
		// which the processor's timer finishes with one reboot; the device learns the active Global slot
		// from a status answer, which the controller's timer asks for. Both by hand here, once a second.
		processor.getConfig().setValue(mdJucePlugin::AudioPluginAudioProcessor::FollowHostTempoConfigKey, true);
		processor.applyFollowHostTempoSetting(true);
		const int second = static_cast<int>(SampleRate / BlockSize);
		bool following = false;
		bool rebooted = false;
		for(int seconds = 0; seconds < 90 && !following; ++seconds)
		{
			if(!rebooted)
				rebooted = processor.serviceFactoryInitialization();
			h.send(md::automation::sysex::statusRequest(_model, Status::Global));
			following = h.processUntil([&] { return processor.getHostSyncState() == md::HostSync::State::Following; }, second);
		}
		require(following, name + ": the machine does not follow the host, host sync state "
			+ std::to_string(static_cast<int>(processor.getHostSyncState())));
		std::cout << name << " follows the host\n";

		// The lengths of A01 and A02, from their dumps
		auto& controller = h.product.controller;
		require(controller.requestPatternDump(0) && controller.requestPatternDump(1), name + ": pattern dumps not asked for");
		require(h.processUntil([&] { return chain.getLength(0) && chain.getLength(1); }, 20 * second),
			name + ": the lengths of A01 and A02 never came");
		const int lengthA01 = *chain.getLength(0);
		const int lengthA02 = *chain.getLength(1);
		std::cout << name << " A01 " << lengthA01 << " steps, A02 " << lengthA02 << " steps\n";

		// The chain A01, A02, handed to the device's player
		require(chain.setEntries({{0, 1}, {1, 1}}), "chain refused");
		chain.setEnabled(true);
		require(chain.update(processor.getHostSyncState() == md::HostSync::State::Following)
			== mdJucePlugin::ChainControl::State::Playing, name + ": the chain does not play");

		// The host plays two rounds and a little; the status asked for on every step
		const double round = (lengthA01 + lengthA02) / 4.0;
		h.replies.clear();
		h.head.playing = true;
		h.head.ppq = 0.0;
		double nextAsk = 0.0;
		while(h.head.ppq < 2 * round + 1.0)
		{
			if(h.head.ppq * 4.0 >= nextAsk)
			{
				h.send(md::automation::sysex::statusRequest(_model, Status::Pattern));
				nextAsk += 1.0;
			}
			h.process(1);
		}
		h.head.playing = false;
		h.process(second);
		std::vector<std::pair<double, int>> statuses;
		for(const auto& [ppq, reply] : h.replies)
		{
			const auto status = md::automation::sysex::parseStatusResponse(_model, reply);
			if(status && status->parameter == Status::Pattern)
				statuses.emplace_back(ppq, status->value);
		}
		require(statuses.size() > static_cast<size_t>(lengthA01 + lengthA02), name + ": too few status answers");

		// Each change of the status: to the next pattern of the chain, between the commit and the first
		// step after the end, at every end of a pattern and nowhere else
		std::vector<std::pair<double, int>> changes;
		for(size_t index = 1; index < statuses.size(); ++index)
		{
			if(statuses[index].second != statuses[index - 1].second)
				changes.push_back(statuses[index]);
		}
		std::cout << name << " first status " << statuses.front().second << " at ppq " << statuses.front().first << "; changes:";
		for(const auto& [ppq, pattern] : changes)
			std::cout << " A0" << pattern + 1 << " at ppq " << ppq;
		std::cout << '\n';
		require(statuses.front().second == 0, name + ": the machine did not start on A01");
		const double commit = md::PatternChain::commitTicks(_model) / 24.0;
		const std::vector<double> ends{lengthA01 / 4.0, round, round + lengthA01 / 4.0, 2 * round};
		size_t expected = 0;
		for(const auto& [ppq, pattern] : changes)
		{
			require(expected < ends.size(), name + ": a change of pattern where the chain has none");
			const double end = ends[expected];
			const int next = expected % 2 == 0 ? 1 : 0;
			// The status answers from the commit, and the answers come back a little after they are asked
			require(pattern == next && ppq >= end - commit && ppq < end + 0.5,
				name + ": a change of pattern not at the end of the chain's pattern");
			++expected;
		}
		require(expected >= ends.size() - 1, name + ": the machine did not follow the chain");
		std::cout << name << " PASS\n";
		return true;
	}
}

int main()
{
	juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		const bool machinedrum = run(md::MachineModel::Machinedrum);
		const bool monomachine = run(md::MachineModel::Monomachine);
		if(!machinedrum && !monomachine)
			return SkipReturnCode;
		std::cout << "mdChainPluginFirmwareTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdChainPluginFirmwareTest: FAIL " << _error.what() << '\n';
		return 1;
	}
}
