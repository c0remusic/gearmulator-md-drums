// MD Drums as a host loads it: the VST3 the user installs, through JUCE's VST3 hosting, as Live does.
// mdDrumsProcessorTest checks the processor; this checks the VST3 layer between it and the host: the class and the
// 608 parameter IDs that Live sets keep (the hashes of the frozen "page_part_index" strings), the only automatable ones
// with Bypass; Main and the 16 Outs and their channels; notes and host automation through the VST3 process call; the
// latency reported; the state through IComponent; the same samples whatever the block sizes and offline; no block that
// stalls the audio thread; nothing of its own told to the host while it automates or restores a state (the host would
// take it for an edit; a Track's machine is the exception, its SYN1-8 taking the machine's defaults); and the editor
// through IPlugView, drawn in the host's window while an audio thread plays.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr double g_rate = 44100.0;
	constexpr int g_block = 128;
	constexpr int g_tracks = 16;
	constexpr int g_parameters = 608;	// 16 Tracks of 34, the 32 master effects, the 32 relays for Push 2
	constexpr int g_relays = 576;		// the first relay: those follow the Track shown and tell the host (ticket 27)
	constexpr int g_latency = 51;		// what the plug-in reports at 44.1 kHz: Main's
	constexpr float g_silence = 1e-5f;	// Main keeps a tail of a few LSB after a Hit (EQ and Dynamix)
	constexpr float g_sound = 1e-3f;

	// The VST3 class's ID as JUCE's hosting hashes it (PluginDescription::uniqueId), from the codes C0rm and Mddr: Live
	// finds MD Drums in a set by it
	constexpr int g_classHash = static_cast<int>(0xbeaa512fu);

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void pump(const int _milliseconds)
	{
		juce::MessageManager::getInstance()->runDispatchLoopUntil(_milliseconds);
	}

	// The frozen IDs in the plug-in's order: the 34 Track parameters for each Track, then the master effects and the
	// relays, which belong to no Track (parameterDescriptions_mddrums.json)
	struct Frozen
	{
		std::vector<juce::String> ids;
		std::vector<juce::String> names;
		std::vector<int> parts;

		int index(const juce::String& _name, const int _part = 0) const
		{
			for(size_t i = 0; i < ids.size(); ++i)
			{
				if(names[i] == _name && parts[i] == _part)
					return static_cast<int>(i);
			}
			throw std::runtime_error("no parameter " + _name.toStdString());
		}
	};

	Frozen frozen()
	{
		const auto json = juce::JSON::parse(juce::File(MD_DRUMS_PARAMETERS));
		const auto* list = json["parameterdescriptions"].getArray();
		require(list != nullptr, "cannot read " MD_DRUMS_PARAMETERS);

		Frozen result;
		const auto add = [&](const juce::var& _entry, const int _part)
		{
			result.ids.push_back(juce::String::formatted("%d_%d_%d", static_cast<int>(_entry["page"]), _part,
				static_cast<int>(_entry["index"])));
			result.names.push_back(_entry["name"].toString());
			result.parts.push_back(_part);
		};
		for(int part = 0; part < g_tracks; ++part)
		{
			for(const auto& entry : *list)
			{
				if(entry["class"].toString() != "NonPartSensitive")
					add(entry, part);
			}
		}
		for(const auto& entry : *list)
		{
			if(entry["class"].toString() == "NonPartSensitive")
				add(entry, 0);
		}
		return result;
	}

	// JUCE's VST3 wrapper: a parameter's VST3 ID is its string ID's hash, its top bit cleared for Studio One
	uint32_t vst3Id(const juce::String& _id)
	{
		return static_cast<uint32_t>(_id.hashCode()) & 0x7fffffffu;
	}

	// Set around the host's process call: what the plug-in tells the host meanwhile comes as the call's outputs
	thread_local bool g_inProcess = false;

	// What the plug-in tells the host by itself: parameters it sets (performEdit, or the outputs of its process call),
	// its gestures and its other changes (restartComponent, setDirty)
	struct Told final : juce::AudioProcessorListener
	{
		std::mutex mutex;
		std::vector<int> parameters;
		int outputs = 0;	// of those, the process call's outputs; the others came as performEdit
		int gestures = 0;
		int changes = 0;

		void audioProcessorParameterChanged(juce::AudioProcessor*, const int _index, float) override
		{
			const std::lock_guard lock(mutex);
			parameters.push_back(_index);
			if(g_inProcess)
				++outputs;
		}

		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override
		{
			const std::lock_guard lock(mutex);
			++changes;
		}

		void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override
		{
			const std::lock_guard lock(mutex);
			++gestures;
		}

		void clear()
		{
			const std::lock_guard lock(mutex);
			parameters.clear();
			outputs = 0;
			gestures = 0;
			changes = 0;
		}

		// The Track and master parameters told, each once, by name
		std::string own(const Frozen& _frozen)
		{
			const std::lock_guard lock(mutex);
			std::map<int, int> counts;
			for(const auto index : parameters)
			{
				if(index < g_relays)
					++counts[index];
			}
			std::string result;
			for(const auto& [index, count] : counts)
			{
				result += (result.empty() ? "" : ", ") + _frozen.names[static_cast<size_t>(index)].toStdString();
				if(index < g_tracks * 34)
					result += " (Track " + std::to_string(_frozen.parts[static_cast<size_t>(index)] + 1) + ")";
				if(count > 1)
					result += " x" + std::to_string(count);
			}
			return result;
		}

		std::string summary(const Frozen& _frozen)
		{
			const auto ownParameters = own(_frozen);
			const std::lock_guard lock(mutex);
			const auto relays = std::count_if(parameters.begin(), parameters.end(), [](const int _i) { return _i >= g_relays; });
			return std::to_string(parameters.size()) + " parameters (" + std::to_string(relays) + " relays; "
				+ (ownParameters.empty() ? std::string("no other") : ownParameters) + "; " + std::to_string(outputs)
				+ " as process outputs), " + std::to_string(gestures)
				+ " gestures, " + std::to_string(changes) + " other changes";
		}
	};

	struct Instance
	{
		Told told;
		std::unique_ptr<juce::AudioPluginInstance> plugin;

		~Instance()
		{
			if(plugin)
				plugin->removeListener(&told);
		}

		juce::AudioProcessorParameter& parameter(const int _index) const { return *plugin->getParameters()[_index]; }

		void prepare(const double _rate) const
		{
			plugin->releaseResources();
			plugin->prepareToPlay(_rate, g_block);
		}

		// Out 01-16 the host takes, the others disabled
		void outs(const std::vector<int>& _outs, const double _rate = g_rate) const
		{
			plugin->releaseResources();
			auto layout = plugin->getBusesLayout();
			for(int bus = 1; bus < layout.outputBuses.size(); ++bus)
			{
				layout.outputBuses.getReference(bus) = std::find(_outs.begin(), _outs.end(), bus) != _outs.end()
					? juce::AudioChannelSet::mono() : juce::AudioChannelSet::disabled();
			}
			require(plugin->setBusesLayout(layout), "the host cannot take the Outs it asks for");
			plugin->prepareToPlay(_rate, g_block);
		}
	};

	std::unique_ptr<Instance> load(juce::AudioPluginFormatManager& _formats, const juce::PluginDescription& _description)
	{
		auto instance = std::make_unique<Instance>();
		juce::String error;
		instance->plugin = _formats.createPluginInstance(_description, g_rate, g_block, error);
		require(instance->plugin != nullptr, "the host cannot create MD Drums: " + error.toStdString());
		instance->plugin->addListener(&instance->told);
		instance->plugin->prepareToPlay(g_rate, g_block);
		pump(300);
		return instance;
	}

	struct Note
	{
		int64_t sample;
		int number;
		int velocity;
	};

	// Notes 36-51 (Tracks 1-16) one after the other
	std::vector<Note> eachTrack(const int64_t _start, const int64_t _spacing)
	{
		std::vector<Note> notes;
		for(int track = 0; track < g_tracks; ++track)
			notes.push_back({_start + track * _spacing, 36 + track, 100});
		return notes;
	}

	// Two Hits a sixteenth at 120 BPM over the 16 Tracks, from 0.25 s
	std::vector<Note> pattern(const double _seconds, const double _rate)
	{
		std::vector<Note> notes;
		for(int step = 0; 0.25 + step * 0.125 < _seconds - 0.5; ++step)
		{
			const auto at = std::llround((0.25 + step * 0.125) * _rate);
			notes.push_back({at, 36 + step * 5 % 16, 60 + step * 37 % 64});
			notes.push_back({at, 36 + (step * 7 + 3) % 16, 100});
		}
		return notes;
	}

	// The plug-in played as a host's audio callback plays it, in blocks of _block or, with a seed, of random sizes up to
	// it; every channel the host enabled, the whole run
	juce::AudioBuffer<float> render(juce::AudioPluginInstance& _plugin, const std::vector<Note>& _notes,
		const int64_t _samples, const int _block = g_block, uint32_t _random = 0, std::vector<double>* _blockMs = nullptr,
		const std::function<void(int64_t)>& _beforeBlock = {})
	{
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
				midi.addEvent(juce::MidiMessage::noteOn(1, _notes[next].number, static_cast<juce::uint8>(_notes[next].velocity)),
					static_cast<int>(_notes[next].sample - position));
			}
			if(_beforeBlock)
				_beforeBlock(position);
			const auto start = Clock::now();
			g_inProcess = true;
			_plugin.processBlock(buffer, midi);
			g_inProcess = false;
			if(_blockMs)
				_blockMs->push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
			for(int channel = 0; channel < channels; ++channel)
				take.copyFrom(channel, static_cast<int>(position), buffer, channel, 0, size);
			position += size;
		}
		return take;
	}

	float peak(const juce::AudioBuffer<float>& _audio, const int _channel, const int64_t _from, const int64_t _length)
	{
		const auto length = std::min<int64_t>(_length, _audio.getNumSamples() - _from);
		return _audio.getMagnitude(_channel, static_cast<int>(_from), static_cast<int>(length));
	}

	// A host automating: the value set, then blocks for the VST3 process call to carry it
	void automate(Instance& _instance, const int _index, const float _value)
	{
		_instance.parameter(_index).setValue(_value);
		render(*_instance.plugin, {}, g_block * 4);
		pump(50);
	}

	// A block far over its deadline (2.9 ms), whatever the PC does meanwhile: what a DSP's JIT compiling on the audio
	// thread costs (27 ms in the first block before MasterEngine::warmUp, up to 8 ms at a machine's first Hit before
	// EngineT::warmUpVoices)
	constexpr double g_stallMs = 10.0;

	std::string blockStats(const std::vector<double>& _ms)
	{
		const auto worst = std::max_element(_ms.begin(), _ms.end()) - _ms.begin();
		auto sorted = _ms;
		std::sort(sorted.begin(), sorted.end());
		const auto deadline = 1000.0 * g_block / g_rate;
		const auto late = std::count_if(sorted.begin(), sorted.end(), [&](const double _m) { return _m > deadline; });
		char text[200];
		std::snprintf(text, sizeof(text), "median %.3f ms, 99.9 %% %.3f ms, worst %.3f ms (block %d), %d of %zu over %.2f ms",
			sorted[sorted.size() / 2], sorted[std::min(sorted.size() - 1, sorted.size() * 999 / 1000)], sorted.back(),
			static_cast<int>(worst), static_cast<int>(late), sorted.size(), deadline);
		return text;
	}

	// The window a host gives the editor
	struct Window final : juce::Component
	{
		Window() { setOpaque(true); }
		void paint(juce::Graphics& _g) override { _g.fillAll(juce::Colours::magenta); }
	};

	// The colours the window shows, the plug-in's own window inside it included: one when the editor drew nothing
	size_t colours(juce::Component& _window)
	{
#if JUCE_WINDOWS
		auto* const window = static_cast<HWND>(_window.getWindowHandle());
		RECT rect{};
		GetClientRect(window, &rect);
		const int width = rect.right, height = rect.bottom;
		if(width <= 0 || height <= 0)
			return 0;
		HDC screen = GetDC(nullptr);
		HDC dc = CreateCompatibleDC(screen);
		HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
		const auto old = SelectObject(dc, bitmap);
		const auto printed = PrintWindow(window, dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
		SelectObject(dc, old);
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = width;
		info.bmiHeader.biHeight = -height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
		const auto rows = GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);
		DeleteObject(bitmap);
		DeleteDC(dc);
		ReleaseDC(nullptr, screen);
		if(!printed || rows != height)
			return 0;
		std::unordered_set<uint32_t> seen;
		for(const auto pixel : pixels)
			seen.insert(pixel & 0xffffffu);
		return seen.size();
#else
		return 2;
#endif
	}

	juce::File bundleOf(const juce::File& _file)
	{
		for(auto file = _file; file.exists() && file != file.getParentDirectory(); file = file.getParentDirectory())
		{
			if(file.isDirectory() && file.hasFileExtension("vst3"))
				return file;
		}
		return _file;
	}

	void run(const juce::File& _bundle)
	{
		const auto ids = frozen();
		require(ids.ids.size() == g_parameters, "the JSON does not hold 608 parameters");

		// The class Live finds in a set
		juce::AudioPluginFormatManager formats;
		formats.addFormat(new juce::VST3PluginFormat());
		juce::OwnedArray<juce::PluginDescription> found;
		formats.getFormat(0)->findAllTypesForFile(found, _bundle.getFullPathName());
		require(found.size() == 1, "the VST3 does not hold one plug-in");
		const auto& description = *found[0];
		std::printf("%s by %s %s, %s, %s, class hash %08x\n", description.name.toRawUTF8(),
			description.manufacturerName.toRawUTF8(), description.version.toRawUTF8(), description.category.toRawUTF8(),
			description.isInstrument ? "instrument" : "effect", static_cast<unsigned>(description.uniqueId));
		require(description.name == "MD Drums" && description.manufacturerName == "c0remusic" && description.isInstrument,
			"the VST3 is not MD Drums, an instrument by c0remusic");
		require(description.uniqueId == g_classHash, "the VST3 class ID changed: Live sets would no longer find MD Drums");

		{
			const auto start = Clock::now();
			auto instance = load(formats, description);
			auto& plugin = *instance->plugin;
			std::printf("Loaded and prepared in %.0f ms; at load it told the host %s\n",
				std::chrono::duration<double, std::milli>(Clock::now() - start).count(),
				instance->told.summary(ids).c_str());

			// Main and the 16 Outs, the Outs off until the host takes them
			require(plugin.getBusCount(true) == 0 && plugin.getBusCount(false) == 17, "not Main and 16 Outs");
			require(plugin.getBus(false, 0)->getName() == "Main" && plugin.getBus(false, 0)->isEnabled()
				&& plugin.getChannelCountOfBus(false, 0) == 2, "Main is not an enabled stereo bus");
			for(int bus = 1; bus <= g_tracks; ++bus)
			{
				require(plugin.getBus(false, bus)->getName() == juce::String::formatted("Out %02d", bus)
					&& !plugin.getBus(false, bus)->isEnabled()
					&& plugin.getBus(false, bus)->getDefaultLayout() == juce::AudioChannelSet::mono(),
					"Out " + std::to_string(bus) + " is not a mono bus off by default");
			}

			// JUCE's wrapper adds Bypass and its MIDI emulation (a parameter per CC and channel, not automatable: Live lists
			// only those that are)
			const auto& parameters = plugin.getParameters();
			require(parameters.size() >= g_parameters, "fewer than 608 parameters through VST3");
			int automatable = 0;
			juce::StringArray others;
			for(int i = 0; i < parameters.size(); ++i)
			{
				if(!parameters[i]->isAutomatable())
					continue;
				++automatable;
				if(i >= g_parameters)
					others.add(parameters[i]->getName(64));
			}
			std::printf("%d parameters through VST3, %d automatable: the 608 and %s\n", parameters.size(), automatable,
				others.joinIntoString(", ").toRawUTF8());
			require(automatable == g_parameters + others.size() && others == juce::StringArray("Bypass"),
				"the host lists other parameters than MD Drums' 608 and Bypass");
			// The parameters' VST3 IDs: those of the frozen string IDs, which Live sets keep
			std::unordered_set<uint32_t> unique;
			for(int i = 0; i < parameters.size(); ++i)
			{
				const auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(parameters[i]);
				require(hosted != nullptr, "a parameter without a VST3 ID");
				const auto id = static_cast<uint32_t>(hosted->getParameterID().getLargeIntValue());
				require(unique.insert(id).second, "two parameters share a VST3 ID");
				if(i < g_parameters && id != vst3Id(ids.ids[static_cast<size_t>(i)]))
				{
					throw std::runtime_error("parameter " + std::to_string(i) + " \"" + parameters[i]->getName(64).toStdString()
						+ "\" has VST3 ID " + std::to_string(id) + ", not the hash of " + ids.ids[static_cast<size_t>(i)].toStdString());
				}
			}

			// The latency the VST3 reports
			std::printf("Latency at 44.1 kHz: %d samples\n", plugin.getLatencySamples());
			require(plugin.getLatencySamples() == g_latency, "the VST3 does not report Main's 51 samples");

			// Each note 36-51 sounds on Main through the VST3 process call
			{
				const auto spacing = std::llround(0.2 * g_rate);
				const auto take = render(plugin, eachTrack(spacing, spacing), spacing * 18);
				std::printf("Main, Tracks 1-16:");
				for(int track = 0; track < g_tracks; ++track)
				{
					const auto from = (track + 1) * spacing;
					const auto value = std::max(peak(take, 0, from, spacing), peak(take, 1, from, spacing));
					std::printf(" %.3f", value);
					require(value > g_sound, "Track " + std::to_string(track + 1) + "'s note is silent on Main");
				}
				std::printf("\n");
			}

			// The host automating: no Track or master parameter echoed back. A Track's machine is the exception: the
			// Machinedrum gives its SYN1-8 the machine's defaults
			instance->told.clear();
			{
				const auto syn1 = ids.index("SYN1", 0), level = ids.index("Level", 6), echo = ids.index("EchoTIME");
				const auto kept = std::vector<float>{plugin.getParameters()[syn1]->getValue(),
					plugin.getParameters()[level]->getValue(), plugin.getParameters()[echo]->getValue()};
				render(plugin, pattern(1.5, g_rate), std::llround(1.5 * g_rate), g_block, 0, nullptr, [&](const int64_t _position)
				{
					const auto phase = static_cast<float>(_position % 22050) / 22050.0f;
					plugin.getParameters()[syn1]->setValue(phase);
					plugin.getParameters()[level]->setValue(1.0f - phase);
					plugin.getParameters()[echo]->setValue(phase * 0.5f);
				});
				pump(300);
				std::printf("While the host automated SYN1 (Track 1, shown), Level (Track 7) and Echo TIME, it told %s\n",
					instance->told.summary(ids).c_str());
				require(instance->told.own(ids).empty(), "MD Drums echoed a parameter the host automates");
				automate(*instance, syn1, kept[0]);
				automate(*instance, level, kept[1]);
				automate(*instance, echo, kept[2]);
			}
			instance->told.clear();
			automate(*instance, ids.index("Machine", 2), 32.0f / 191.0f);
			pump(300);
			std::printf("The host set Track 3's machine to EFM-BD: told %s\n", instance->told.summary(ids).c_str());

			// The Outs: each Track on its own when the host takes the 16, and its channel the bus's
			instance->outs({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
			require(plugin.getTotalNumOutputChannels() == 18, "Main and 16 Outs are not 18 channels");
			for(int track = 0; track < g_tracks; ++track)
				automate(*instance, ids.index("Out", track), 1.0f);
			{
				const auto spacing = std::llround(0.25 * g_rate);
				const auto take = render(plugin, eachTrack(spacing, spacing), spacing * 18);
				for(int track = 0; track < g_tracks; ++track)
				{
					const auto from = (track + 1) * spacing;
					require(peak(take, 2 + track, from, spacing) > g_sound,
						"Track " + std::to_string(track + 1) + " is silent on its Out");
					// The Outs of the Tracks still to strike (those struck may still ring)
					for(int out = track + 1; out < g_tracks; ++out)
					{
						require(peak(take, 2 + out, from, spacing) < g_silence,
							"Track " + std::to_string(track + 1) + " sounds on Out " + std::to_string(out + 1));
					}
				}
				std::printf("16 Outs taken: each Track on its own Out's channel; Track 1 left Main at %.6f\n",
					std::max(peak(take, 0, spacing, spacing), peak(take, 1, spacing, spacing)));
			}
			// Out 16 alone: its channel follows Main's, and a Track on an Out the host left sounds nowhere
			instance->outs({16});
			{
				const auto spacing = std::llround(0.25 * g_rate);
				const auto take = render(plugin, {{spacing, 51, 100}, {spacing * 3, 40, 100}}, spacing * 5);
				std::printf("Out 16 alone: Track 16 on channel 2 %.3f; Track 5 (Out off) Main %.6f, channel 2 %.6f\n",
					peak(take, 2, spacing, spacing), peak(take, 0, spacing * 3, spacing), peak(take, 2, spacing * 3, spacing));
				require(plugin.getTotalNumOutputChannels() == 3 && peak(take, 2, spacing, spacing) > g_sound,
					"Track 16 is not on Out 16 when the host takes it alone");
				require(peak(take, 2, spacing * 3, spacing) < g_silence && peak(take, 0, spacing * 3, spacing) < g_silence
					&& peak(take, 1, spacing * 3, spacing) < g_silence, "Track 5 sounds although on an Out the host left");
			}
			for(int track = 1; track < g_tracks; ++track)
				automate(*instance, ids.index("Out", track), 0.0f);
			instance->outs({});

			// The state through IComponent. Restored where it was saved, nothing changes, so that whatever reaches the host
			// comes from the plug-in (JUCE's hosting reads every value again after a state, and its listeners hear those
			// that differ): no parameter of its own
			automate(*instance, ids.index("Level", 1), 32.0f / 127.0f);	// on the plug-in's steps: it rounds the host's value
			automate(*instance, ids.index("EchoTIME"), 64.0f / 127.0f);
			juce::MemoryBlock saved;
			plugin.getStateInformation(saved);
			instance->told.clear();
			plugin.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
			pump(300);
			render(plugin, {}, g_block * 8);
			pump(300);
			std::printf("State of %zu bytes restored where it was saved: told %s\n", saved.getSize(),
				instance->told.summary(ids).c_str());
			require(instance->told.own(ids).empty(), "MD Drums told the host its parameters while restoring a state");

			// Another instance gets every parameter back, gives the same state back, and plays Track 1 off Main as saved
			{
				auto restored = load(formats, description);
				restored->plugin->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
				pump(300);
				render(*restored->plugin, {}, g_block * 8);
				pump(300);
				for(int i = 0; i < g_relays; ++i)
				{
					const auto expected = plugin.getParameters()[i]->getValue(), value = restored->parameter(i).getValue();
					require(std::abs(expected - value) < 1e-6f, "the restored " + ids.names[static_cast<size_t>(i)].toStdString()
						+ " is " + std::to_string(value) + ", not " + std::to_string(expected));
				}
				juce::MemoryBlock again;
				restored->plugin->getStateInformation(again);
				require(again == saved, "the restored instance does not give the same state back ("
					+ std::to_string(again.getSize()) + " bytes)");
				const auto spacing = std::llround(0.25 * g_rate);
				const auto take = render(*restored->plugin, {{spacing, 36, 100}}, spacing * 3);
				require(peak(take, 0, spacing, spacing) < g_silence && peak(take, 1, spacing, spacing) < g_silence,
					"the restored Track 1 sounds on Main although on its Out");
			}
		}

		// The same samples whatever the block sizes and offline, from two fresh instances: Live cuts blocks as it likes and
		// renders offline when it freezes or exports
		std::vector<double> blockMs;
		{
			const auto seconds = 6.0;
			const auto notes = pattern(seconds, g_rate);
			const auto samples = std::llround(seconds * g_rate);
			auto fixed = load(formats, description);
			const auto reference = render(*fixed->plugin, notes, samples, g_block, 0, &blockMs);
			auto varied = load(formats, description);
			varied->plugin->setNonRealtime(true);
			const auto other = render(*varied->plugin, notes, samples, g_block, 0x9e3779b9u);
			int64_t first = -1;
			float largest = 0;
			for(int channel = 0; channel < 2; ++channel)
			{
				for(int64_t s = 0; s < samples; ++s)
				{
					const auto i = static_cast<int>(s);
					const auto difference = std::abs(reference.getSample(channel, i) - other.getSample(channel, i));
					if(difference > 0 && (first < 0 || s < first))
						first = s;
					largest = std::max(largest, difference);
				}
			}
			std::printf("Blocks of 128 realtime against random sizes 1-128 offline, %.0f s: peak %.3f, %s\n", seconds,
				reference.getMagnitude(0, 0, static_cast<int>(samples)),
				first < 0 ? "the same samples" : ("first difference at sample " + std::to_string(first) + ", largest "
					+ std::to_string(largest)).c_str());
			require(first < 0, "the samples depend on the host's block sizes or on offline rendering");
			std::printf("A block of 128 through VST3: %s\n", blockStats(blockMs).c_str());
			require(*std::max_element(blockMs.begin(), blockMs.end()) < g_stallMs, "a block stalled the audio thread");

			// Another rate and back, as when the user changes Live's audio device
			auto& plugin = *fixed->plugin;
			fixed->prepare(48000.0);
			const auto at48 = render(plugin, pattern(1.5, 48000.0), std::llround(1.5 * 48000.0));
			std::printf("48 kHz: latency %d samples, peak %.3f\n", plugin.getLatencySamples(),
				at48.getMagnitude(0, 0, at48.getNumSamples()));
			require(at48.getMagnitude(0, 0, at48.getNumSamples()) > g_sound, "silent at 48 kHz");
			fixed->prepare(g_rate);
			require(plugin.getLatencySamples() == g_latency, "the latency did not come back at 44.1 kHz");
		}

		// The editor through IPlugView in the host's window, an audio thread playing and the host automating meanwhile
		{
			auto instance = load(formats, description);
			auto& plugin = *instance->plugin;
			require(plugin.hasEditor(), "MD Drums has no editor through VST3");
			const auto start = Clock::now();
			std::unique_ptr<juce::AudioProcessorEditor> editor(plugin.createEditorIfNeeded());
			require(editor != nullptr, "the host cannot open the editor");
			Window window;
			window.addAndMakeVisible(*editor);
			window.setSize(editor->getWidth(), editor->getHeight());
			window.setTopLeftPosition(48, 48);
			window.addToDesktop(juce::ComponentPeer::windowIsTemporary);
			window.setVisible(true);
			const auto opened = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
			pump(1500);
			window.setSize(editor->getWidth(), editor->getHeight());
			pump(300);
			const auto shown = colours(window);
			std::printf("Editor %dx%d, opened in %.0f ms, %zu colours shown\n", editor->getWidth(), editor->getHeight(), opened,
				shown);
			require(editor->getWidth() > 0 && editor->getHeight() > 0, "the editor has no size");
			require(shown >= 16, "the editor drew nothing in the host's window");

			std::atomic<bool> stop{false};
			std::vector<double> openMs;
			openMs.reserve(4096);
			std::thread audio([&]
			{
				const auto channels = plugin.getTotalNumOutputChannels();
				juce::AudioBuffer<float> buffer(channels, g_block);
				juce::MidiBuffer midi;
				const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(g_block / g_rate));
				auto deadline = Clock::now();
				for(int64_t block = 0; !stop.load(); ++block)
				{
					midi.clear();
					if(block % 43 == 0)	// a Hit every 0.125 s, over the 16 Tracks
					{
						const auto note = 36 + static_cast<int>(block / 43 % g_tracks);
						midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(100)), 0);
					}
					buffer.clear();
					const auto begin = Clock::now();
					plugin.processBlock(buffer, midi);
					openMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
					deadline += period;
					while(Clock::now() < deadline) {}	// a sleeping thread in this process wakes on the 15.6 ms timer
				}
			});
			const auto syn1 = ids.index("SYN1", 0);
			for(int step = 0; step < 30; ++step)
			{
				plugin.getParameters()[syn1]->setValue(static_cast<float>(step % 10) / 10.0f);
				pump(100);
			}
			stop = true;
			audio.join();
			std::printf("Editor open, Hits playing, SYN1 automated: a block of 128 %s\n", blockStats(openMs).c_str());
			require(*std::max_element(openMs.begin(), openMs.end()) < g_stallMs,
				"a block stalled the audio thread while the editor was open");

			window.removeChildComponent(editor.get());
			editor.reset();
			pump(300);
		}
	}
}

