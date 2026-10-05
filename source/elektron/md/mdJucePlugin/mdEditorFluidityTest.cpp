// How smoothly the MD/MM editor draws. Two measures:
// - by default (ctest): the cost of one frame, per page, while values move at 60 Hz, split in
//   what the presentation timer does, the RmlUi update (layout and draw calls, on the message
//   thread whatever the renderer) and rasterizing the frame with the software renderer (on the
//   GPU with OpenGL or Metal). It requires 60 Hz with an accelerated renderer (presentation and
//   update under 16.7 ms on every page), checks that the editor asks 60 Hz of those renderers
//   and 30 of the software one, and reports what the software renderer reaches.
// - --window [seconds per page] [--scale percent]: the editor in a real window of the size a
//   host opens (1100 x 606 dp at the GUI scale, 100 % by default) with its default renderer, an
//   audio thread running the machine in real time (the firmware when a ROM is found, as in the
//   plug-in), values moving at 60 Hz; it counts the frames RmlUi delivers each second on each
//   page (evPostUpdate) against 60.
// mmEditorFluidityTest (MD_EDITOR_FLUIDITY_TEST_MM) runs the Monomachine plug-in.
// MD_EDITOR_FLUIDITY_REQUIRE=0 reports without failing.

#include "mdEditor.h"
#include "mdPluginEditorState.h"
#include "mdPluginProcessor.h"

#include "jucePluginLib/controller.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "mdAutomationTestSupport.h"
#include "mdController.h"
#include "mdLiveDevice.h"
#include "mdMasterEffectsView.h"
#include "mdOutputMetersView.h"
#include "mdSystemPage.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdpanel.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mdJucePlugin
{
	struct ControllerAutomationTestAccess
	{
		static void useSyntheticFirmware(Controller& _controller)
		{
			_controller.m_syntheticFirmwareReadyForTests = true;
		}
	};

	// What the presentation timer does (Editor::timerCallback), without a timer
	struct EditorIdentityTestAccess
	{
		static void present(Editor& _editor)
		{
			_editor.timerCallback(1);
		}
	};
}

