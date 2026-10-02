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
#include "mdMasterEffectsView.h"
#include "mdOutputMetersView.h"
#include "mdSystemPage.h"

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

	// Software renderer: the cost of each frame, page by page
	bool measureFrameCost(const md::MachineModel _model)
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
		juce::Image image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
		Mover mover(processor, _model);

		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		std::printf("mdEditorFluidityTest %s: software renderer, %dx%d, one frame = presentation timer + RmlUi update + rasterized frame\n",
			name, component->getWidth(), component->getHeight());

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
		double secondsPerPage = 4.0;
		double scalePercent = 100.0;
		for(int argument = 1; argument < _argc; ++argument)
		{
			const std::string value(_argv[argument]);
			if(value == "--window")
			{
				window = true;
				if(argument + 1 < _argc && std::atof(_argv[argument + 1]) > 0)
					secondsPerPage = std::atof(_argv[++argument]);
			}
			else if(value == "--scale" && argument + 1 < _argc && std::atof(_argv[argument + 1]) > 0)
				scalePercent = std::atof(_argv[++argument]);
		}
		const bool ok = window ? measureWindow(model, secondsPerPage, scalePercent) : measureFrameCost(model);
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
