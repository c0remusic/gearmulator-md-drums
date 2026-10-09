// Measurement, not a gate (ticket 31 of the editor map): several MD Drums instances in one process, as Live loads them.
// - The process's private memory for 1, 4 and N instances, each with its Device prepared at 44.1 kHz;
// - the CPU each instance takes on a busy pattern (four Hits a sixteenth at 120 BPM over the 16 Tracks, factory Kit 1's
//   master effects on), rendered in blocks of 128 one instance after the other: the share of real time all instances
//   take together (what a host's threads share out), each instance's, and the worst block against its deadline;
// - the memory an open editor adds, then the Hit screen's preview (its engine, shared by the process), then three more
//   editors.
// usage: mdDrumsInstancesTest [instances=16] [seconds=10]

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsHitView.h"
#include "mdDrumsProcessor.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "synthLib/plugin.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
	using Clock = std::chrono::steady_clock;

	constexpr double g_rate = 44100.0;
	constexpr int g_block = 128;
	constexpr double g_sixteenth = g_rate * 60.0 / 120.0 / 4.0;	// samples, at 120 BPM

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// The process's private memory, in MB
	double privateMb()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
#else
		return 0.0;
#endif
	}

	double msSince(const Clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - _start).count();
	}

	// The Tracks a sixteenth strikes: one walking over the 16, a kick on the beats, hats on every one, a snare on the
	// off-beats of the bar
	std::vector<int> struck(const int64_t _step)
	{
		std::vector<int> tracks{static_cast<int>(_step % 16), 6};
		if(_step % 4 == 0)
			tracks.push_back(0);
		if(_step % 8 == 4)
			tracks.push_back(1);
		return tracks;
	}

	struct Instance
	{
		std::unique_ptr<mdDrums::Processor> processor;
		juce::AudioBuffer<float> buffer;
		juce::MidiBuffer midi;
		int64_t position = 0;
		int64_t offset = 0;	// where its pattern starts, so that instances do not strike together
		double totalMs = 0.0;
		double worstMs = 0.0;
		int lastNotes = 0;	// notes the last block took
	};

	// A block over its deadline: which instance, which block of the measure, the notes it took, how long it took
	struct Late
	{
		size_t instance = 0;
		int block = 0;
		int notes = 0;
		double ms = 0.0;
	};

	// One block of _instance, its pattern's notes at their offsets; the time it took
	double render(Instance& _instance)
	{
		const auto from = _instance.position - _instance.offset;
		const auto to = from + g_block;
		for(auto step = static_cast<int64_t>(std::ceil(static_cast<double>(std::max<int64_t>(from, 0)) / g_sixteenth));
			static_cast<double>(step) * g_sixteenth < static_cast<double>(to); ++step)
		{
			const auto at = static_cast<int64_t>(static_cast<double>(step) * g_sixteenth) - from;
			if(at < 0 || at >= g_block)
				continue;
			for(const auto track : struck(step))
				_instance.midi.addEvent(juce::MidiMessage::noteOn(1, mdDrums::Device::FirstNote + track, static_cast<juce::uint8>(100)),
					static_cast<int>(at));
		}
		_instance.lastNotes = _instance.midi.getNumEvents();
		const auto start = Clock::now();
		static_cast<juce::AudioProcessor&>(*_instance.processor).processBlock(_instance.buffer, _instance.midi);
		const auto ms = msSince(start);
		_instance.midi.clear();
		_instance.position += g_block;
		return ms;
	}
}

