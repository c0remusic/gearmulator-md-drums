#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// MD Drums driven as a host drives it, for the tests that do: mdDrumsProcessorTest in process, mdDrumsVst3HostTest
// through JUCE's VST3 hosting. Notes played in blocks, a parameter automated, a state restored, and what the plug-in
// tells the host by itself, which each adapter counts as its channel reports it; then the scenarios of ticket 34 that
// both must keep. Header-only on juce::AudioProcessor: the two executables build JUCE differently, and the VST3 test
// does not link the plug-in.
namespace mdDrums::hostDriver
{
	inline void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	struct Note
	{
		int64_t sample;
		int number;
		int velocity;
		int channel = 1;
		bool off = false;
	};

	// The plug-in played as a host's audio callback plays it, in blocks of _block or, with a seed, of random sizes up to
	// it; every channel the host enabled, the whole run. _blockMs takes each process call's time, _beforeBlock runs before
	// each one with its first sample, and _inProcess is true during each one.
	inline juce::AudioBuffer<float> render(juce::AudioProcessor& _plugin, const std::vector<Note>& _notes,
		const int64_t _samples, const int _block, uint32_t _random = 0, std::vector<double>* _blockMs = nullptr,
		const std::function<void(int64_t)>& _beforeBlock = {}, bool* _inProcess = nullptr)
	{
		using Clock = std::chrono::steady_clock;
		const auto channels = _plugin.getTotalNumOutputChannels();
		juce::AudioBuffer<float> take(channels, static_cast<int>(_samples));
		take.clear();
		juce::AudioBuffer<float> buffer(channels, _block);
		juce::MidiBuffer midi;
		size_t next = 0;
		for(int64_t position = 0; position < _samples;)
		{
			auto size = _block;
			if(_random)
			{
				_random ^= _random << 13;
				_random ^= _random >> 17;
				_random ^= _random << 5;
				size = 1 + static_cast<int>(_random % static_cast<uint32_t>(_block));
			}
			size = static_cast<int>(std::min<int64_t>(size, _samples - position));
			buffer.setSize(channels, size, false, false, true);
			buffer.clear();
			midi.clear();
			for(; next < _notes.size() && _notes[next].sample < position + size; ++next)
			{
				const auto& note = _notes[next];
				midi.addEvent(note.off ? juce::MidiMessage::noteOff(note.channel, note.number)
					: juce::MidiMessage::noteOn(note.channel, note.number, static_cast<juce::uint8>(note.velocity)),
					static_cast<int>(note.sample - position));
			}
			if(_beforeBlock)
				_beforeBlock(position);
			const auto start = Clock::now();
			if(_inProcess)
				*_inProcess = true;
			_plugin.processBlock(buffer, midi);
			if(_inProcess)
				*_inProcess = false;
			if(_blockMs)
				_blockMs->push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
			for(int channel = 0; channel < channels; ++channel)
				take.copyFrom(channel, static_cast<int>(position), buffer, channel, 0, size);
			position += size;
		}
		return take;
	}

	// A host of the plug-in under test, as each test reaches it
	class Host
	{
	public:
		static constexpr int Relays = 576;	// the first relay's index: the relays come after the Tracks and the master

		explicit Host(const int _block) : m_block(_block) {}
		virtual ~Host() = default;

		virtual juce::AudioProcessor& plugin() = 0;
		// A parameter's index by its JSON name and part (0 for the master effects and the relays)
		virtual int index(const std::string& _name, int _part = 0) = 0;
		// The message thread's turn after blocks: what the Controller's timer does with what they brought
		virtual void tick() = 0;
		// What the plug-in told the host by itself since clear: the parameters told as edits (performEdit, which Live
		// records while it records), and the re-reads of every value (restartComponent with kParamValuesChanged)
		virtual std::vector<int> edits() = 0;
		virtual int rereads() = 0;
		virtual void clear() = 0;

		int block() const { return m_block; }
		juce::AudioBuffer<float> render(const std::vector<Note>& _notes, const int64_t _samples)
		{
			return hostDriver::render(plugin(), _notes, _samples, m_block);
		}

		// A host automating: the value set, normalised, then blocks for the plug-in to take it and a tick
		void automate(const int _index, const float _value)
		{
			plugin().getParameters()[_index]->setValue(_value);
			render({}, m_block * 4);
			tick();
		}

		// A host restoring a state: the state set, then the ticks and blocks that follow it
		void restore(const juce::MemoryBlock& _state)
		{
			plugin().setStateInformation(_state.getData(), static_cast<int>(_state.getSize()));
			tick();
			render({}, m_block * 8);
			tick();
		}

		// The edits told for the Tracks and the master, the relays left out
		size_t editsWithoutRelays()
		{
			const auto all = edits();
			return static_cast<size_t>(std::count_if(all.begin(), all.end(), [](const int _i) { return _i < Relays; }));
		}

	private:
		const int m_block;
	};

	// The scenarios of ticket 34 (measured in Live 12.2.1), which both channels must keep:

	// A Track's machine set by the host: the parameters the Machinedrum gives it reach the host as one re-read, no edit
	inline void machineChange(Host& _host, const int _part, const int _machine)
	{
		_host.clear();
		_host.automate(_host.index("Machine", _part), static_cast<float>(_machine) / 191.0f);
		require(_host.edits().empty() && _host.rereads() > 0, "a machine set by the host did not reach it as one re-read,"
			" but as " + std::to_string(_host.edits().size()) + " edits and " + std::to_string(_host.rereads()) + " re-reads");
	}

	// The relay Track set by the host (Push 2's first encoder): another Track shown, its relays told as edits, nothing
	// else, no re-read
	inline void relayTrack(Host& _host, const int _part)
	{
		_host.clear();
		_host.automate(_host.index("FocusTrack"), static_cast<float>(_part) / 15.0f);
		require(!_host.edits().empty() && _host.editsWithoutRelays() == 0 && _host.rereads() == 0,
			"showing another Track did not give the host its relays as edits alone");
	}

	// A parameter of the Track shown automated: the relay that follows it is not told, nor is anything else
	inline void shownParameter(Host& _host, const int _part, const std::string& _name, const int _value)
	{
		_host.clear();
		_host.automate(_host.index(_name, _part), static_cast<float>(_value) / 127.0f);
		require(_host.edits().empty() && _host.rereads() == 0, "a parameter of the Track shown, automated, told the host "
			+ std::to_string(_host.edits().size()) + " edits and " + std::to_string(_host.rereads()) + " re-reads");
	}

	// A state restored where it was saved: no edit (a re-read is how the host follows a state)
	inline void stateRestored(Host& _host)
	{
		juce::MemoryBlock saved;
		_host.plugin().getStateInformation(saved);
		_host.clear();
		_host.restore(saved);
		require(_host.edits().empty(), "MD Drums told the host " + std::to_string(_host.edits().size())
			+ " edits while restoring a state");
	}
}