namespace juceRmlUi
{
	struct RenderingTestAccess
	{
		static void update(RmlComponent& _component) { _component.update(); }
		static float targetFPS(const RmlComponent& _component) { return _component.m_targetFPS; }
		static void useDefaultFrameRateFor(RmlComponent& _component, const RmlComponent::Renderer _renderer)
		{
			_component.useDefaultFrameRateFor(_renderer);
		}
		static const char* renderer(const RmlComponent& _component)
		{
			switch(_component.m_renderType)
			{
			case RmlComponent::Renderer::Software: return "software";
			case RmlComponent::Renderer::Gl2: return "OpenGL 2";
			case RmlComponent::Renderer::Gl3: return "OpenGL 3";
			default: return "other";
			}
		}
	};
}

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	bool failuresRequired()
	{
		const auto* value = std::getenv("MD_EDITOR_FLUIDITY_REQUIRE");
		return !value || std::string(value) != "0";
	}

	struct Stats
	{
		double mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
	};

	Stats stats(std::vector<double> _values)
	{
		Stats result;
		if(_values.empty())
			return result;
		std::sort(_values.begin(), _values.end());
		const auto at = [&](const double _q) { return _values[std::min(_values.size() - 1, static_cast<size_t>(_q * static_cast<double>(_values.size())))]; };
		for(const auto value : _values)
			result.mean += value;
		result.mean /= static_cast<double>(_values.size());
		result.p50 = at(0.5);
		result.p95 = at(0.95);
		result.p99 = at(0.99);
		result.max = _values.back();
		return result;
	}

	Rml::Element* element(Rml::ElementDocument& _doc, const std::string& _id)
	{
		auto* e = _doc.GetElementById(_id);
		require(e != nullptr, "missing element " + _id);
		return e;
	}

	Rml::Element* tabButton(Rml::ElementDocument& _doc, const std::string& _group, const std::string& _index)
	{
		Rml::ElementList buttons;
		_doc.GetElementsByTagName(buttons, "button");
		for(auto* b : buttons)
		{
			if(b->GetAttribute("tabgroup", std::string()) == _group && b->GetAttribute("tabbutton", std::string()) == _index)
				return b;
		}
		throw std::runtime_error("no tab button " + _group + "/" + _index);
	}

	// Progress of the --window run, at once on stdout
	void progress(const char* _step)
	{
		std::printf("  ... %s\n", _step);
		std::fflush(stdout);
	}

	struct Page
	{
		const char* name;
		std::function<void(Rml::ElementDocument&)> show;
	};

	// The pages to measure, each shown the way a user gets there
	std::vector<Page> pages(const md::MachineModel _model)
	{
		std::vector<Page> result;
		result.push_back({"FACE AVANT", [](Rml::ElementDocument& _doc) { element(_doc, "mdViewPanel")->Click(); }});
		result.push_back({"SON", [](Rml::ElementDocument& _doc)
		{
			element(_doc, "mdViewEditor")->Click();
			tabButton(_doc, "mdEdit", "0")->Click();
			element(_doc, "editTrack0")->Click();
		}});
		if(_model == md::MachineModel::Machinedrum)
		{
			result.push_back({"MASTER", [](Rml::ElementDocument& _doc)
			{
				element(_doc, "mdViewEditor")->Click();
				tabButton(_doc, "mdEdit", "0")->Click();
				element(_doc, "editMaster")->Click();
			}});
		}
		result.push_back({"MIX", [](Rml::ElementDocument& _doc)
		{
			element(_doc, "mdViewEditor")->Click();
			tabButton(_doc, "mdEdit", "1")->Click();
		}});
		result.push_back({"JOUER", [](Rml::ElementDocument& _doc)
		{
			element(_doc, "mdViewEditor")->Click();
			tabButton(_doc, "mdEdit", "2")->Click();
		}});
		result.push_back({"BIBLIO", [](Rml::ElementDocument& _doc)
		{
			element(_doc, "mdViewEditor")->Click();
			tabButton(_doc, "mdEdit", "3")->Click();
		}});
		result.push_back({"SYSTÈME", [](Rml::ElementDocument& _doc)
		{
			element(_doc, "mdViewEditor")->Click();
			tabButton(_doc, "mdEdit", "4")->Click();
		}});
		return result;
	}

	// Values moving at 60 Hz, as a busy session moves them: the edited track's machine and
	// mix parameters automated by the host, levels on the three output buses.
	class Mover
	{
	public:
		Mover(mdJucePlugin::AudioPluginAudioProcessor& _processor, const md::MachineModel _model)
			: m_processor(_processor)
		{
			const char* names[] = {"MachineParameter1", "MachineParameter2", "FilterBase", "Volume", "Pan", "LFOSpeed"};
			const char* mmNames[] = {"SynthesisA", "SynthesisB", "FilterBase", "AmpVolume", "AmpPan", "Lfo1Speed"};
			auto& controller = _processor.getController();
			for(size_t index = 0; index < 6; ++index)
			{
				if(auto* parameter = controller.getParameter(_model == md::MachineModel::Monomachine ? mmNames[index] : names[index], 0))
					m_parameters.push_back(parameter);
			}
			require(!m_parameters.empty(), "no parameters to move");
		}

		// _flush: without timers, do what the controller timer and JUCE's asynchronous Value
		// listeners do for the moved parameters
		void step(const bool _meters, const bool _flush)
		{
			++m_frame;
			for(size_t index = 0; index < m_parameters.size(); ++index)
			{
				const auto phase = static_cast<float>(m_frame) * 0.05f + static_cast<float>(index);
				// What a host writes when it plays automation
				m_parameters[index]->setValue(0.5f + 0.45f * std::sin(phase));
				if(_flush)
				{
					m_parameters[index]->flushRealtimeValueToUi();
					m_parameters[index]->getValueObject().getValueSource().sendChangeMessage(true);
				}
			}
			if(!_meters)
				return;
			// A bar of levels on each channel: what processBlock measures with sound playing
			for(size_t channel = 0; channel < mdJucePlugin::OutputMeters::ChannelCount; ++channel)
			{
				const auto level = 0.5f + 0.45f * std::sin(static_cast<float>(m_frame) * 0.2f + static_cast<float>(channel));
				std::fill(m_block.begin(), m_block.end(), level);
				m_processor.getOutputMeters().measure(channel, m_block.data(), static_cast<int>(m_block.size()));
			}
		}

	private:
		mdJucePlugin::AudioPluginAudioProcessor& m_processor;
		std::vector<pluginLib::Parameter*> m_parameters;
		std::vector<float> m_block = std::vector<float>(128);
		uint64_t m_frame = 0;
	};

	// Software renderer: the cost of each frame, page by page, rasterized at _scalePercent of the editor's size
	bool measureFrameCost(const md::MachineModel _model, const double _scalePercent)
	{
		mdJucePlugin::AudioPluginAudioProcessor processor(_model,
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{std::string{}}, false);
		processor.setForceSoftwareRendererForSession(true);
		auto& editorState = static_cast<mdJucePlugin::PluginEditorState&>(processor.getOrCreateEditorState());
		auto* editor = dynamic_cast<mdJucePlugin::Editor*>(editorState.getEditor());
		require(editor != nullptr, "processor did not create the editor");
		auto* component = editor->getRmlComponent();
		require(component && component->getDocument(), "editor has no RmlUi document");
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		auto& doc = *component->getDocument();
		auto& controller = dynamic_cast<mdJucePlugin::Controller&>(processor.getController());
		mdJucePlugin::ControllerAutomationTestAccess::useSyntheticFirmware(controller);

		// The editor asks 60 Hz of OpenGL and Metal, and leaves the software renderer at its 30
		using Renderer = juceRmlUi::RmlComponent::Renderer;
		require(juceRmlUi::RenderingTestAccess::targetFPS(*component) == 30.0f, "software renderer not at 30 Hz");
		juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(*component, Renderer::Gl3);
		require(juceRmlUi::RenderingTestAccess::targetFPS(*component) == 60.0f, "OpenGL not at 60 Hz for the editor");
		juceRmlUi::RenderingTestAccess::useDefaultFrameRateFor(*component, Renderer::Software);

		juceRmlUi::LookAndFeel lookAndFeel;
		component->setLookAndFeel(&lookAndFeel);
		// The size a host gives the editor at a GUI scale (1100 x 606 dp at 100 %): the software renderer
		// rasterizes that many pixels
		{
			auto& state = processor.getOrCreateEditorState();
			const auto scale = _scalePercent / 100.0 * state.getRootScale();
			editor->setSize(juce::roundToInt(state.getWidth() * scale), juce::roundToInt(state.getHeight() * scale));
			juceRmlUi::RenderingTestAccess::update(*component);
		}
		juce::Image image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
		Mover mover(processor, _model);

		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		std::printf("mdEditorFluidityTest %s: software renderer, %dx%d (%.1f %%), one frame = presentation timer"
			" + RmlUi update + rasterized frame\n", name, component->getWidth(), component->getHeight(), _scalePercent);

		// Frames reach the machine without the device lock, which pauses its rendering: counted from
		// the second frame, the first takes what the device shares with the editor
		auto& instrumentation = processor.getPlugin().getRealtimeInstrumentation();
		instrumentation.setEnabled(true);
		mdJucePlugin::EditorIdentityTestAccess::present(*editor);
		const auto accessesBefore = instrumentation.snapshot().deviceAccessCount;

		bool fast = true;
		bool accelerated = true;
		constexpr int frames = 240;
		for(const auto& page : pages(_model))
		{
			page.show(doc);
			// A frame: the values move and the presentation timer runs (presentation), RmlUi lays out
			// and records its draw calls (update: what the message thread does with OpenGL too), then
			// the software renderer rasterizes them (raster: on the GPU with OpenGL)
			std::vector<double> costs, presentation, update, raster;
			const auto since = [](const std::chrono::steady_clock::time_point _start)
			{
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start).count();
			};
			for(int frame = 0; frame < frames + 20; ++frame)
			{
				const auto start = std::chrono::steady_clock::now();
				mover.step(true, true);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				const auto presented = since(start);
				const auto updateStart = std::chrono::steady_clock::now();
				juceRmlUi::RenderingTestAccess::update(*component);
				const auto updated = since(updateStart);
				const auto rasterStart = std::chrono::steady_clock::now();
				lookAndFeel.getCurrentImage() = image;
				{
					juce::Graphics g(image);
					component->paint(g);
				}
				const auto rasterized = since(rasterStart);
				if(frame < 20)	// the first frames build caches
					continue;
				costs.push_back(since(start));
				presentation.push_back(presented);
				update.push_back(updated);
				raster.push_back(rasterized);
			}
			const auto s = stats(costs);
			std::printf("  %-10s frame p50 %5.2f ms  p95 %5.2f  max %5.2f  -> %3.0f frames/s  |  presentation p95 %5.2f  update p95 %5.2f"
				"  raster p95 %5.2f\n", page.name, s.p50, s.p95, s.max, s.p95 > 0 ? 1000.0 / s.p95 : 0.0,
				stats(presentation).p95, stats(update).p95, stats(raster).p95);
			fast &= s.p95 < 1000.0 / 60.0;
			accelerated &= stats(presentation).p95 + stats(update).p95 < 1000.0 / 60.0;
		}
		component->setLookAndFeel(nullptr);
		const auto accesses = instrumentation.snapshot().deviceAccessCount - accessesBefore;
		instrumentation.setEnabled(false);
		// With OpenGL the message thread only presents and updates; the GPU rasterizes on its own thread
		std::printf("  60 Hz with OpenGL (presentation + update under 16.7 ms on every page): %s\n"
			"  60 Hz with the software renderer (whole frame under 16.7 ms on every page): %s\n"
			"  device lock taken while drawing: %llu times\n",
			accelerated ? "yes" : "no", fast ? "yes" : "no", static_cast<unsigned long long>(accesses));
		require(accesses == 0, "the editor took the device lock, pausing the rendering, while drawing");
		return accelerated;
	}

	// A real window: frames delivered per second, page by page, with the machine running
	bool measureWindow(const md::MachineModel _model, const double _secondsPerPage, const double _scalePercent)
	{
		// The ROM is found as in the plug-in; the config is not saved and no network server opens
		mdJucePlugin::AudioPluginAudioProcessor processor(_model, mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{}, false);
		progress("processor created");
		auto& audioProcessor = static_cast<juce::AudioProcessor&>(processor);
		constexpr double sampleRate = 48000.0;
		constexpr int blockSize = 256;
		audioProcessor.prepareToPlay(sampleRate, blockSize);
		const auto channels = std::max(audioProcessor.getTotalNumInputChannels(), audioProcessor.getTotalNumOutputChannels());

		std::atomic<bool> running{true};
		std::atomic<bool> audioStopped{false};
		std::thread audio([&]
		{
			// The host's audio callback, in real time: one block per block period
			juce::AudioBuffer<float> buffer(channels, blockSize);
			juce::MidiBuffer midi;
			const auto period = std::chrono::duration<double>(blockSize / sampleRate);
			auto next = std::chrono::steady_clock::now();
			uint64_t block = 0;
			while(running.load())
			{
				buffer.clear();
				midi.clear();
				// A note a sixteenth at 120 BPM on the base channel, cycling over the tracks
				if(block % 23 == 0)
					midi.addEvent(juce::MidiMessage::noteOn(1, 36 + static_cast<int>((block / 23) % 16), static_cast<juce::uint8>(100)), 0);
				audioProcessor.processBlock(buffer, midi);
				++block;
				next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
				std::this_thread::sleep_until(next);
			}
			audioStopped = true;
		});

		progress("audio thread running");
		auto* editor = audioProcessor.createEditorIfNeeded();
		progress("editor created");
		require(editor != nullptr, "no editor");
		auto* pluginEditor = dynamic_cast<mdJucePlugin::Editor*>(
			static_cast<mdJucePlugin::PluginEditorState&>(processor.getOrCreateEditorState()).getEditor());
		require(pluginEditor && pluginEditor->getRmlComponent(), "editor has no RmlUi component");
		auto& component = *pluginEditor->getRmlComponent();

		// The window a host opens: the skin's size at the GUI scale (1100 x 606 dp at 100 %).
		// Without a host, the editor keeps its minimum size until something sizes it.
		auto& state = processor.getOrCreateEditorState();
		const auto scale = _scalePercent / 100.0 * state.getRootScale();
		const auto width = juce::roundToInt(state.getWidth() * scale);
		const auto height = juce::roundToInt(state.getHeight() * scale);
		juce::DocumentWindow window("mdEditorFluidityTest", juce::Colours::black, 0);
		window.setUsingNativeTitleBar(true);
		window.setContentNonOwned(editor, true);
		// A window not on the desktop yet gives its content its own minimum size: size it after
		window.setContentComponentSize(width, height);
		window.centreWithSize(window.getWidth(), window.getHeight());
		window.setVisible(true);
		// In front of everything, so the window draws as the user sees it
		window.setAlwaysOnTop(true);
		window.toFront(true);
		std::printf("  ... window shown, %dx%d\n", editor->getWidth(), editor->getHeight());
		std::fflush(stdout);

		std::vector<double> frameTimes;
		frameTimes.reserve(100000);
		std::vector<double> updateCosts;
		double updateStart = 0;
		component.evPreUpdate.addListener([&](juceRmlUi::RmlComponent*) { updateStart = juce::Time::getMillisecondCounterHiRes(); });
		component.evPostUpdate.addListener([&](juceRmlUi::RmlComponent*)
		{
			const auto now = juce::Time::getMillisecondCounterHiRes();
			frameTimes.push_back(now);
			updateCosts.push_back(now - updateStart);
		});

		Mover mover(processor, _model);
		const auto pageList = pages(_model);
		struct Window final : juce::Timer
		{
			std::function<void()> tick;
			void timerCallback() override { tick(); }
		} ticker;
		const auto start = juce::Time::getMillisecondCounterHiRes();
		// Boot: the machine runs a while before the measure, as when a project opens
		const double warmup = 20000.0;
		size_t shownPage = ~size_t{0};
		std::vector<std::pair<size_t, size_t>> pageFrames(pageList.size(), {0, 0});
		bool firmware = false;
		ticker.tick = [&]
		{
			const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
			if(running)
				mover.step(false, false);
			if(elapsed < warmup)
				return;
			const auto page = static_cast<size_t>((elapsed - warmup) / (_secondsPerPage * 1000.0));
			if(page >= pageList.size())
			{
				// The audio thread stops while messages still flow: the machine may wait on them
				running = false;
				if(audioStopped)
					juce::MessageManager::getInstance()->stopDispatchLoop();
				return;
			}
			if(page == 0 && shownPage != 0)
			{
				firmware = processor.getPlugin().withDeviceLocked([](synthLib::Device* _device)
				{
					return dynamic_cast<md::Device*>(_device) != nullptr;
				});
			}
			if(page != shownPage)
			{
				std::printf("  ... page %s\n", pageList[page].name);
				std::fflush(stdout);
				juceRmlUi::RmlInterfaces::ScopedAccess access(component);
				pageList[page].show(*component.getDocument());
				shownPage = page;
				pageFrames[page].first = frameTimes.size();
			}
			pageFrames[page].second = frameTimes.size();
		};
		ticker.startTimerHz(60);
		progress("dispatch loop");
		juce::MessageManager::getInstance()->runDispatchLoop();
		ticker.stopTimer();
		progress("dispatch loop ended");

		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		std::printf("mdEditorFluidityTest %s --window: %dx%d (%.0f %%), renderer %s, frame cap %.0f Hz, audio %d samples at %.0f Hz on its own thread, %s\n",
			name, editor->getWidth(), editor->getHeight(), _scalePercent, juceRmlUi::RenderingTestAccess::renderer(component),
			juceRmlUi::RenderingTestAccess::targetFPS(component), blockSize, sampleRate, firmware ? "firmware running" : "no firmware");
		bool smooth = true;
		for(size_t page = 0; page < pageList.size(); ++page)
		{
			const auto [first, last] = pageFrames[page];
			// Skip the first half second of a page: showing it is one long frame by design
			std::vector<double> intervals;
			double begin = 0, end = 0;
			for(size_t index = first + 1; index < last && index < frameTimes.size(); ++index)
			{
				if(frameTimes[index] - frameTimes[first] < 500.0)
					continue;
				if(begin == 0)
					begin = frameTimes[index - 1];
				end = frameTimes[index];
				intervals.push_back(frameTimes[index] - frameTimes[index - 1]);
			}
			const auto fps = end > begin ? 1000.0 * static_cast<double>(intervals.size()) / (end - begin) : 0.0;
			std::vector<double> costs(updateCosts.begin() + static_cast<std::ptrdiff_t>(std::min(first, updateCosts.size())),
				updateCosts.begin() + static_cast<std::ptrdiff_t>(std::min(last, updateCosts.size())));
			const auto i = stats(intervals);
			const auto c = stats(costs);
			std::printf("  %-10s %5.1f frames/s  interval p50 %5.1f ms  p95 %5.1f  max %6.1f  update p95 %4.1f ms\n",
				pageList[page].name, fps, i.p50, i.p95, i.max, c.p95);
			smooth &= fps >= 59.0;
		}
		std::fflush(stdout);

		audio.join();
		progress("closing the editor");
		window.clearContentComponent();
		delete editor;
		progress("releasing the processor");
		audioProcessor.releaseResources();
		progress("closed");
		return smooth;
	}

	// Editing while the machine plays, as in a host: the plug-in with a fresh config (two blocks of latency,
	// the machine rendering on its own thread, the firmware when a ROM is found), no window. An audio thread
	// calls processBlock in real time; the message thread runs the controller and editor timers, starts the
	// sequencer from the panel, shows JOUER and then sets or clears a trig every _interval seconds, as a click
	// does. Reported before the first click and in the two seconds after each: the host callback's time per
	// block (what a DAW's CPU meter shows), the render thread's time per block, and the longest wait between
	// two ticks of a 60 Hz timer on the message thread (an interface freeze). With _library, BIBLIO's reading
	// of every stored Kit and pattern instead of the clicks: how long it takes, and the same figures meanwhile.
	bool measureEditLoad(const md::MachineModel _model, const double _interval, const int _clicks, const bool _library)
	{
		mdJucePlugin::AudioPluginAudioProcessor processor(_model, mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{}, false);
		auto& audioProcessor = static_cast<juce::AudioProcessor&>(processor);
		constexpr double sampleRate = 48000.0;
		constexpr int blockSize = 256;
		audioProcessor.prepareToPlay(sampleRate, blockSize);
		const auto channels = std::max(audioProcessor.getTotalNumInputChannels(), audioProcessor.getTotalNumOutputChannels());
		const double periodMs = 1000.0 * blockSize / sampleRate;
		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";

		const auto origin = std::chrono::steady_clock::now();
		const auto msAt = [&](const std::chrono::steady_clock::time_point _time)
		{
			return std::chrono::duration<double, std::milli>(_time - origin).count();
		};
		struct Sample { double at; double ms; };
		std::vector<Sample> callbacks;
		callbacks.reserve(1 << 20);
		std::atomic<bool> running{true};
		std::atomic<bool> audioStopped{false};
		// Callbacks that started more than half a period after their time: a host's audio thread is never
		// that late, so the measure is not one of the plug-in
		uint64_t lateCallbacks = 0;
		double latestCallbackMs = 0;
		std::thread audio([&]
		{
			juce::AudioBuffer<float> buffer(channels, blockSize);
			juce::MidiBuffer midi;
			const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(blockSize / sampleRate));
			auto next = std::chrono::steady_clock::now();
			while(running.load())
			{
				buffer.clear();
				midi.clear();
				const auto start = std::chrono::steady_clock::now();
				const auto lateMs = std::chrono::duration<double, std::milli>(start - next).count();
				lateCallbacks += lateMs > periodMs / 2 ? 1 : 0;
				latestCallbackMs = std::max(latestCallbackMs, lateMs);
				audioProcessor.processBlock(buffer, midi);
				const auto end = std::chrono::steady_clock::now();
				callbacks.push_back({msAt(start), std::chrono::duration<double, std::milli>(end - start).count()});
				next += period;
				// No sleep: a process without a visible window may get the default 15.6 ms timer resolution,
				// and a sleep then overshoots a whole period. Spin, as a host's audio thread waits on its device.
				while(std::chrono::steady_clock::now() < next)
					std::this_thread::yield();
			}
			audioStopped = true;
		});

		// No disclaimer box on the desktop: this config is never saved
		processor.getConfig().setValue("disclaimerSeen", true);
		auto& editorState = static_cast<mdJucePlugin::PluginEditorState&>(processor.getOrCreateEditorState());
		auto* editor = dynamic_cast<mdJucePlugin::Editor*>(editorState.getEditor());
		require(editor != nullptr && editor->getRmlComponent() && editor->getRmlComponent()->getDocument(), "no editor");
		auto& component = *editor->getRmlComponent();
		auto& controller = dynamic_cast<mdJucePlugin::Controller&>(processor.getController());

		const auto panel = [&](const md::PanelControl _control, const bool _down)
		{
			processor.getPlugin().withDeviceLocked([&](synthLib::Device* _device)
			{
				auto* device = dynamic_cast<md::Device*>(_device);
				if(!device)
					return;
				const auto packet = md::panelPacket(_model, _control);
				if(packet)
					(void)device->getHardware().trySendPanelEvent(packet->row, _down ? packet->mask : uint8_t{0});
			});
		};
		const auto patternKnown = [&]
		{
			return _model == md::MachineModel::Monomachine ? controller.getMmPattern().has_value()
				: controller.getPattern().has_value();
		};

		enum class Phase { Boot, Start, Pattern, Baseline, Clicks, Library, Done };
		Phase phase = Phase::Boot;
		double phaseSince = 0;
		std::vector<double> ticks;
		ticks.reserve(1 << 16);
		std::vector<double> clickTimes, clickCosts;
		bool firmware = false;
		int clicks = 0;
		double baselineStart = 0;
		double patternAsked = 0;
		bool playingAtBaseline = false;
		// The library reading: started, Kits done, ended (0 until then), and when progress was last printed
		double libraryStart = 0, libraryKitsDone = 0, libraryEnd = 0, libraryNote = 0;
		// A fresh config may prepare the factory flash and reboot the machine in process: the measure waits
		// until the machine has kept its epoch for five seconds
		uint64_t epoch = ~uint64_t{0};
		double epochSince = 0;
		// The render thread's silent blocks so far, read without the device lock (its counters are atomic)
		const md::AsyncRender* async = nullptr;
		const auto silentBlocks = [&] { return async ? async->missedBlocks() + async->droppedBlocks() : 0; };
		uint64_t silentAtBaseline = 0;
		std::vector<uint64_t> silentAtClick;
		uint64_t silentAtLibrary = 0, silentAfterLibrary = 0;
		struct Ticker final : juce::Timer
		{
			std::function<void()> tick;
			void timerCallback() override { tick(); }
		} ticker;
		ticker.tick = [&]
		{
			const auto now = msAt(std::chrono::steady_clock::now());
			ticks.push_back(now);
			const auto inPhase = now - phaseSince;
			const auto next = [&](const Phase _phase)
			{
				phase = _phase;
				phaseSince = now;
			};
			switch(phase)
			{
			case Phase::Boot:
				if(const auto status = processor.getLiveDevice().status(); status && status->hardwareEpoch != epoch)
				{
					epoch = status->hardwareEpoch;
					epochSince = now;
				}
				if(const auto status = processor.getLiveDevice().status(); status && status->firmwareReady
					&& !status->factoryInitializationExpected && !status->restorePending && now - epochSince > 5000 && inPhase > 3000)
				{
					firmware = true;
					processor.getPlugin().withDeviceLocked([&](synthLib::Device* _device)
					{
						if(auto* device = dynamic_cast<md::Device*>(_device))
							async = device->asyncRender();
					});
					progress("firmware ready: showing JOUER, pressing PLAY");
					juceRmlUi::RmlInterfaces::ScopedAccess access(component);
					element(*component.getDocument(), "mdViewEditor")->Click();
					tabButton(*component.getDocument(), "mdEdit", "2")->Click();
					panel(md::PanelControl::Play, true);
					next(Phase::Start);
				}
				else if(inPhase > 90000)
				{
					progress("no firmware after 90 s");
					next(Phase::Done);
				}
				break;
			case Phase::Start:
				if(inPhase > 100)
				{
					panel(md::PanelControl::Play, false);
					next(Phase::Pattern);
				}
				break;
			case Phase::Pattern:
				if(patternKnown() && inPhase > 2000)
				{
					progress("pattern read: baseline");
					baselineStart = now;
					silentAtBaseline = silentBlocks();
					playingAtBaseline = controller.getPlayingStep().has_value();
					next(Phase::Baseline);
				}
				else if(inPhase > 30000)
				{
					progress("pattern never read");
					next(Phase::Done);
				}
				else if(now - patternAsked > 2000.0)
				{
					// As JOUER asks while it has none: the Monomachine's view asks only while drawn, which it is not here
					patternAsked = now;
					(void)controller.requestPattern();
				}
				break;
			case Phase::Baseline:
				if(inPhase <= _interval * 1000.0)
					break;
				if(!_library)
					next(Phase::Clicks);
				else if(controller.readLibrary())
				{
					progress("reading the library");
					libraryStart = now;
					libraryNote = now;
					silentAtLibrary = silentBlocks();
					next(Phase::Library);
				}
				break;
			case Phase::Library:
			{
				const auto done = controller.getLibraryProgress();
				if(!libraryKitsDone && done >= controller.getKitLibrarySize())
					libraryKitsDone = now;
				if(controller.isLibraryRead())
				{
					libraryEnd = now;
					silentAfterLibrary = silentBlocks();
					next(Phase::Done);
				}
				else if(inPhase > 900000)
				{
					progress("library not read after 15 min");
					next(Phase::Done);
				}
				else if(now - libraryNote > 30000)
				{
					libraryNote = now;
					std::printf("  ... %zu of %zu read\n", done,
						controller.getKitLibrarySize() + mdJucePlugin::Controller::PatternLibrarySize);
					std::fflush(stdout);
				}
				break;
			}
			case Phase::Clicks:
				if(clicks >= _clicks)
				{
					if(inPhase > _interval * 1000.0)
					{
						silentAtClick.push_back(silentBlocks());
						next(Phase::Done);
					}
					break;
				}
				if(clicks == 0 || inPhase > _interval * 1000.0)
				{
					juceRmlUi::RmlInterfaces::ScopedAccess access(component);
					const auto id = std::string(_model == md::MachineModel::Monomachine ? "mmPlayStep" : "mdPlayStep")
						+ "0_" + std::to_string(2 + 4 * (clicks % 4));
					auto* cell = element(*component.getDocument(), id);
					silentAtClick.push_back(silentBlocks());
					const auto start = std::chrono::steady_clock::now();
					cell->Click();
					clickCosts.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
					clickTimes.push_back(msAt(start));
					++clicks;
					phaseSince = now;
				}
				break;
			case Phase::Done:
				running = false;
				if(audioStopped)
					juce::MessageManager::getInstance()->stopDispatchLoop();
				break;
			}
		};
		ticker.startTimerHz(60);
		progress("dispatch loop");
		juce::MessageManager::getInstance()->runDispatchLoop();
		ticker.stopTimer();
		audio.join();

		// What the render thread did, per job: render start and duration
		std::vector<Sample> renders;
		md::AsyncRender::Stats asyncStats{};
		uint64_t late = 0, missed = 0, dropped = 0;
		processor.getPlugin().withDeviceLocked([&](synthLib::Device* _device)
		{
			auto* device = dynamic_cast<md::Device*>(_device);
			if(!device || !device->asyncRender())
				return;
			const auto* async = device->asyncRender();
			asyncStats = async->stats();
			late = async->lateBlocks();
			missed = async->missedBlocks();
			dropped = async->droppedBlocks();
			const auto jobs = std::min<uint64_t>(asyncStats.jobs, md::AsyncRender::JobTimeCount);
			const auto& timeline = async->timeline();
			const auto originNs = std::chrono::duration_cast<std::chrono::nanoseconds>(origin.time_since_epoch()).count();
			for(uint64_t j = asyncStats.jobs - jobs; j < asyncStats.jobs; ++j)
			{
				const auto& t = timeline[j % md::AsyncRender::JobTimeCount];
				if(t.renderStart && t.renderEnd > t.renderStart)
					renders.push_back({static_cast<double>(t.renderStart - originNs) / 1e6, static_cast<double>(t.renderEnd - t.renderStart) / 1e6});
			}
		});

		std::printf("mdEditorFluidityTest %s %s: %d blocks at %.0f Hz (%.2f ms), %s, render %s, playhead %s; %d clicks %.1f s apart\n",
			name, _library ? "--library-load" : "--edit-load", blockSize, sampleRate, periodMs,
			firmware ? "firmware running" : "no firmware", renders.empty() ? "on the host's thread" : "on its own thread",
			playingAtBaseline ? "moving" : "not seen moving", clicks, _interval);
		const auto window = [&](const std::vector<Sample>& _samples, const double _from, const double _to)
		{
			std::vector<double> values;
			uint32_t over = 0;
			for(const auto& s : _samples)
			{
				if(s.at < _from || s.at >= _to)
					continue;
				values.push_back(s.ms);
				over += s.ms > periodMs ? 1 : 0;
			}
			return std::make_pair(stats(values), over);
		};
		const auto longestGap = [&](const double _from, const double _to)
		{
			double gap = 0;
			for(size_t i = 1; i < ticks.size(); ++i)
			{
				if(ticks[i] >= _from && ticks[i - 1] < _to)
					gap = std::max(gap, ticks[i] - ticks[i - 1]);
			}
			return gap;
		};
		const auto report = [&](const char* _label, const double _from, const double _to, const uint64_t _silent)
		{
			const auto [host, hostOver] = window(callbacks, _from, _to);
			const auto [render, renderOver] = window(renders, _from, _to);
			std::printf("  %-14s host callback p50 %5.2f p99 %5.2f max %6.2f ms (%u over the period) | render p50 %5.2f p99 %5.2f"
				" max %6.2f ms (%u over) | %3llu blocks silent | message thread longest gap %6.1f ms\n", _label, host.p50,
				host.p99, host.max, hostOver, render.p50, render.p99, render.max, renderOver,
				static_cast<unsigned long long>(_silent), longestGap(_from, _to));
		};
		if(!clickTimes.empty() && silentAtClick.size() == clickTimes.size() + 1)
		{
			report("before clicks", baselineStart, clickTimes.front(), silentAtClick.front() - silentAtBaseline);
			for(size_t c = 0; c < clickTimes.size(); ++c)
			{
				char label[32];
				std::snprintf(label, sizeof(label), "click %zu (%.1f ms)", c + 1, clickCosts[c]);
				report(label, clickTimes[c], c + 1 < clickTimes.size() ? clickTimes[c + 1] : clickTimes[c] + _interval * 1000.0,
					silentAtClick[c + 1] - silentAtClick[c]);
			}
			// The slowest renders after the clicks, when they happened
			std::vector<Sample> slow;
			for(const auto& r : renders)
			{
				if(r.at >= clickTimes.front() && r.ms > periodMs)
					slow.push_back(r);
			}
			std::sort(slow.begin(), slow.end(), [](const Sample& _a, const Sample& _b) { return _a.ms > _b.ms; });
			std::string list;
			for(size_t i = 0; i < std::min<size_t>(slow.size(), 12); ++i)
			{
				// Time after the latest click before it
				double click = clickTimes.front();
				for(const auto t : clickTimes)
					if(t <= slow[i].at)
						click = t;
				char text[48];
				std::snprintf(text, sizeof(text), " %.1f ms @+%.0f", slow[i].ms, slow[i].at - click);
				list += text;
			}
			std::printf("  slowest renders after the clicks (duration @ms after the click):%s\n", list.c_str());
		}
		if(libraryEnd > 0)
		{
			report("before reading", baselineStart, libraryStart, silentAtLibrary - silentAtBaseline);
			report("library read", libraryStart, libraryEnd, silentAfterLibrary - silentAtLibrary);
			const auto kits = controller.getKitLibrarySize();
			size_t kitsRead = 0, patternsRead = 0;
			for(size_t slot = 0; slot < kits; ++slot)
				kitsRead += controller.getLibraryKit(static_cast<uint8_t>(slot)).value_or(mdJucePlugin::Controller::LibraryKit{}).read;
			for(size_t slot = 0; slot < mdJucePlugin::Controller::PatternLibrarySize; ++slot)
				patternsRead += controller.getLibraryPattern(static_cast<uint8_t>(slot)).value_or(mdJucePlugin::Controller::LibraryPattern{}).read;
			const auto kitsEnd = libraryKitsDone ? libraryKitsDone : libraryEnd;
			std::printf("  library: %zu of %zu Kits read in %.1f s, %zu of %zu patterns in %.1f s, %.1f s in all\n",
				kitsRead, kits, (kitsEnd - libraryStart) / 1000.0, patternsRead, mdJucePlugin::Controller::PatternLibrarySize,
				(libraryEnd - kitsEnd) / 1000.0, (libraryEnd - libraryStart) / 1000.0);
		}
		std::printf("  render thread: %llu jobs, late %llu, played as silence %llu, dropped %llu\n",
			static_cast<unsigned long long>(asyncStats.jobs), static_cast<unsigned long long>(late),
			static_cast<unsigned long long>(missed), static_cast<unsigned long long>(dropped));
		std::printf("  audio thread: %llu of %zu callbacks over half a period late, the latest by %.1f ms\n",
			static_cast<unsigned long long>(lateCallbacks), callbacks.size(), latestCallbackMs);
		std::fflush(stdout);
		audioProcessor.releaseResources();
		return missed == 0 && (!_library || libraryEnd > 0);
	}
}

