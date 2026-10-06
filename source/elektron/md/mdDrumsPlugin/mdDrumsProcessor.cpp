#include "mdDrumsProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mdDrums
{
	namespace
	{
		// Room for the largest block at the lowest host rate: engine samples one host block can need.
		constexpr size_t g_pendingCapacity = 1 << 16;

		std::unique_ptr<Engine> createEngine()
		{
			// The plug-in's own folder first (on Windows, the module inside the .vst3 bundle), then its bundle's.
			const auto module = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
			std::vector<std::filesystem::path> folders;
			for(auto folder = module.getParentDirectory(); folders.size() < 4 && folder.exists();
				folder = folder.getParentDirectory())
				folders.emplace_back(folder.getFullPathName().toStdString());
			const auto flash = Engine::findFlashImage(folders);
			if(flash.empty())
				return {};
			try
			{
				return std::make_unique<Engine>(flash);
			}
			catch(const std::exception& _error)
			{
				std::fprintf(stderr, "[MD Drums] %s\n", _error.what());
				return {};
			}
		}

		juce::String trackId(const int _track, const char* _name)
		{
			return "t" + juce::String(_track + 1) + "_" + _name;
		}
	}

	Processor::Processor()
		: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
		, m_engine(createEngine())
		, m_state(*this, nullptr, "MDDrums", createLayout())
	{
		for(int t = 0; t < Engine::TrackCount; ++t)
		{
			auto& track = m_tracks[t];
			track.machine = m_state.getRawParameterValue(trackId(t, "machine"));
			for(int p = 0; p < Engine::ParamCount; ++p)
			{
				track.params[p] = m_state.getRawParameterValue(trackId(t, Engine::paramNames()[p]));
				track.appliedParams[p] = -1;
			}
			track.level = m_state.getRawParameterValue(trackId(t, "level"));
			track.mute = m_state.getRawParameterValue(trackId(t, "mute"));
		}
		for(auto& pending : m_pending)
			pending.resize(g_pendingCapacity);
	}

	Processor::~Processor() = default;

	juce::AudioProcessorValueTreeState::ParameterLayout Processor::createLayout()
	{
		juce::AudioProcessorValueTreeState::ParameterLayout layout;
		for(int t = 0; t < Engine::TrackCount; ++t)
		{
			auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
				"track" + juce::String(t + 1), "Track " + juce::String(t + 1), " | ");
			const auto prefix = "T" + juce::String(t + 1) + " ";

			// The machine as its id (0-191), named after the OS's table, so that a saved set keeps its machines
			// whether or not the flash image is found.
			auto machineName = [this](const int _id, int)
			{
				if(m_engine)
				{
					for(const auto& machine : m_engine->machines())
						if(machine.id == _id)
							return juce::String(machine.name);
				}
				return juce::String(_id);
			};
			group->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackId(t, "machine"), 1},
				prefix + "Machine", 0, 191, Engine::defaultMachine(t), juce::AudioParameterIntAttributes()
					.withStringFromValueFunction(machineName)));
			for(int p = 0; p < Engine::ParamCount; ++p)
			{
				const auto* name = Engine::paramNames()[p];
				group->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackId(t, name), 1},
					prefix + name, 0, 127, Engine::paramDefaults()[p]));
			}
			group->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{trackId(t, "level"), 1},
				prefix + "Level", 0, 127, Engine::DefaultLevel));
			group->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{trackId(t, "mute"), 1},
				prefix + "Mute", false));
			layout.add(std::move(group));
		}
		return layout;
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layouts) const
	{
		return _layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
			&& _layouts.getMainInputChannelSet().isDisabled();
	}

	void Processor::prepareToPlay(const double _sampleRate, int)
	{
		m_ratio = Engine::SampleRate / _sampleRate;
		for(auto& interpolator : m_interpolators)
			interpolator.reset();
		m_pendingCount = 0;
	}

	void Processor::applyParameters()
	{
		const auto value = [](const std::atomic<float>* _parameter)
		{
			return static_cast<int>(std::lround(_parameter->load(std::memory_order_relaxed)));
		};
		for(int t = 0; t < Engine::TrackCount; ++t)
		{
			auto& track = m_tracks[t];
			const auto machine = value(track.machine);
			if(machine != track.appliedMachine)
			{
				// The engine gives a new machine its own SYN defaults; the host's values stay the reference.
				m_engine->setMachine(t, static_cast<uint8_t>(machine));
				track.appliedMachine = machine;
				std::fill(track.appliedParams.begin(), track.appliedParams.begin() + 8, -1);
			}
			for(int p = 0; p < Engine::ParamCount; ++p)
			{
				const auto parameter = value(track.params[p]);
				if(parameter == track.appliedParams[p])
					continue;
				m_engine->setParam(t, p, parameter);
				track.appliedParams[p] = parameter;
			}
			if(const auto level = value(track.level); level != track.appliedLevel)
			{
				m_engine->setLevel(t, level);
				track.appliedLevel = level;
			}
			if(const auto mute = value(track.mute); mute != track.appliedMute)
			{
				m_engine->setMute(t, mute != 0);
				track.appliedMute = mute;
			}
		}
	}

	// _count host samples, at the host's rate.
	void Processor::render(float* _left, float* _right, const int _count)
	{
		if(_count <= 0)
			return;
		if(m_ratio == 1.0)
		{
			m_engine->render(_left, _right, static_cast<size_t>(_count));
			return;
		}
		// Enough engine samples for the interpolators to produce _count, with a few to spare.
		const auto needed = std::min(g_pendingCapacity,
			static_cast<size_t>(std::ceil(_count * m_ratio)) + 4);
		if(m_pendingCount < needed)
		{
			m_engine->render(m_pending[0].data() + m_pendingCount, m_pending[1].data() + m_pendingCount,
				needed - m_pendingCount);
			m_pendingCount = needed;
		}
		const auto used = m_interpolators[0].process(m_ratio, m_pending[0].data(), _left, _count);
		m_interpolators[1].process(m_ratio, m_pending[1].data(), _right, _count);
		const auto consumed = std::min(static_cast<size_t>(used), m_pendingCount);
		for(auto& pending : m_pending)
			std::copy(pending.begin() + consumed, pending.begin() + m_pendingCount, pending.begin());
		m_pendingCount -= consumed;
	}

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		const auto count = _buffer.getNumSamples();
		auto* left = _buffer.getWritePointer(0);
		auto* right = _buffer.getWritePointer(1);
		if(!m_engine)
		{
			_buffer.clear();
			return;
		}

		applyParameters();
		if(const auto* head = getPlayHead())
		{
			if(const auto position = head->getPosition(); position && position->getBpm())
				m_engine->setTempo(*position->getBpm());
		}

		// Render up to each note, then trigger: the engine starts it at its next 32-sample block.
		int done = 0;
		for(const auto metadata : _midi)
		{
			const auto message = metadata.getMessage();
			if(!message.isNoteOn())
				continue;
			const auto track = message.getNoteNumber() - FirstNote;
			if(track < 0 || track >= Engine::TrackCount)
				continue;
			const auto at = std::clamp(metadata.samplePosition, done, count);
			render(left + done, right + done, at - done);
			done = at;
			m_engine->trigger(track, message.getVelocity());
		}
		render(left + done, right + done, count - done);
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new juce::GenericAudioProcessorEditor(*this);
	}

	void Processor::getStateInformation(juce::MemoryBlock& _destData)
	{
		if(const auto xml = m_state.copyState().createXml())
			copyXmlToBinary(*xml, _destData);
	}

	void Processor::setStateInformation(const void* _data, const int _sizeInBytes)
	{
		if(const auto xml = getXmlFromBinary(_data, _sizeInBytes); xml && xml->hasTagName(m_state.state.getType()))
			m_state.replaceState(juce::ValueTree::fromXml(*xml));
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new mdDrums::Processor();
}