int main(const int _argc, char* _argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	if(_argc < 2)
	{
		std::cerr << "usage: mdDrumsVst3HostTest <MD Drums.vst3>\n";
		return 2;
	}
	const auto* const flash = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(flash == nullptr || *flash == '\0' || !std::filesystem::exists(flash))
	{
		std::cout << "mdDrumsVst3HostTest: SKIP (GEARMULATOR_MD_FIRMWARE_BIN names no flash image)\n";
		return 77;
	}

	// The plug-in's data folder (its config, Bank.syx) in a folder of this run, never the user's
	const auto dataRoot = std::filesystem::temp_directory_path()
		/ ("MDDrumsVst3HostTest_" + std::to_string(Clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(dataRoot);
#if JUCE_WINDOWS
	_putenv_s("GEARMULATOR_DATA_ROOT", dataRoot.string().c_str());
#else
	setenv("GEARMULATOR_DATA_ROOT", dataRoot.string().c_str(), 1);
#endif

	int result = 0;
	{
		juce::ScopedJuceInitialiser_GUI juce;
		const auto bundle = bundleOf(juce::File(juce::String(_argv[1])));
		if(!bundle.isDirectory())
		{
			std::cerr << "mdDrumsVst3HostTest: no VST3 bundle at " << _argv[1] << '\n';
			result = 1;
		}
		else
		{
			try
			{
				run(bundle);
				std::cout << "mdDrumsVst3HostTest: OK\n";
			}
			catch(const std::exception& _error)
			{
				std::cerr << "mdDrumsVst3HostTest: " << _error.what() << '\n';
				result = 1;
			}
		}
	}
	std::error_code error;
	std::filesystem::remove_all(dataRoot, error);
	return result;
}