int main(const int _argc, const char* const* _argv)
{
	// Where a crash happens, with the symbols the build has
	juce::SystemStats::setApplicationCrashHandler([](void*)
	{
		std::fflush(stdout);
		std::fprintf(stderr, "mdEditorFluidityTest: CRASH\n%s\n", juce::SystemStats::getStackBacktrace().toRawUTF8());
		std::fflush(stderr);
	});
	try
	{
		juce::ScopedJuceInitialiser_GUI gui;
#if defined(MD_EDITOR_FLUIDITY_TEST_MM)
		constexpr auto model = md::MachineModel::Monomachine;
#else
		constexpr auto model = md::MachineModel::Machinedrum;
#endif
		bool window = false;
		bool editLoad = false;
		bool libraryLoad = false;
		double secondsPerPage = 4.0;
		double scalePercent = 100.0;
		double clickInterval = 2.0;
		int clickCount = 6;
		for(int argument = 1; argument < _argc; ++argument)
		{
			const std::string value(_argv[argument]);
			if(value == "--window")
			{
				window = true;
				if(argument + 1 < _argc && std::atof(_argv[argument + 1]) > 0)
					secondsPerPage = std::atof(_argv[++argument]);
			}
			else if(value == "--edit-load")
			{
				editLoad = true;
				if(argument + 1 < _argc && std::atof(_argv[argument + 1]) > 0)
					clickInterval = std::atof(_argv[++argument]);
				if(argument + 1 < _argc && std::atoi(_argv[argument + 1]) > 0)
					clickCount = std::atoi(_argv[++argument]);
			}
			else if(value == "--library-load")
				libraryLoad = true;
			else if(value == "--scale" && argument + 1 < _argc && std::atof(_argv[argument + 1]) > 0)
				scalePercent = std::atof(_argv[++argument]);
		}
		if(editLoad || libraryLoad)
		{
			const bool clean = measureEditLoad(model, clickInterval, libraryLoad ? 0 : clickCount, libraryLoad);
			std::printf("mdEditorFluidityTest: %s\n", clean ? "no block played as silence" : "blocks played as silence");
			std::fflush(stdout);
			// As with --window: the fresh config's disclaimer may be open on its own thread
			std::_Exit(clean ? 0 : 1);
		}
		const bool ok = window ? measureWindow(model, secondsPerPage, scalePercent) : measureFrameCost(model, scalePercent);
		const auto failed = !ok && failuresRequired();
		std::printf("mdEditorFluidityTest: %s\n", failed ? "FAIL below 60 frames per second"
			: ok ? "PASS" : "below 60 frames per second (not required)");
		std::fflush(stdout);
		// A real window shows the plug-in's disclaimer (a native message box on its own thread)
		// with a fresh config; ending JUCE while it is open crashes in it. The measure is done.
		if(window)
			std::_Exit(failed ? 1 : 0);
		return failed ? 1 : 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdEditorFluidityTest: FAIL %s\n", _error.what());
		return 1;
	}
}