int main(const int _argc, char** _argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI juce;
	const auto count = _argc > 1 ? std::max(1, std::atoi(_argv[1])) : 16;
	const auto seconds = _argc > 2 ? std::max(1, std::atoi(_argv[2])) : 10;
	try
	{
		std::vector<Instance> instances;
		instances.reserve(static_cast<size_t>(count));
		const auto baseline = privateMb();
		std::printf("%d instances, %d s of a busy pattern at 44.1 kHz in blocks of %d; process at %.1f MB before\n", count,
			seconds, g_block, baseline);
		for(int i = 0; i < count; ++i)
		{
			Instance instance;
			instance.processor = std::make_unique<mdDrums::Processor>(true);
			if(!dynamic_cast<const mdDrums::Device*>(instance.processor->getPlugin().getDevice()))
			{
				std::cout << "mdDrumsInstancesTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
				return 77;
			}
			auto& host = static_cast<juce::AudioProcessor&>(*instance.processor);
			host.setRateAndBufferSizeDetails(g_rate, g_block);
			host.prepareToPlay(g_rate, g_block);
			instance.buffer.setSize(host.getTotalNumOutputChannels(), g_block);
			instance.offset = static_cast<int64_t>(i) * 37;
			render(instance);
			instances.push_back(std::move(instance));
			const auto n = static_cast<int>(instances.size());
			if(n == 1 || n == 4 || n == count)
			{
				const auto memory = privateMb() - baseline;
				std::printf("  %2d instance(s): %.1f MB, %.1f MB each\n", n, memory, memory / n);
			}
		}

		// A second of warming up, then the measure
		const auto blocks = static_cast<int>(g_rate / g_block);
		for(int b = 0; b < blocks; ++b)
			for(auto& instance : instances)
				render(instance);
		// As a host's audio thread: the scheduler's highest priority, so that what it measures is the plug-in's
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
		const auto measured = static_cast<int>(seconds * g_rate / g_block);
		double total = 0.0;
		const auto deadline = g_block / g_rate * 1000.0;
		std::vector<double> times;
		std::vector<Late> lates;
		times.reserve(static_cast<size_t>(measured) * instances.size());
		for(int b = 0; b < measured; ++b)
		{
			for(size_t i = 0; i < instances.size(); ++i)
			{
				auto& instance = instances[i];
				const auto ms = render(instance);
				instance.totalMs += ms;
				instance.worstMs = std::max(instance.worstMs, ms);
				total += ms;
				times.push_back(ms);
				if(ms > deadline)
					lates.push_back({i, b, instance.lastNotes, ms});
			}
		}
		const auto audioMs = measured * g_block / g_rate * 1000.0;
		double lowest = 1e9, highest = 0.0;
		for(const auto& instance : instances)
		{
			const auto share = instance.totalMs / audioMs * 100.0;
			lowest = std::min(lowest, share);
			highest = std::max(highest, share);
		}
		std::sort(times.begin(), times.end());
		const auto at = [&](const double _fraction)
		{
			return times[std::min(times.size() - 1, static_cast<size_t>(_fraction * static_cast<double>(times.size())))];
		};
		const auto late = static_cast<size_t>(times.end() - std::upper_bound(times.begin(), times.end(), deadline));
		std::printf("CPU: all %d instances %.1f %% of real time (%.2f cores' worth); one instance %.1f to %.1f %%\n", count,
			total / audioMs * 100.0, total / audioMs, lowest, highest);
		std::printf("a block of one instance: median %.3f ms, 99 %% %.3f ms, 99.9 %% %.3f ms, worst %.2f ms, against %.2f ms;"
			" %zu of %zu over it\n", at(0.5), at(0.99), at(0.999), times.back(), deadline, late, times.size());
		// Where they fall: on a note or not, spread over instances and time or gathered
		size_t withNotes = 0;
		std::string where;
		for(size_t l = 0; l < lates.size(); ++l)
		{
			withNotes += lates[l].notes > 0;
			if(l < 24)
				where += " " + std::to_string(lates[l].instance + 1) + "@" + std::to_string(lates[l].block) + "("
					+ std::to_string(lates[l].notes) + "n," + std::to_string(static_cast<int>(lates[l].ms * 10) / 10.0).substr(0, 4)
					+ ")";
		}
		std::printf("late blocks: %zu with a note in them; instance@block(notes,ms):%s\n", withNotes, where.c_str());
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif

		// Editors: the first, the Hit screen's preview, three more
		const auto beforeEditors = privateMb();
		const auto open = [&](Instance& _instance) -> mdDrums::Editor&
		{
			_instance.processor->setForceSoftwareRendererForSession(true);
			auto& state = _instance.processor->getOrCreateEditorState();
			auto* editor = dynamic_cast<mdDrums::Editor*>(state.getEditor());
			require(editor != nullptr, "no editor");
			// One layout, so that the screens know they show
			auto* component = editor->getRmlComponent();
			require(component && component->getContext(), "the editor has no RmlUi context");
			juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
			component->getContext()->Update();
			return *editor;
		};
		auto& first = open(instances.front());
		const auto oneEditor = privateMb() - beforeEditors;

		auto* view = first.getHitView();
		require(view != nullptr, "no Hit view");
		auto* fltf = instances.front().processor->getController().getParameter("FLTF", 0);
		require(fltf != nullptr, "no FLTF");
		double now = 1.0e6;
		view->update(now);
		// The audio stopped with the pattern: its last Hit is no longer playing 100 ms later, and the preview may show
		view->update(now += 100.0);
		fltf->setUnnormalizedValueNotifyingHost(fltf->getUnnormalizedValue() == 40 ? 41 : 40, pluginLib::Parameter::Origin::Ui);
		view->update(now += 1.0);
		const auto start = Clock::now();
		while(view->previewPending() || !view->previewing())
		{
			if(msSince(start) >= 10000.0)
			{
				const auto* screen = first.findChild("screen_hit", false);
				std::printf("no preview: Hit screen %s, request %s, live Hit %u playing %s\n",
					screen && screen->IsVisible(true) ? "visible" : "hidden", view->previewPending() ? "waiting" : "none",
					view->current().id, view->playing() ? "yes" : "no");
				throw std::runtime_error("no preview within 10 s");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			view->update(now += 0.05);
		}
		// The next engine is built while nothing waits
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		const auto withPreview = privateMb() - beforeEditors;

		const auto opened = std::min<size_t>(4, instances.size());
		for(size_t i = 1; i < opened; ++i)
			open(instances[i]);
		const auto moreEditors = privateMb() - beforeEditors;
		std::printf("editors: the first %.1f MB; with the Hit screen's preview %.1f MB; %zu editors %.1f MB (%.1f MB each "
			"after the first)\n", oneEditor, withPreview, opened, moreEditors,
			opened > 1 ? (moreEditors - withPreview) / static_cast<double>(opened - 1) : 0.0);
		std::printf("process at %.1f MB\n", privateMb());

		for(auto& instance : instances)
			instance.processor->destroyEditorState();
		std::cout << "mdDrumsInstancesTest: done\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsInstancesTest: " << _error.what() << '\n';
		return 1;
	}
}
