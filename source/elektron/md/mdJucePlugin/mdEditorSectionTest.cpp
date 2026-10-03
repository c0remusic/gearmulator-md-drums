// Loads the real Machinedrum skin (or the Monomachine skin with MD_EDITOR_SECTION_TEST_MM) headless
// and checks the editor: window size, the FACE AVANT / ÉDITEUR switch and the stacked layout of a
// tall window, dp geometry of the grid, parameter binding and tabs.

#include "mdEditor.h"
#include "mdPluginEditorState.h"
#include "mdPluginProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlElemValue.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "jucePluginLib/controller.h"
#include "mdAutomationTestSupport.h"
#include "mdController.h"
#include "mdCurveView.h"
#include "mdKitPatternScreen.h"
#include "mdLfoView.h"
#include "mdLibraryView.h"
#include "mdMachinePicker.h"
#include "mdMasterEffectsView.h"
#include "mdOutputMetersView.h"
#include "mdPatternView.h"
#include "mdMmPatternView.h"
#include "mdChainView.h"
#include "mdStepColumns.h"
#include "mdStepGrid.h"
#include "mdSystemPage.h"
#include "mdTrackActivity.h"
#include "mdTrackRoutingView.h"
#include "mdUnreadValues.h"
#include "mdLib/mdmachines.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	// Lets the controller accept a pattern read without booted firmware.
	struct ControllerAutomationTestAccess
	{
		static void useSyntheticFirmware(Controller& _controller)
		{
			_controller.m_syntheticFirmwareReadyForTests = true;
		}

		// The library Kit or pattern asked for is not answered in time: the controller timer skips it
		static void expireLibraryRequest(Controller& _controller)
		{
			_controller.m_libraryRequestMs = Controller::milliseconds() - 4001;
			_controller.onControllerTimer();
		}

		// A copy's source or read back is not answered in time, on either model: the controller timer fails it
		static void expirePatternCopy(Controller& _controller)
		{
			_controller.m_patternCopyMs = Controller::milliseconds() - Controller::MmWriteTimeoutMilliseconds - 1;
			_controller.onControllerTimer();
		}

		// The edits pause: the controller timer writes the pattern sendPatternSoon() waits with
		static void pauseEdits(Controller& _controller)
		{
			const std::lock_guard lock(_controller.m_synchronizationLock);
			_controller.servicePatternWrite(Controller::milliseconds() + Controller::PatternWritePauseMilliseconds);
		}
		static bool patternWriteWaiting(const Controller& _controller)
		{
			return _controller.m_patternWriteFirstMs != 0;
		}

		// The sequencer plays _step, or is stopped (nullopt), as the machine's RAM would say
		static void setPlayingStep(Controller& _controller, const std::optional<uint8_t> _step)
		{
			_controller.m_syntheticPlayingStepForTests = _step;
		}

		// The Kit the machine's RAM holds (none: no live Kit), read as the controller timer reads it: past
		// the grace an assignment gets, three reads a poll and a second of emulation apart, two to agree
		// and three to see no CC sent to the track in between
		static void readLiveKit(Controller& _controller, const std::optional<md::LiveKit>& _kit,
			const std::function<void()>& _afterFirstRead = {})
		{
			const std::lock_guard lock(_controller.m_synchronizationLock);
			_controller.m_previousLiveKit.reset();
			_controller.m_liveKitPollMs = 0;
			const auto now = Controller::milliseconds() + Controller::AssignmentGraceMilliseconds;
			for(uint64_t read = 0; read < 3; ++read)
			{
				if(read == 1 && _afterFirstRead)
					_afterFirstRead();
				auto kit = _kit;
				if(kit)
					kit->frame = (read + 1) * md::g_samplerate;
				_controller.m_syntheticLiveKitForTests = kit;
				_controller.serviceLiveKit(now + read * Controller::LiveKitPollMilliseconds);
			}
		}

		// The tracks' machines, as an applied Kit dump sets them
		static std::vector<uint16_t> machines(const Controller& _controller)
		{
			std::vector<uint16_t> result;
			for(uint8_t track = 0; track < _controller.getPartCount(); ++track)
				result.push_back(_controller.getTrackMachine(track));
			return result;
		}
		static void setMachines(Controller& _controller, const std::vector<uint16_t>& _machines)
		{
			_controller.storeKitMachines(_machines, true);
		}
	};

	// What the presentation timer does for the top bar's screen; no timer runs here.
	struct EditorIdentityTestAccess
	{
		static bool updateScreen(Editor& _editor)
		{
			return _editor.m_kitPatternScreen && _editor.m_kitPatternScreen->update();
		}

		static bool updateUnread(Editor& _editor)
		{
			return _editor.m_unreadValues && _editor.m_unreadValues->update();
		}

		static bool updateMeters(Editor& _editor, const double _now)
		{
			return _editor.m_outputMetersView && _editor.m_outputMetersView->update(_now);
		}

		static bool updateSystem(Editor& _editor, const double _now)
		{
			return _editor.m_systemPage && _editor.m_systemPage->update(_now);
		}

		static bool updateActivity(Editor& _editor, const double _now)
		{
			return _editor.m_trackActivity && _editor.m_trackActivity->update(_now);
		}

		static const OutputMetersView& meters(const Editor& _editor) { return *_editor.m_outputMetersView; }
		static const PatternView& pattern(const Editor& _editor) { return *_editor.m_patternView; }
		// What the mouse does in the Machinedrum's lane
		static PatternView& editPattern(Editor& _editor) { return *_editor.m_patternView; }
		static const MmPatternView& mmPattern(const Editor& _editor) { return *_editor.m_mmPatternView; }
		// What clicks in the Monomachine's grid and roll, and its COPIER VERS menu, do
		static MmPatternView& editMmPattern(Editor& _editor) { return *_editor.m_mmPatternView; }
		static const StepGrid& steps(const Editor& _editor) { return *_editor.m_stepGrid; }
		// A slot chosen in COPIER VERS's menu
		static void copyPatternTo(Editor& _editor, const uint8_t _slot) { _editor.m_patternView->copyTo(_slot); }

		// Every editor component the presentation timer refreshes, for the snapshots
		static void present(Editor& _editor)
		{
			updateMeters(_editor, juce::Time::getMillisecondCounterHiRes());
			updateSystem(_editor, juce::Time::getMillisecondCounterHiRes());
			updateActivity(_editor, juce::Time::getMillisecondCounterHiRes());
			if(_editor.m_lfoView)
				_editor.m_lfoView->update();
			if(_editor.m_patternView)
				_editor.m_patternView->update();
			if(_editor.m_mmPatternView)
				_editor.m_mmPatternView->update(juce::Time::getMillisecondCounterHiRes());
			if(_editor.m_chainView)
				_editor.m_chainView->update();
			if(_editor.m_libraryView)
				_editor.m_libraryView->update(juce::Time::getMillisecondCounterHiRes());
			if(_editor.m_machinePicker)
				_editor.m_machinePicker->update();
			if(_editor.m_stepGrid)
				_editor.m_stepGrid->update(juce::Time::getMillisecondCounterHiRes());
			if(_editor.m_curveView)
				_editor.m_curveView->update();
			if(_editor.m_masterEffectsView)
				_editor.m_masterEffectsView->update();
			if(_editor.m_trackRoutingView)
				_editor.m_trackRoutingView->update();
			updateScreen(_editor);
			updateUnread(_editor);
		}
	};
}

namespace juceRmlUi
{
	// Same friend hook as mdPanelRenderingTest: runs one RmlUi frame so paint() has geometry.
	struct RenderingTestAccess
	{
		static void update(RmlComponent& _component) { _component.update(); }
	};
}

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Rml::Element& element(Rml::ElementDocument& _doc, const std::string& _id)
	{
		auto* e = _doc.GetElementById(_id);
		require(e != nullptr, "missing element " + _id);
		return *e;
	}

	void requireRect(Rml::Element& _e, const float _x, const float _y, const float _w, const float _h, const std::string& _label)
	{
		const auto pos = _e.GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto size = _e.GetBox().GetSize(Rml::BoxArea::Border);
		const auto near = [](const float _a, const float _b) { return std::fabs(_a - _b) < 0.5f; };
		require(near(pos.x, _x) && near(pos.y, _y) && near(size.x, _w) && near(size.y, _h),
			_label + ": expected " + std::to_string(_x) + "," + std::to_string(_y) + " " + std::to_string(_w) + "x" + std::to_string(_h)
			+ ", got " + std::to_string(pos.x) + "," + std::to_string(pos.y) + " " + std::to_string(size.x) + "x" + std::to_string(size.y));
	}

	Rml::Element& tabButton(Rml::ElementDocument& _doc, const std::string& _group, const std::string& _index)
	{
		Rml::ElementList buttons;
		_doc.GetElementsByTagName(buttons, "button");
		for(auto* b : buttons)
		{
			const auto* g = b->GetAttribute("tabgroup");
			const auto* i = b->GetAttribute("tabbutton");
			if(g && i && g->Get<Rml::String>(b->GetCoreInstance()) == _group && i->Get<Rml::String>(b->GetCoreInstance()) == _index)
				return *b;
		}
		throw std::runtime_error("no tab button " + _group + "/" + _index);
	}

	bool visible(Rml::Element& _e)
	{
		return _e.IsVisible(true);
	}

	void collectBound(Rml::Element& _e, std::vector<Rml::Element*>& _out)
	{
		if(_e.GetAttribute("param"))
			_out.push_back(&_e);
		for(int i = 0; i < _e.GetNumChildren(); ++i)
			collectBound(*_e.GetChild(i), _out);
	}

	// The blocks of a view tile it: rows whose blocks share one height, each row
	// 12 columns wide (1068 dp with its gutters) and the next row 12 dp below.
	void requireTiled(Rml::Element& _view, const std::string& _label)
	{
		Rml::ElementList blocks;
		_view.GetElementsByClassName(blocks, "mdEdBlock");
		require(!blocks.empty(), _label + ": no blocks");
		std::map<int, std::vector<Rml::Element*>> rows;
		for(auto* b : blocks)
			if(b->IsVisible(true))
				rows[static_cast<int>(std::lround(b->GetAbsoluteOffset(Rml::BoxArea::Border).y))].push_back(b);
		int nextTop = rows.begin()->first;
		for(const auto& [top, row] : rows)
		{
			require(top == nextTop, _label + ": gap or overlap above the row at y " + std::to_string(top));
			const auto height = std::lround(row.front()->GetBox().GetSize(Rml::BoxArea::Border).y);
			long width = -12;
			for(auto* b : row)
			{
				require(std::lround(b->GetBox().GetSize(Rml::BoxArea::Border).y) == height,
					_label + ": blocks of the row at y " + std::to_string(top) + " differ in height");
				width += std::lround(b->GetBox().GetSize(Rml::BoxArea::Border).x) + 12;
			}
			require(width == 1068, _label + ": row at y " + std::to_string(top) + " does not span the 12 columns");
			nextTop = top + static_cast<int>(height) + 12;
		}
	}

	struct Block
	{
		const char* id;
		float x, y, w, h;
	};

	// Tops of the SON blocks are relative to the page, which starts under the top bar (36 dp)
	// and the track strip (32 dp) when the editor is shown. Every view fits the 538 dp page.
	constexpr float g_pageTop = 36 + 32;
	constexpr float g_pageHeight = 538;
#if defined(MD_EDITOR_SECTION_TEST_MM)
	constexpr auto g_model = md::MachineModel::Monomachine;
	// Picker: family 1 is SID, whose only machine is SID-6581 (3); family 0 (GND) holds GND-SIN (1).
	constexpr uint16_t g_pickMachine = 3;
	constexpr const char* g_pickName = "SID-6581";
	constexpr const char* g_pickFamily = "SID";
	constexpr uint16_t g_otherFamilyMachine = 1;
	// SON's first synthesis control as the skin names it and as SID-6581 does; GND-SIN uses only TUNE
	constexpr const char* g_firstSynthesis = "SynthesisA";
	constexpr const char* g_genericParameterName = "SYN A";
	constexpr const char* g_pickParameterName = "PW";
	constexpr const char* g_otherFamilyUnused = "SynthesisA";
	constexpr const char* g_otherFamilyUsed = "SynthesisH";
	constexpr float g_pickerTop = 12 + 64 + 4;
	constexpr const char* g_name = "mmEditorSectionTest";
	constexpr int g_trackCount = 6;
	constexpr float g_trackTabPitch = 180, g_trackTabWidth = 168;
	// SON: 54 knobs + 2 faders; MIX: 3 values, a level fader and a mute LED per track
	constexpr size_t g_boundElements = 54 + 2 + 6 * 5;
	constexpr Block g_blocks[] = {
		{"mdEdMachine", 16, 12, 1068, 64},
		{"mdEdOsc", 16, 88, 348, 208},
		{"mdEdAmp", 376, 88, 348, 208},
		{"mdEdFilter", 736, 88, 348, 208},
		{"mdEdEffects", 16, 308, 348, 208},
		{"mdEdLfo", 376, 308, 708, 208},
	};
#else
	constexpr auto g_model = md::MachineModel::Machinedrum;
	// Picker: family 1 is TRX; TRX-BD is 16. Family 0 (GND) holds GND-SN (1).
	constexpr uint16_t g_pickMachine = 16;
	constexpr const char* g_pickName = "TRX-BD";
	constexpr const char* g_pickFamily = "TRX";
	constexpr uint16_t g_otherFamilyMachine = 1;
	// SON's first synthesis control as the skin names it and as TRX-BD does; GND-SN uses 1 to 4
	constexpr const char* g_firstSynthesis = "MachineParameter1";
	constexpr const char* g_genericParameterName = "PARAM 1";
	constexpr const char* g_pickParameterName = "PTCH";
	constexpr const char* g_otherFamilyUnused = "MachineParameter5";
	constexpr const char* g_otherFamilyUsed = "MachineParameter1";
	constexpr float g_pickerTop = 12 + 64 + 4;
	constexpr const char* g_name = "mdEditorSectionTest";
	constexpr int g_trackCount = 16;
	constexpr float g_trackTabPitch = 63, g_trackTabWidth = 60;
	// SON: 21 knobs + 3 faders; MIX: 4 values, a level fader and a mute LED per track
	constexpr size_t g_boundElements = 21 + 3 + 16 * 6;
	constexpr Block g_blocks[] = {
		// rows whose blocks share one height
		{"mdEdSteps", 16, 12, 708, 64},
		{"mdEdMachine", 736, 12, 348, 64},
		{"mdEdSource", 16, 88, 348, 208},
		{"mdEdFilter", 376, 88, 258, 208},
		{"mdEdColour", 646, 88, 168, 208},
		{"mdEdMix", 826, 88, 258, 208},
		{"mdEdLfo", 16, 308, 1068, 124},
	};
#endif
}

int main()
{
	try
	{
		juce::ScopedJuceInitialiser_GUI gui;

		mdJucePlugin::AudioPluginAudioProcessor processor(g_model,
			mdJucePlugin::AudioPluginAudioProcessor::EphemeralConfig{std::string{}}, false);
		processor.setForceSoftwareRendererForSession(true);

		auto& editorState = static_cast<mdJucePlugin::PluginEditorState&>(processor.getOrCreateEditorState());
		auto* editor = dynamic_cast<mdJucePlugin::Editor*>(editorState.getEditor());
		require(editor != nullptr, "processor did not create the editor");
		auto* component = editor->getRmlComponent();
		require(component && component->getContext() && component->getDocument(), "editor has no RmlUi document");

		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		auto& context = *component->getContext();
		auto& doc = *component->getDocument();
		context.Update();

		// Optional: MD_EDITOR_TEST_PNG=<prefix> writes snapshots of the editor for review.
		const char* png = std::getenv("MD_EDITOR_TEST_PNG");
		juceRmlUi::LookAndFeel lookAndFeel;
		const auto snap = [&](const std::string& _suffix)
		{
			if(!png)
				return;
			// What the controller timer and JUCE's asynchronous Value listeners bring in the
			// plug-in: parameter values flushed to the UI, then the knobs and texts that show them
			for(const auto& [address, parameters] : processor.getController().getExposedParameters())
			{
				for(auto* parameter : parameters)
				{
					parameter->flushRealtimeValueToUi();
					parameter->getValueObject().getValueSource().sendChangeMessage(true);
				}
			}
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			component->setLookAndFeel(&lookAndFeel);
			juce::Image image;
			for(int frame = 0; frame < 6; ++frame)
			{
				juceRmlUi::RenderingTestAccess::update(*component);
				image = juce::Image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
				lookAndFeel.getCurrentImage() = image;
				juce::Graphics g(image);
				component->paint(g);
			}
			component->setLookAndFeel(nullptr);
			juce::FileOutputStream out(juce::File(std::string(png) + _suffix + ".png"));
			out.setPosition(0);
			out.truncate();
			juce::PNGImageFormat().writeImageToStream(image, out);
		};

		// window: 1100 x 606 dp at scale 1, a top bar over the front panel; the height follows the window
		const auto docSize = component->getDocumentSize();
		require(docSize.x == 1100 && docSize.y == 606,
			"document is " + std::to_string(docSize.x) + "x" + std::to_string(docSize.y) + ", expected 1100x606");
		require(component->isHeightResizable(), "skin height does not follow the window");
		requireRect(element(doc, "mdTopBar"), 0, 0, 1100, 36, "top bar");
		requireRect(element(doc, "mdFrontPanel"), 0, 36, 1100, 570, "front panel");
		require(!visible(element(doc, "mdEditor")), "editor shown with the front panel in a 606 dp window");

		// ÉDITEUR replaces the front panel: track strip, then the page
		element(doc, "mdViewEditor").Click();
		context.Update();

		// COURBES: hidden in the 606 dp window, under SON once the page has room for it.
		{
			using Curve = mdJucePlugin::CurveView;
			require(!visible(element(doc, "mdEdCurves")), "curves shown in the 606 dp window");
			const auto roomy = element(doc, "mdEdTrackView").GetAttribute("roomyheight")->Get<float>(doc.GetCoreInstance(), 0.0f);
			const auto window = static_cast<int>(roomy + g_pageTop);
			component->setSize(1100, window - 1);
			context.Update();
			require(!visible(element(doc, "mdEdCurves")), "curves shown one dp short of their room");
			component->setSize(1100, window);
			context.Update();
			require(visible(element(doc, "mdEdCurves")) && visible(element(doc, "mdEdCurvesFilter")) && visible(element(doc, "mdEdCurvesEq")),
				"curves not shown when the page has room");
			require(std::lround(element(doc, "mdEdTrackView").GetBox().GetSize(Rml::BoxArea::Border).y) == std::lround(roomy),
				"SON did not grow by the curve row");
			requireTiled(element(doc, "mdEdTrackView"), "SON with curves");
			component->setSize(1100, 606);
			context.Update();
			require(!visible(element(doc, "mdEdCurves")), "curves still shown after the window shrank");

			// The shapes: a pass band between BASE and BASE + WIDTH, a bell at EQF, attack, hold, decay.
			const std::vector<int> band{40, 40, 0, 0};
			require(Curve::level(Curve::Curve::Filter, band, 60.0f / 127.0f) > -3.0f
				&& Curve::level(Curve::Curve::Filter, band, 0.05f) < -20.0f && Curve::level(Curve::Curve::Filter, band, 0.95f) < -20.0f,
				"filter curve does not pass its band only");
			require(Curve::level(Curve::Curve::Eq, {64, 127}, 0.5f) > 17.0f && std::fabs(Curve::level(Curve::Curve::Eq, {64, 64}, 0.5f)) < 0.01f
				&& Curve::level(Curve::Curve::Eq, {64, 127}, 0.05f) < 1.0f, "EQ curve is not a bell at its frequency");
			require(Curve::level(Curve::Curve::Amp, {0, 0, 126}, 0.0f) == 0.0f && Curve::level(Curve::Curve::Amp, {126, 0, 0}, 0.9f) < 1.0f
				&& Curve::level(Curve::Curve::Amp, {10, 107, 10}, 0.5f) == 1.0f && Curve::level(Curve::Curve::Amp, {10, 10, 107}, 1.0f) == 0.0f,
				"amp curve is not attack, hold and decay");
		}
		require(visible(element(doc, "mdEditor")) && !visible(element(doc, "mdFrontPanel")), "ÉDITEUR did not replace the front panel");
		requireRect(element(doc, "mdEditor"), 0, 36, 1100, 570, "editor");
		constexpr int lastTrack = g_trackCount - 1;
		requireRect(element(doc, "editTrack0"), 16, 36 + 4, g_trackTabWidth, 28, "first track tab");
		requireRect(element(doc, "editTrack" + std::to_string(lastTrack)), 16 + lastTrack * g_trackTabPitch, 36 + 4,
			g_trackTabWidth, 28, "last track tab");
		require(doc.GetElementById("editTrack" + std::to_string(g_trackCount)) == nullptr, "more track tabs than tracks");
		constexpr float pageTop = g_pageTop;
		for(const auto& b : g_blocks)
			requireRect(element(doc, b.id), b.x, pageTop + b.y, b.w, b.h, b.id);
		requireTiled(element(doc, "mdEdTrackView"), "SON");
		const auto fits = [&](const char* _view)
		{
			const auto h = element(doc, _view).GetBox().GetSize(Rml::BoxArea::Border).y;
			require(h <= g_pageHeight, std::string(_view) + " is " + std::to_string(h) + " dp, taller than the page");
		};
		fits("mdEdTrackView");

		// every editor control with a param found its parameter (the binding writes min/max on the element);
		// inactive controls (data not exposed yet) carry no param
		std::vector<Rml::Element*> bound;
		collectBound(element(doc, "mdEditor"), bound);
		require(bound.size() == g_boundElements, "expected " + std::to_string(g_boundElements) + " bound editor controls, found "
			+ std::to_string(bound.size()));
		for(auto* e : bound)
		{
			require(e->GetAttribute("max") != nullptr,
				"control not bound to parameter " + e->GetAttribute("param")->Get<Rml::String>(e->GetCoreInstance()));
		}

		// track tabs choose the part edited by partCurrent
		auto& controller = processor.getController();
		element(doc, "editTrack" + std::to_string(lastTrack)).Click();
		context.Update();
		require(controller.getCurrentPart() == lastTrack, "last track tab did not select the last part");
		element(doc, "editTrack0").Click();
		context.Update();
		require(controller.getCurrentPart() == 0, "track tab 1 did not select part 0");

#if defined(MD_EDITOR_SECTION_TEST_MM)
		// MODULATION: LFO 1-3 tabs switch the knob pages
		require(visible(element(doc, "mmLfoPage0")) && !visible(element(doc, "mmLfoPage1")), "LFO 1 page not shown first");
		element(doc, "mmLfoTab1").Click();
		context.Update();
		require(visible(element(doc, "mmLfoPage1")) && !visible(element(doc, "mmLfoPage0")), "LFO 2 tab did not switch pages");
		element(doc, "mmLfoTab0").Click();
		context.Update();
#else
		// MASTER (17th tab) replaces the track view without changing the edited part
		element(doc, "editMaster").Click();
		context.Update();
		require(visible(element(doc, "mdEdMasterView")) && !visible(element(doc, "mdEdTrackView")), "MASTER tab did not show the master effects");
		requireTiled(element(doc, "mdEdMasterView"), "MASTER");
		requireRect(element(doc, "mdEdEcho"), 16, pageTop + 12, 528, 208, "RHYTHM ECHO (6 columns, two rows of knobs)");
		requireRect(element(doc, "mdEdDynamix"), 556, pageTop + 232, 528, 208, "DYNAMIX (6 columns, two rows of knobs)");
		require(element(doc, "mdEdFxVal0_0").IsClassSet("mdEdUnread") && element(doc, "mdEdFx3_7").IsClassSet("mdEdUnread"),
			"master effects shown before any Kit dump");
		fits("mdEdMasterView");
		require(controller.getCurrentPart() == 0, "MASTER tab changed the edited part");
		element(doc, "editTrack0").Click();
		context.Update();
		require(visible(element(doc, "mdEdTrackView")) && !visible(element(doc, "mdEdMasterView")), "track tab did not bring the track view back");
#endif

		// MIX: the LED of a row mutes its own track
		{
			auto* mute = controller.getParameter("Mute", 2);
			require(mute != nullptr, "no Mute parameter for part 2");
			const auto before = mute->getUnnormalizedValue();
			Rml::ElementList leds;
			element(doc, "mdEdLevelRow2").GetElementsByTagName(leds, "button");
			require(!leds.empty(), "MIX row 3 has no mute LED");
			leds.front()->Click();
			context.Update();
			require(mute->getUnnormalizedValue() != before, "mute LED of row 3 did not change Mute of part 2");
			leds.front()->Click();
			context.Update();
			require(mute->getUnnormalizedValue() == before, "second click did not restore Mute of part 2");
		}

		// MACHINE: the picker opens under the block, a family tab filters the machines,
		// a click assigns the machine to the edited track and closes the picker.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			auto& picker = element(doc, "mdEdMachinePicker");
			require(!visible(picker), "machine picker open before it was asked for");
			require(element(doc, "mdEdMachineName").GetInnerRML() == "—", "unknown machine not shown as unknown");
			const auto firstName = [&] { return std::string(element(doc, std::string("mdEdName_") + g_firstSynthesis).GetInnerRML()); };
			require(firstName() == g_genericParameterName, "unknown machine's synthesis control not named as in the skin");
			element(doc, "mdEdMachineChange").Click();
			context.Update();
			require(visible(picker), "CHANGER DE MACHINE did not open the picker");
			requireRect(picker, 16, pageTop + g_pickerTop, 1068, picker.GetBox().GetSize(Rml::BoxArea::Border).y, "machine picker");
			Rml::ElementList tabs;
			element(doc, "mdEdPickerFamilies").GetElementsByTagName(tabs, "div");
			const auto& machines = md::machines::machines(g_model);
			size_t offered = 0;
			for(size_t family = 0; family < md::machines::families(g_model).size(); ++family)
				offered += std::any_of(machines.begin(), machines.end(), [family](const auto& _m) { return _m.family == family && _m.assignable; }) ? 1 : 0;
			require(tabs.size() == offered, "picker does not show one tab per family with machines to offer");
			require(offered == (g_model == md::MachineModel::Machinedrum ? 10u : 7u), "unexpected family tab count");
			require(visible(element(doc, "mdEdMachine" + std::to_string(g_otherFamilyMachine)))
				&& !visible(element(doc, "mdEdMachine" + std::to_string(g_pickMachine))), "family 1 shown before its tab was chosen");
			element(doc, "mdEdFamily1").Click();
			context.Update();
			require(visible(element(doc, "mdEdMachine" + std::to_string(g_pickMachine)))
				&& !visible(element(doc, "mdEdMachine" + std::to_string(g_otherFamilyMachine))), "family tab did not filter the machines");
			require(doc.GetElementById("mdEdMachine4") == nullptr || g_model == md::MachineModel::Monomachine,
				"picker offers GND-SW, which the manual does not list");

			element(doc, "mdEdMachine" + std::to_string(g_pickMachine)).Click();
			context.Update();
			require(md.getTrackMachine(0) == g_pickMachine, "picking a machine did not assign it to the edited track");
			require(md.getTrackMachine(1) == md::machines::g_unknown, "picking a machine touched another track");
			require(!visible(picker), "picker stayed open after the assignment");
			require(element(doc, "mdEdMachineName").GetInnerRML() == g_pickName
				&& element(doc, "mdEdMachineFamily").GetInnerRML() == g_pickFamily, "MACHINE block does not show the assigned machine");
			require(firstName() == g_pickParameterName, "synthesis control not named after the assigned machine");

			element(doc, "editTrack1").Click();
			context.Update();
			require(element(doc, "mdEdMachineName").GetInnerRML() == "—", "MACHINE block kept the previous track's machine");
			require(firstName() == g_genericParameterName, "synthesis control kept the previous track's name");
			element(doc, "editTrack0").Click();
			context.Update();
			require(element(doc, "mdEdMachineName").GetInnerRML() == g_pickName, "MACHINE block lost the machine of track 1");
			require(firstName() == g_pickParameterName, "synthesis control lost the name of track 1's machine");
		}

#if !defined(MD_EDITOR_SECTION_TEST_MM)
		// PAS: RELIRE reads the current pattern; the grid shows the edited track's
		// trigs and length, and a focused step shows its locked values.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			const auto text = [&](const char* _id) { return std::string(element(doc, _id).GetInnerRML()); };
			require(text("mdEdStepsInfo").find("en attente") != std::string::npos, "PAS block claims a pattern before any read");
			mdJucePlugin::ControllerAutomationTestAccess::useSyntheticFirmware(md);
			element(doc, "mdEdStepsRefresh").Click();
			context.Update();
			require(text("mdEdStepsInfo").find("lecture") != std::string::npos, "RELIRE did not start a pattern read");

			std::array<uint32_t, 16> trigs{};
			trigs[0] = 0x11;           // track 1: steps 1 and 5
			trigs[1] = 0x80000000u;    // track 2: step 32, beyond the 24-step length
			md.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 18, 0xf7}, synthLib::MidiEventSource::Device);
			md.parseSysexMessage(mdAutomationTest::makeMdPatternDump(18, 24, trigs, 99), synthLib::MidiEventSource::Device);
			element(doc, "editTrack0").Click();
			context.Update();
			const auto step = [&](int _s) -> Rml::Element& { return element(doc, "mdEdStep" + std::to_string(_s)); };
			require(step(0).IsClassSet("mdEdStepTrig") && step(4).IsClassSet("mdEdStepTrig")
				&& !step(1).IsClassSet("mdEdStepTrig"), "PAS grid does not show track 1's trigs");
			require(step(24).IsClassSet("mdEdStepOut") && !step(23).IsClassSet("mdEdStepOut"), "PAS grid ignores the pattern length");
			require(text("mdEdStepsInfo").find("B03") != std::string::npos && text("mdEdStepsInfo").find("24 pas") != std::string::npos,
				"PAS line does not name pattern B03 and its length");
			require(element(doc, "mdEdStepPage0").IsClassSet("mdEdSelected") && element(doc, "mdEdStepPage1").IsClassSet("mdEdOff"),
				"PAS offers steps 33 to 64 for a pattern of 24 steps");

			step(0).Click();
			context.Update();
			require(step(0).IsClassSet("mdEdStepFocus"), "clicked step not focused");
			require(visible(element(doc, "mdEdLock_MachineParameter1")) && text("mdEdLock_MachineParameter1") == "99"
				&& element(doc, "mdEdVal_MachineParameter1").IsClassSet("mdEdHidden"), "locked value not shown on the focused step");
			require(element(doc, "mdEdCtl_FilterBase").IsClassSet("mdEdDim") && !element(doc, "mdEdCtl_MachineParameter1").IsClassSet("mdEdDim"),
				"unlocked controls not dimmed on the focused step");

			element(doc, "editTrack1").Click();
			context.Update();
			require(!step(0).IsClassSet("mdEdStepTrig") && !step(31).IsClassSet("mdEdStepTrig") && !visible(element(doc, "mdEdLock_MachineParameter1")),
				"PAS grid kept track 1 on track 2");
			element(doc, "editTrack0").Click();
			step(0).Click();
			context.Update();
			require(!step(0).IsClassSet("mdEdStepFocus") && !element(doc, "mdEdCtl_FilterBase").IsClassSet("mdEdDim")
				&& !visible(element(doc, "mdEdLock_MachineParameter1")), "second click did not clear the focus");

			// Writing: a double click sets a trig and focuses its step; a control then
			// writes a lock on that step, and a double click on it clears the lock.
			const auto doubleClick = [&](Rml::Element& _e)
			{
				_e.Click();
				_e.Click();
				_e.DispatchEvent(Rml::EventId::Dblclick, Rml::Dictionary());
				context.Update();
			};
			auto& lockKnob = element(doc, "mdEdLockKnob_FilterBase");
			require(!visible(lockKnob), "lock knob shown without a focused step");
			doubleClick(step(1));
			require(step(1).IsClassSet("mdEdStepTrig") && step(1).IsClassSet("mdEdStepFocus"), "double click did not set a trig and focus it");
			require(md.getPattern()->hasTrig(0, 1), "trig not in the controller's pattern");
			// Written once the edits pause, not at the click
			require(md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::None
				&& mdJucePlugin::ControllerAutomationTestAccess::patternWriteWaiting(md), "trig written before the edits paused");
			mdJucePlugin::ControllerAutomationTestAccess::pauseEdits(md);
			require(md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::Pending
				&& !mdJucePlugin::ControllerAutomationTestAccess::patternWriteWaiting(md), "trig not written to the firmware");
			require(visible(lockKnob) && visible(element(doc, "mdEdLockKnob_Volume")), "lock knobs not over the controls of a step with a trig");
			const auto filterBase = md.getParameter("FilterBase", 0);
			const auto kitBefore = filterBase->getUnnormalizedValue();
			juceRmlUi::ElemValue::setValue(&lockKnob, 77.0f);
			context.Update();
			require(visible(element(doc, "mdEdLock_FilterBase")) && text("mdEdLock_FilterBase") == "77", "lock knob did not write a lock");
			require(filterBase->getUnnormalizedValue() == kitBefore, "lock knob changed the Kit value");
			lockKnob.DispatchEvent(Rml::EventId::Dblclick, Rml::Dictionary());
			context.Update();
			require(!visible(element(doc, "mdEdLock_FilterBase")), "double click on the lock knob did not clear the lock");
			doubleClick(step(1));
			require(!step(1).IsClassSet("mdEdStepTrig") && !md.getPattern()->hasTrig(0, 1) && !visible(lockKnob),
				"second double click did not clear the trig");
			step(1).Click();
			context.Update();
		}
#endif

		// categories: MIX replaces SON
		tabButton(doc, "mdEdit", "1").Click();
		context.Update();
		require(visible(element(doc, "mdEdPageMix")) && !visible(element(doc, "mdEdPageSound")), "MIX tab did not switch pages");
		requireTiled(element(doc, "mdEdPageMix"), "MIX");
		{
			Rml::ElementList contents;
			element(doc, "mdEdPageMix").GetElementsByClassName(contents, "mdEdContent");
			require(!contents.empty() && contents.front()->GetBox().GetSize(Rml::BoxArea::Border).y <= g_pageHeight, "MIX is taller than the page");
		}
		tabButton(doc, "mdEdit", "0").Click();
		context.Update();
		require(visible(element(doc, "mdEdPageSound")) && !visible(element(doc, "mdEdPageMix")), "SON tab did not switch back");

		// FACE AVANT brings the front panel back; a category opens the editor again
		element(doc, "mdViewPanel").Click();
		context.Update();
		require(visible(element(doc, "mdFrontPanel")) && !visible(element(doc, "mdEditor")), "FACE AVANT did not bring the front panel back");
		require(element(doc, "mdViewPanel").IsPseudoClassSet("checked") && !element(doc, "mdViewEditor").IsPseudoClassSet("checked"),
			"switch does not show the front panel as chosen");
		tabButton(doc, "mdEdit", "1").Click();
		context.Update();
		require(visible(element(doc, "mdEditor")) && visible(element(doc, "mdEdPageMix")), "MIX category did not open the editor on MIX");
		tabButton(doc, "mdEdit", "0").Click();
		element(doc, "mdViewPanel").Click();
		context.Update();

		// A window tall enough for both stacks them and hides the switch; a shorter one goes back to the switch.
		const auto& first = g_blocks[0];
		component->setSize(1100, 1200);
		context.Update();
		require(visible(element(doc, "mdFrontPanel")) && visible(element(doc, "mdEditor")), "1200 dp window does not stack panel and editor");
		require(element(doc, "mdViewPanel").IsClassSet("mdEdHidden"), "switch shown while both views are");
		requireRect(element(doc, "mdEditor"), 0, 606, 1100, 594, "stacked editor");
		requireRect(element(doc, first.id), first.x, 606 + 32 + first.y, first.w, first.h, "first block, stacked");
		component->setSize(1100, 606);
		context.Update();
		require(visible(element(doc, "mdFrontPanel")) && !visible(element(doc, "mdEditor")), "606 dp window still stacked");
		element(doc, "mdViewEditor").Click();
		context.Update();

		// The top bar's screen: the Kit and pattern the firmware selected, the Kit's name under them.
		{
			using Screen = mdJucePlugin::KitPatternScreen;
			require(Screen::mainLine(0xff, 0xff) == "KIT — · PATTERN —" && Screen::mainLine(63, 127) == "KIT 64 · PATTERN H16"
				&& Screen::mainLine(127, 0) == "KIT 128 · PATTERN A01", "screen line does not number Kits from 1 and patterns A01 to H16");

			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			// The replies below stand for the firmware, even where a ROM was found and never booted
			mdJucePlugin::ControllerAutomationTestAccess::useSyntheticFirmware(md);
			const auto text = [&](const char* _id) { return std::string(element(doc, _id).GetInnerRML()); };
			mdJucePlugin::EditorIdentityTestAccess::updateScreen(*editor);
			context.Update();
			require(text("mdEdScreenMain").rfind("KIT — · PATTERN ", 0) == 0 && text("mdEdScreenName").empty(),
				"screen names a Kit before any status reply");

			const uint8_t product = g_model == md::MachineModel::Monomachine ? 0x03 : 0x02;
			const auto status = [&](const uint8_t _parameter, const uint8_t _value)
			{
				md.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, product, 0x00, 0x72, _parameter, _value, 0xf7},
					synthLib::MidiEventSource::Device);
			};
			const std::string name = g_model == md::MachineModel::Monomachine ? "NIGHT BUS" : "BROKEN DUB";
			md.onStateLoaded();
			status(0x01, 0);
			status(0x02, 2);
			md.parseSysexMessage(mdAutomationTest::makeGlobalDump(g_model, 0, 0), synthLib::MidiEventSource::Device);
			md.parseSysexMessage(mdAutomationTest::makeKitDump(g_model, 2, 64, {}, name), synthLib::MidiEventSource::Device);
			status(0x04, 19);
			require(md.getCurrentKit() == 2 && md.getKitName() == name && md.getCurrentPattern() == 19,
				"controller did not keep the selected Kit, its name and the pattern");
			require(mdJucePlugin::EditorIdentityTestAccess::updateScreen(*editor), "screen not refreshed after the selection changed");
			context.Update();
			require(text("mdEdScreenMain") == "KIT 03 · PATTERN B04" && text("mdEdScreenName") == name,
				"screen shows \"" + text("mdEdScreenMain") + "\" / \"" + text("mdEdScreenName") + "\"");
			require(!mdJucePlugin::EditorIdentityTestAccess::updateScreen(*editor), "screen rewritten without a change");

			// Another Kit, as the 5 s poll finds it: its name is unknown until its dump arrives
			md.requestAutomationState();
			status(0x01, 0);
			status(0x02, 5);
			mdJucePlugin::EditorIdentityTestAccess::updateScreen(*editor);
			context.Update();
			require(text("mdEdScreenMain") == "KIT 06 · PATTERN B04" && text("mdEdScreenName").empty(),
				"screen kept the previous Kit's name");
			md.parseSysexMessage(mdAutomationTest::makeKitDump(g_model, 5, 64, {}, name), synthLib::MidiEventSource::Device);
			mdJucePlugin::EditorIdentityTestAccess::updateScreen(*editor);
			context.Update();
			require(text("mdEdScreenName") == name, "screen did not show the new Kit's name");
		}

		// Values the firmware set without telling: after a machine assignment, the
		// machine's values of the edited track are greyed and read "—" until a Kit is
		// applied or a control sets them.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			const bool mm = g_model == md::MachineModel::Monomachine;
			const uint8_t product = mm ? 0x03 : 0x02;
			const auto status = [&](const uint8_t _parameter, const uint8_t _value)
			{
				md.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, product, 0x00, 0x72, _parameter, _value, 0xf7},
					synthLib::MidiEventSource::Device);
			};
			const auto loadKit = [&]
			{
				md.onStateLoaded();
				status(0x01, 0);
				status(0x02, 0);
				md.parseSysexMessage(mdAutomationTest::makeGlobalDump(g_model, 0, 0), synthLib::MidiEventSource::Device);
				md.parseSysexMessage(mdAutomationTest::makeKitDump(g_model, 0, 64, std::vector<uint16_t>(g_trackCount, g_pickMachine)),
					synthLib::MidiEventSource::Device);
				mdJucePlugin::EditorIdentityTestAccess::updateUnread(*editor);
				context.Update();
			};
			const auto unread = [&](const std::string& _id) { return element(doc, _id).IsClassSet("mdEdUnread"); };
			const std::string synthesis = mm ? "SynthesisA" : "MachineParameter1";
			// MD: effects (FilterBase) and routing (Volume) pages too; MM: the amp page is kept
			const std::string other = mm ? "AmpVolume" : "FilterBase";
			const auto mixRowValues = [&](const int _track, const std::string& _param)
			{
				Rml::ElementList knobs;
				element(doc, "mdEdLevelRow" + std::to_string(_track)).GetElementsByTagName(knobs, "knob");
				for(auto* knob : knobs)
					if(knob->GetAttribute("param", std::string()) == _param)
						return knob->IsClassSet("mdEdUnread");
				throw std::runtime_error("MIX row " + std::to_string(_track + 1) + " has no " + _param);
			};

			element(doc, "editTrack0").Click();
			loadKit();
			require(md.isAutomationSynchronized(), "unread test did not synchronize");
			require(!unread("mdEdCtl_" + synthesis) && !unread("mdEdVal_" + synthesis), "value greyed after an applied Kit dump");

			require(md.assignMachine(0, g_otherFamilyMachine), "assignment refused");
			require(mdJucePlugin::EditorIdentityTestAccess::updateUnread(*editor), "unread values not refreshed after an assignment");
			context.Update();
			// A synthesis parameter the machine does not use: dimmed, named "—"
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(element(doc, std::string("mdEdCtl_") + g_otherFamilyUnused).IsClassSet("mdEdUnused")
				&& element(doc, std::string("mdEdName_") + g_otherFamilyUnused).GetInnerRML() == "—"
				&& !element(doc, std::string("mdEdCtl_") + g_otherFamilyUsed).IsClassSet("mdEdUnused"),
				"synthesis parameters not dimmed exactly where the machine does not use them");
			require(unread("mdEdCtl_" + synthesis) && unread("mdEdVal_" + synthesis), "synthesis value not greyed after an assignment");
			require(unread("mdEdCtl_" + other) == !mm, mm ? "kept amp value greyed" : "effects value not greyed after an assignment");
			require(mixRowValues(0, mm ? "AmpVolume" : "Volume") == !mm && !mixRowValues(1, mm ? "AmpVolume" : "Volume"),
				"MIX rows do not grey exactly the assigned track's routing");
			// What shows those values elsewhere: the filter curve (MD effects page; kept on the MM)
			require(unread("mdEdCurveFilter") == !mm, "filter curve does not follow its values");
			require(!mdJucePlugin::EditorIdentityTestAccess::updateUnread(*editor), "unread values rewritten without a change");
			snap("-unread");

			// Another track shows its own values; the assigned one is greyed again on return
			element(doc, "editTrack1").Click();
			context.Update();
			require(!unread("mdEdCtl_" + synthesis), "another track's value greyed");
			element(doc, "editTrack0").Click();
			context.Update();
			require(unread("mdEdCtl_" + synthesis), "assigned track's value no longer greyed after a track change");

			// A control sets the value: known again, the others stay greyed
			juceRmlUi::ElemValue::setValue(&element(doc, "mdEdCtl_" + synthesis), 90.0f);
			mdJucePlugin::EditorIdentityTestAccess::updateUnread(*editor);
			context.Update();
			const std::string next = mm ? "SynthesisB" : "MachineParameter2";
			require(!unread("mdEdCtl_" + synthesis) && !unread("mdEdVal_" + synthesis) && unread("mdEdCtl_" + next),
				"a control did not make exactly its own value known");

			// The live Kit (the machine's RAM): once two reads show the assigned machine, the values still
			// unknown take the firmware's; the one a control set keeps its value
			using Access = mdJucePlugin::ControllerAutomationTestAccess;
			const auto value = [&](const std::string& _param, const uint8_t _part)
			{
				return static_cast<int>(controller.getParameter(_param, _part)->getUnnormalizedValue());
			};
			md::LiveKit kit;
			kit.tracks = static_cast<uint8_t>(g_trackCount);
			for(int track = 0; track < g_trackCount; ++track)
			{
				kit.machines[track] = static_cast<uint8_t>(g_pickMachine);
				kit.values[track].fill(64);
			}
			kit.machines[0] = static_cast<uint8_t>(g_otherFamilyMachine);
			kit.values[0].fill(23);
			Access::readLiveKit(md, kit);
			mdJucePlugin::EditorIdentityTestAccess::updateUnread(*editor);
			context.Update();
			require(!unread("mdEdCtl_" + next) && value(next, 0) == 23 && value(synthesis, 0) == 90,
				"the live Kit did not give the unknown values exactly");
			// A machine changed on the front panel: taken, with the values the firmware gave its pages
			kit.machines[1] = static_cast<uint8_t>(g_otherFamilyMachine);
			kit.values[1].fill(77);
			Access::readLiveKit(md, kit);
			require(md.getTrackMachine(1) == g_otherFamilyMachine && value(synthesis, 1) == 77
				&& value(other, 1) == (mm ? 64 : 77), "a machine changed on the front panel was not taken with its values");
			// A CC sent to a track between the reads: the machine waits for reads that saw none go out, the
			// RAM may not show the CC yet
			kit.machines[2] = static_cast<uint8_t>(g_otherFamilyMachine);
			kit.values[2].fill(50);
			Access::readLiveKit(md, kit, [&]
			{
				controller.getParameter(synthesis, 2)->setUnnormalizedValueNotifyingHost(50, pluginLib::Parameter::Origin::Ui);
			});
			require(md.getTrackMachine(2) == g_pickMachine, "a machine was taken while a CC to its track was on its way");
			Access::readLiveKit(md, kit);
			require(md.getTrackMachine(2) == g_otherFamilyMachine && value(synthesis, 2) == 50,
				"a machine was not taken once no CC went to its track");
			Access::readLiveKit(md, std::nullopt);

			loadKit();
			require(!unread("mdEdCtl_" + next) && !mixRowValues(0, mm ? "AmpVolume" : "Volume"), "values still greyed after the Kit was applied");

#if !defined(MD_EDITOR_SECTION_TEST_MM)
			// MASTER: the Kit's master effects, 8 knobs each, from the Kit dump (byte n of the
			// effects is 64 + n there, reverb first), and a turned knob sends its value.
			using Effect = md::automation::sysex::MasterEffect;
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			element(doc, "editMaster").Click();
			context.Update();
			const auto text = [&](const std::string& _id) { return std::string(element(doc, _id).GetInnerRML()); };
			require(md.getMasterEffect(Effect::Echo, 0) == uint8_t{64 + 8} && md.getMasterEffect(Effect::Reverb, 0) == uint8_t{64}
				&& md.getMasterEffect(Effect::Dynamix, 7) == uint8_t{(64 + 31) & 0x7f}, "controller did not keep the Kit's master effects");
			require(text("mdEdFxVal0_0") == "72" && text("mdEdFxVal1_0") == "64" && text("mdEdFxVal2_3") == "83"
				&& !unread("mdEdFxVal0_0") && !unread("mdEdFx3_7"), "MASTER knobs do not show the Kit's master effects");
			const auto revision = md.getMasterEffectRevision();
			juceRmlUi::ElemValue::setValue(&element(doc, "mdEdFx2_4"), 20.0f);
			require(md.getMasterEffect(Effect::Eq, 4) == uint8_t{20} && md.getMasterEffectRevision() > revision,
				"a turned MASTER knob did not set its master effect");
			require(text("mdEdFxVal2_4") == "20", "MASTER value line did not follow its knob");
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(text("mdEdFxVal2_4") == "20", "MASTER value line changed back after the turn");
			element(doc, "editTrack0").Click();
			context.Update();

			// MODULATION: track 1's LFO from the Kit dump (zeros in the test's: track 1's PTCH, triangles, FREE),
			// then from the live Kit, with the master effects; an update segment sets the LFO ($62)
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(text("mdEdLfoDest") == "PISTE 01 · PTCH" && element(doc, "mdEdLfoUpdate0").IsClassSet("mdEdSelected")
				&& text("mdEdLfoShape1Name") == "TRIANGLE", "MODULATION does not show the Kit's LFO: \"" + text("mdEdLfoDest") + "\"");
			{
				md::LiveKit live;
				live.tracks = static_cast<uint8_t>(g_trackCount);
				live.machinedrum = true;
				for(int track = 0; track < g_trackCount; ++track)
				{
					live.machines[track] = static_cast<uint8_t>(g_pickMachine);
					live.values[track].fill(64);
				}
				live.lfos[0] = {4, 17, md::LfoSettings::Ramp, md::LfoSettings::Exponential, md::LfoSettings::Hold};
				for(uint8_t index = 0; index < live.masterEffects.size(); ++index)
					live.masterEffects[index] = static_cast<uint8_t>(10 + index);
				Access::readLiveKit(md, live);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				context.Update();
				require(text("mdEdLfoDest") == "PISTE 05 · VOL" && element(doc, "mdEdLfoUpdate2").IsClassSet("mdEdSelected")
					&& !element(doc, "mdEdLfoUpdate0").IsClassSet("mdEdSelected") && text("mdEdLfoShape1Name") == "RAMPE"
					&& text("mdEdLfoShape2Name") == "EXPO", "MODULATION does not show the live Kit's LFO: \"" + text("mdEdLfoDest") + "\"");
				snap("-lfo");
				// The live Kit's master effects, in the dump's order: reverb first
				require(md.getMasterEffect(Effect::Reverb, 0) == uint8_t{10} && md.getMasterEffect(Effect::Echo, 0) == uint8_t{18}
					&& md.getMasterEffect(Effect::Dynamix, 7) == uint8_t{41}, "the controller did not take the live Kit's master effects");
				Access::readLiveKit(md, std::nullopt);
				element(doc, "mdEdLfoUpdate1").Click();
				context.Update();
				require(md.getTrackLfo(0) && md.getTrackLfo(0)->update == md::LfoSettings::Trig
					&& element(doc, "mdEdLfoUpdate1").IsClassSet("mdEdSelected"), "TRIG did not set the LFO's update");
			}

			// MIX, SORTIE: each track's output from the Global (MAIN for all in the test's
			// Global), and a click routes the track.
			using Output = md::automation::sysex::TrackOutput;
			const auto selected = [&](const int _track, const int _output)
			{
				return element(doc, "mdEdOut" + std::to_string(_track) + "_" + std::to_string(_output)).IsClassSet("mdEdSelected");
			};
			tabButton(doc, "mdEdit", "1").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(selected(0, 6) && !selected(0, 0) && selected(15, 6) && !unread("mdEdOut0_6"), "SORTIE does not show MAIN from the Global");
			element(doc, "mdEdOut4_2").Click();
			context.Update();
			require(md.getTrackOutput(4) == Output::C && selected(4, 2) && !selected(4, 6), "a click did not route track 5 to C");
			// A Global dump requested before a routing write does not undo it
			md.requestAutomationState();
			status(0x01, 0);
			element(doc, "mdEdOut4_3").Click();
			md.parseSysexMessage(mdAutomationTest::makeGlobalDump(g_model, 0, 0), synthLib::MidiEventSource::Device);
			require(md.getTrackOutput(4) == Output::D, "a Global dump requested before the routing write undid it");
			// A Global requested after it is the firmware's word
			loadKit();
			require(md.getTrackOutput(4) == Output::Main, "a Global dump requested after the write did not replace the routing");
			element(doc, "mdEdOut4_2").Click();
			element(doc, "mdEdOut8_0").Click();
			element(doc, "mdEdOut12_5").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(selected(4, 2) && selected(8, 0) && selected(12, 5) && selected(0, 6), "SORTIE does not show the routing");
			tabButton(doc, "mdEdit", "0").Click();
			context.Update();
#endif
		}

		const auto text = [&](const std::string& _id) { return std::string(element(doc, _id).GetInnerRML()); };

		// MIX, SORTIES: the meters follow the levels the processor measured after each block,
		// the louder channel's peak shows in dB, a bus near 0 dBFS turns orange, levels fall
		// once the sound stops; each bus names its tracks (MD) and says whether the DAW has it on.
		{
			using View = mdJucePlugin::OutputMetersView;
			require(View::meterPosition(1.0f) == 1.0f && View::meterPosition(2.0f) == 1.0f && View::meterPosition(0.001f) == 0.0f
				&& std::fabs(View::meterPosition(0.5f) - (60.0f - 6.0206f) / 60.0f) < 0.001f, "meter scale is not -60 to 0 dBFS");
			auto& meters = processor.getOutputMeters();
			tabButton(doc, "mdEdit", "1").Click();
			context.Update();
			require(visible(element(doc, "mdEdMeters0")), "meters area hidden on MIX");
			double now = juce::Time::getMillisecondCounterHiRes();
			mdJucePlugin::EditorIdentityTestAccess::updateMeters(*editor, now);
			std::vector<float> half(128, 0.5f);
			meters.measure(0, half.data(), static_cast<int>(half.size()));
			meters.measure(1, half.data(), static_cast<int>(half.size()));
			now += 250;
			require(mdJucePlugin::EditorIdentityTestAccess::updateMeters(*editor, now), "meters not redrawn after a level");
			context.Update();
			require(text("mdEdBusLevel0") == "−6 dB" && text("mdEdBusLevel1") == "—", "bus peaks not shown in dB");
			const auto& view = mdJucePlugin::EditorIdentityTestAccess::meters(*editor);
			require(std::fabs(view.holdPosition(0, 0) - View::meterPosition(0.5f)) < 0.01f && view.levelPosition(0, 1) > 0.85f
				&& view.holdPosition(1, 0) == 0.0f && !view.isHot(0, 0), "meters do not follow the levels");
			std::vector<float> full(128, 1.0f);
			meters.measure(0, full.data(), static_cast<int>(full.size()));
			now += 250;
			mdJucePlugin::EditorIdentityTestAccess::updateMeters(*editor, now);
			require(view.isHot(0, 0) && !view.isHot(0, 1) && view.levelPosition(0, 0) == 1.0f, "a channel at 0 dBFS did not turn orange");
			require(text("mdEdBusActive0") == "ACTIF DANS LE DAW" && element(doc, "mdEdBusActive0").IsClassSet("mdEdSelected")
				&& text("mdEdBusActive1") == "INACTIF DANS LE DAW" && !element(doc, "mdEdBusActive1").IsClassSet("mdEdSelected"),
				"buses not shown on or off as the DAW has them");
#if defined(MD_EDITOR_SECTION_TEST_MM)
			require(text("mdEdBusTracks0") == "routage des pistes : inconnu sur le MM" && text("mdEdBusWarning1").empty(),
				"MM buses claim a routing");
#else
			// The routing the SORTIE test left: track 5 on C, 9 on A, 13 on F, the others on MAIN
			require(text("mdEdBusTracks0") == "MAIN : 13 pistes · A : 9" && text("mdEdBusTracks1") == "C : 5"
				&& text("mdEdBusTracks2") == "F : 13", "buses do not name their tracks: \"" + text("mdEdBusTracks0") + "\"");
			require(text("mdEdBusWarning1") == "pistes muettes : activer Out C/D dans le DAW" && text("mdEdBusWarning0").empty(),
				"no warning for tracks routed to a bus the DAW has off");
			using Output = md::automation::sysex::TrackOutput;
			View::TrackOutputs outputs{};
			outputs.fill(Output::Main);
			require(View::routedTracks(outputs, 0) == "MAIN : les 16 pistes" && View::routedTracks(outputs, 1) == "aucune piste",
				"all tracks on MAIN not named so");
			outputs[1] = outputs[2] = outputs[3] = outputs[6] = Output::C;
			outputs[7] = Output::D;
			outputs[10] = std::nullopt;
			require(View::routedTracks(outputs, 1) == "routage : en attente du Global", "unknown routing not said");
			outputs[10] = Output::C;
			require(View::routedTracks(outputs, 1) == "C : 2–4, 7, 11 · D : 8", "track runs not written as ranges");
#endif
			snap("-mix");
			// The sound stops: after a second of hold the levels fall 20 dB a second
			for(int second = 1; second <= 4; ++second)
			{
				now += 1000;
				mdJucePlugin::EditorIdentityTestAccess::updateMeters(*editor, now);
			}
			context.Update();
			require(text("mdEdBusLevel0") == "—" && view.holdPosition(0, 0) == 0.0f && view.levelPosition(0, 0) == 0.0f
				&& !view.isHot(0, 0), "meters did not fall after the sound stopped");
		}

		// SYSTÈME: one row per subject, with its state and action.
		{
			using Page = mdJucePlugin::SystemPage;
			using Model = md::MachineModel;
			require(Page::globalLine(Model::Machinedrum, 0xff, false, 0x7f) == "MIDI : en attente du Global"
				&& Page::globalLine(Model::Monomachine, 2, true, 0x7f) == "Global 3 · MIDI : aucun canal (NONE), l'automation attend"
				&& Page::globalLine(Model::Machinedrum, 0, true, 14) == "Global 1 · MIDI : canal de base 15",
				"Global line wrong for an unknown Global, NONE or a base channel near 16");
			tabButton(doc, "mdEdit", "4").Click();
			context.Update();
			double now = juce::Time::getMillisecondCounterHiRes() + 10000;
			require(mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now), "SYSTÈME not filled when shown");
			context.Update();
			const std::string channels = g_model == Model::Monomachine ? "1 à 6" : "1 à 4";
			require(text("mdSysGlobalState") == "Global 1 · MIDI : canal de base 1, pistes sur les canaux " + channels,
				"SYSTÈME does not show the Global: \"" + text("mdSysGlobalState") + "\"");
			require(text("mdSysSysexState") == "aucun transfert ; envoie un fichier .syx à la machine" && text("mdSysSysex") == "FICHIER…",
				"SYSTÈME does not show the idle SysEx transfer");
			const bool follow = processor.getFollowHostTempoSetting();
			element(doc, "mdSysFollowTempo").Click();
			mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now += 1);
			context.Update();
			require(processor.getFollowHostTempoSetting() != follow && element(doc, "mdSysFollowTempo").IsPseudoClassSet("checked") != follow,
				"SUIVRE L'HÔTE did not toggle the setting");
			element(doc, "mdSysFollowTempo").Click();
			const bool parallel = processor.getParallelTransportSetting();
			element(doc, "mdSysParallel").Click();
			mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now += 1);
			context.Update();
			require(processor.getParallelTransportSetting() != parallel && element(doc, "mdSysParallel").IsPseudoClassSet("checked") != parallel,
				"PARALLÈLE did not toggle the setting");
			element(doc, "mdSysParallel").Click();
			mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now += 1);
			context.Update();
			require(processor.getFollowHostTempoSetting() == follow && processor.getParallelTransportSetting() == parallel, "settings not toggled back");
			// The plug-in menu's other entries. ÉCHELLE: a new install opens at 130 %, which the selector has.
			const auto scaleSetting = [&] { return juce::roundToInt(processor.getConfig().getDoubleValue("scale", 100)); };
			const auto scaleId = [](const int _scale) { return "mdSysScale" + std::to_string(_scale); };
			const auto scale = scaleSetting();
			require(text("mdSysScaleState") == std::to_string(scale) + " %" && element(doc, scaleId(scale)).IsClassSet("mdEdSelected"),
				"ÉCHELLE does not show the scale: \"" + text("mdSysScaleState") + "\"");
			element(doc, scaleId(150)).Click();
			mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now += 1);
			context.Update();
			require(scaleSetting() == 150 && element(doc, scaleId(150)).IsClassSet("mdEdSelected")
				&& !element(doc, scaleId(scale)).IsClassSet("mdEdSelected") && text("mdSysScaleState") == "150 %",
				"ÉCHELLE did not set the scale");
			element(doc, scaleId(scale)).Click();
			mdJucePlugin::EditorIdentityTestAccess::updateSystem(*editor, now += 1);
			context.Update();
			require(scaleSetting() == scale && !element(doc, scaleId(150)).IsClassSet("mdEdSelected"), "scale not set back");
			// DIAGNOSTICS: the capture's state in French, CAPTURE on while it records. Not started here: it writes a report.
			using Status = synthLib::PerformanceReport::Status;
			require(Page::diagnosticsLine(std::nullopt, false) == "aucune capture ; CAPTURE mesure l'émulation dans un rapport"
				&& Page::diagnosticsLine(Status::Recording, false) == "capture en cours : 10 minutes ou 8 Mio au plus"
				&& Page::diagnosticsLine(Status::Recording, true) == "dossier des journaux impossible à créer",
				"diagnostics line wrong for no capture, a capture or a logs folder error");
			require(text("mdSysDiagnosticsState") == Page::diagnosticsLine(processor.performanceDiagnosticsState(),
					processor.performanceDiagnosticsFolderError())
				&& element(doc, "mdSysCapture").IsPseudoClassSet("checked") == processor.performanceDiagnosticsActive()
				&& element(doc, "mdSysLogs").GetTagName() == "button",
				"SYSTÈME does not show the performance capture: \"" + text("mdSysDiagnosticsState") + "\"");
#if !defined(MD_EDITOR_SECTION_TEST_MM)
			require(doc.GetElementById("mdSysRamComplete") != nullptr && !text("mdSysRamState").empty(), "MD has no RAM recording row");
#else
			require(doc.GetElementById("mdSysRamComplete") == nullptr, "MM shows a RAM recording row");
#endif
			require(element(doc, "mdEdOptions").GetTagName() == "button" && !element(doc, "mdEdOptions").IsClassSet("mdEdOff"),
				"OPTIONS still inactive");
			snap("-system");
			tabButton(doc, "mdEdit", "0").Click();
			context.Update();
		}

#if !defined(MD_EDITOR_SECTION_TEST_MM)
		// JOUER: the current pattern on every track under the steps' numbers, a trig with locks in orange,
		// and the lane of the edited track: the Kit value in grey, a lock in orange, on the steps that play.
		// A groove on a drum Kit for the snapshot: kick on the beats (a lock of 77 on P1 at step 1), snare on
		// 5 and 13, closed hats on the eighths, open hats between them, clap, rimshot, tom and cowbell.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			using Access = mdJucePlugin::ControllerAutomationTestAccess;
			const auto kitMachines = Access::machines(md);
			Access::setMachines(md, {16, 17, 22, 23, 19, 20, 18, 21, 24, 25, 26, 32, 33, 50, 51, 0});
			std::array<uint32_t, 16> trigs{};
			trigs[0] = 0x11111111u;
			trigs[1] = 0x10101010u;
			trigs[2] = 0x55555555u;
			trigs[3] = 0x44444444u;
			trigs[4] = 0x10001000u;
			trigs[5] = 0x00480048u;
			trigs[6] = 0x54000000u;
			trigs[7] = 0x04000400u;
			require(md.requestPattern(), "pattern read refused");
			md.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, 0x04, 20, 0xf7}, synthLib::MidiEventSource::Device);
			// The PAS test left writes waiting for their read-back: each dump answers one
			for(int reply = 0; reply < 8 && (!md.getPattern() || md.getPattern()->slot != 20); ++reply)
				md.parseSysexMessage(mdAutomationTest::makeMdPatternDump(20, 32, trigs, 77), synthLib::MidiEventSource::Device);
			require(md.getPattern() && md.getPattern()->slot == 20, "pattern B05 not read");
			element(doc, "editTrack0").Click();
			tabButton(doc, "mdEdit", "2").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			const auto cell = [&](const int _track, const int _step) -> Rml::Element&
			{
				return element(doc, "mdPlayStep" + std::to_string(_track) + "_" + std::to_string(_step));
			};
			require(text("mdPlayInfo") == "pattern B05 · 32 pas · 47 trigs", "JOUER does not name the pattern: \"" + text("mdPlayInfo") + "\"");
			require(text("mdPlayHead0") == "1" && text("mdPlayHead31") == "32" && text("mdPlayTrack1") == "02 TRX-SD",
				"JOUER does not number the steps or name the tracks");
			require(cell(0, 0).IsClassSet("mdEdStepTrig") && cell(0, 0).IsClassSet("mdPlayLocked") && cell(0, 4).IsClassSet("mdEdStepTrig")
				&& !cell(0, 4).IsClassSet("mdPlayLocked") && !cell(0, 1).IsClassSet("mdEdStepTrig") && cell(2, 4).IsClassSet("mdEdStepTrig")
				&& !cell(2, 1).IsClassSet("mdEdStepTrig") && cell(3, 30).IsClassSet("mdEdStepTrig") && !cell(8, 0).IsClassSet("mdEdStepTrig"),
				"JOUER grid does not show the pattern");
			const auto& lane = mdJucePlugin::EditorIdentityTestAccess::pattern(*editor);
			require(lane.barValue(0) == 77 && lane.barLocked(0) && lane.barValue(4) == 64 && !lane.barLocked(4) && lane.barValue(1) == -1,
				"lane does not show the lock and the Kit value");
			// Track 1 holds TRX-BD, whose first parameter is PTCH
			require(text("mdPlayLaneInfo") == "piste 01 · PTCH : 1 lock · kit 64" && text("mdPlayParam0") == "PTCH · 1",
				"lane does not count the locks: \"" + text("mdPlayLaneInfo") + "\"");
			// The sequencer plays step 5, then 6: its column is lit, the previous one no longer; stopped, none
			Access::setPlayingStep(md, uint8_t{4});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(cell(0, 4).IsClassSet("mdPlayNow") && cell(15, 4).IsClassSet("mdPlayNow")
				&& element(doc, "mdPlayHead4").IsClassSet("mdPlayNow") && !cell(0, 3).IsClassSet("mdPlayNow"),
				"the playing step's column is not lit");
			// Its trigs light their tracks' LEDs in the track strip, in JOUER and in MIX, for a moment
			const auto stepPattern = md.getPattern();
			const auto led = [&](const std::string& _id) { return element(doc, _id).IsClassSet("mdTrackHit"); };
			const auto mixLed = [&](const int _track)
			{
				Rml::ElementList buttons;
				element(doc, "mdEdLevelRow" + std::to_string(_track)).GetElementsByTagName(buttons, "button");
				return !buttons.empty() && buttons.front()->IsClassSet("mdTrackHit");
			};
			for(int track = 0; track < 16; ++track)
			{
				const auto n = std::to_string(track);
				const bool trig = stepPattern->hasTrig(static_cast<uint8_t>(track), 4);
				require(led("mdEdTrackLed" + n) == trig && led("mdPlayLed" + n) == trig && mixLed(track) == trig,
					"track " + std::to_string(track + 1) + "'s LEDs do not follow its trig on the playing step");
			}
			require(led("mdEdTrackLed0") && !led("mdEdTrackLed3"), "the test pattern lost its trigs on step 5");
			snap("-play");
			mdJucePlugin::EditorIdentityTestAccess::updateActivity(*editor,
				juce::Time::getMillisecondCounterHiRes() + mdJucePlugin::TrackActivity::LitMilliseconds + 1.0);
			require(!led("mdEdTrackLed0") && !led("mdPlayLed0") && !mixLed(0), "a track's LED stays lit");
			Access::setPlayingStep(md, uint8_t{5});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(cell(0, 5).IsClassSet("mdPlayNow") && !cell(0, 4).IsClassSet("mdPlayNow")
				&& !element(doc, "mdPlayHead4").IsClassSet("mdPlayNow"), "the lit column did not follow the playing step");
			// SON's PAS lights the same step
			tabButton(doc, "mdEdit", "0").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(element(doc, "mdEdStep5").IsClassSet("mdEdStepNow") && !element(doc, "mdEdStep4").IsClassSet("mdEdStepNow")
				&& mdJucePlugin::EditorIdentityTestAccess::steps(*editor).getShownPlayStep() == 5, "PAS does not light the playing step");
			tabButton(doc, "mdEdit", "2").Click();
			context.Update();
			Access::setPlayingStep(md, std::nullopt);
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(!cell(0, 5).IsClassSet("mdPlayNow") && lane.getShownPlayStep() == -1, "a column stays lit once stopped");
			element(doc, "mdPlayParam17").Click();
			context.Update();
			require(text("mdPlayLaneInfo") == "piste 01 · VOL : 0 locks · kit 64" && !lane.barLocked(0) && lane.barValue(0) == 64,
				"choosing VOL did not change the lane");
			// The lane under the mouse: a press locks VOL on step 1 (a trig) at the height pressed, a double click
			// clears the lock; written once the edits pause. The top of a lane is 127, its foot 0.
			{
				auto& edit = mdJucePlugin::EditorIdentityTestAccess::editPattern(*editor);
				namespace columns = mdJucePlugin::stepColumns;
				require(columns::laneValueAt(0.0f, 928.0f, 200.0f) == 127 && columns::laneValueAt(200.0f, 928.0f, 200.0f) == 0
					&& columns::columnAt(30.0f, 928.0f) == 1 && columns::columnAt(-1.0f, 928.0f) == -1,
					"a lane does not map the mouse to its steps and values");
				edit.editLock(0, uint8_t{100});
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(md.getPattern()->lock(0, 17, 0) == uint8_t{100} && lane.barLocked(0) && lane.barValue(0) == 100
					&& mdJucePlugin::ControllerAutomationTestAccess::patternWriteWaiting(md)
					&& text("mdPlayLaneInfo").rfind("piste 01 · VOL : 1 lock", 0) == 0, "a press in the lane did not lock VOL on step 1");
				edit.editLock(1, uint8_t{100});
				require(!md.getPattern()->lock(0, 17, 1), "a lock set on a step without a trig");
				edit.editLock(0, std::nullopt);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(!md.getPattern()->lock(0, 17, 0) && !lane.barLocked(0) && lane.barValue(0) == 64, "a double click did not clear the lock");
				mdJucePlugin::ControllerAutomationTestAccess::pauseEdits(md);
			}
			// A track name makes it the edited track; its lane follows
			element(doc, "mdPlayTrack1").Click();
			context.Update();
			require(controller.getCurrentPart() == 1 && element(doc, "mdPlayTrack1").IsClassSet("mdEdSelected")
				&& lane.barValue(0) == -1 && lane.barValue(4) == 64, "a track name did not take the lane");
			// A click sets a trig, a second one clears it; the pattern is written once the clicks pause
			cell(1, 3).Click();
			context.Update();
			require(md.getPattern()->hasTrig(1, 3) && cell(1, 3).IsClassSet("mdEdStepTrig")
				&& mdJucePlugin::ControllerAutomationTestAccess::patternWriteWaiting(md), "a click did not set the trig");
			cell(1, 3).Click();
			context.Update();
			require(!md.getPattern()->hasTrig(1, 3) && !cell(1, 3).IsClassSet("mdEdStepTrig"), "a second click did not clear the trig");
			mdJucePlugin::ControllerAutomationTestAccess::pauseEdits(md);
			require(md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::Pending
				&& !mdJucePlugin::ControllerAutomationTestAccess::patternWriteWaiting(md), "the clicks were not written once they paused");
			// TOUT EFFACER: the first click asks to confirm, the second clears every trig and writes the pattern
			element(doc, "mdPlayClear").Click();
			context.Update();
			require(lane.isClearArmed() && text("mdPlayClear") == "CONFIRMER ?" && md.getPattern()->hasTrig(0, 0),
				"TOUT EFFACER cleared without asking");
			element(doc, "mdPlayClear").Click();
			context.Update();
			const auto cleared = md.getPattern();
			require(cleared && std::all_of(cleared->trigs.begin(), cleared->trigs.end(), [](const auto _trigs) { return _trigs == 0; })
				&& !lane.isClearArmed() && text("mdPlayClear") == "TOUT EFFACER" && !cell(0, 0).IsClassSet("mdEdStepTrig")
				&& md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::Pending, "TOUT EFFACER did not clear and write the pattern");

			// A pattern over 32 steps: PAS 33–64 shows steps 33 to 64, numbered from 33, past the length greyed;
			// the playing step and a click there are steps 33 to 64 too
			std::array<uint64_t, 16> longTrigs{};
			longTrigs[0] = 1u | uint64_t{1} << 40;	// track 1: steps 1 and 41
			const auto readPattern = [&](const pluginLib::SysEx& _dump, const uint8_t _steps)
			{
				require(md.requestPattern(), "pattern read refused");
				// Writes waiting for their read-back: each dump answers one
				for(int reply = 0; reply < 8 && (!md.getPattern() || md.getPattern()->steps != _steps); ++reply)
					md.parseSysexMessage(_dump, synthLib::MidiEventSource::Device);
				require(md.getPattern() && md.getPattern()->steps == _steps, "pattern of " + std::to_string(_steps) + " steps not read");
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				context.Update();
			};
			readPattern(mdAutomationTest::makeMdPatternDump(20, 48, longTrigs), 64);
			require(lane.getStepPage() == 0 && !element(doc, "mdPlayPage1").IsClassSet("mdEdOff")
				&& text("mdPlayInfo") == "pattern B05 · 48 pas · 2 trigs", "a long pattern does not offer its steps 33 to 64: \""
				+ text("mdPlayInfo") + "\"");
			element(doc, "mdPlayPage1").Click();
			context.Update();
			require(lane.getStepPage() == 1 && text("mdPlayHead0") == "33" && text("mdPlayHead31") == "64"
				&& cell(0, 8).IsClassSet("mdEdStepTrig") && !cell(0, 0).IsClassSet("mdEdStepTrig")
				&& cell(0, 16).IsClassSet("mdEdStepOut") && !cell(0, 15).IsClassSet("mdEdStepOut")
				&& element(doc, "mdPlayPage1").IsClassSet("mdEdSelected"), "PAS 33–64 does not show steps 33 to 64");
			Access::setPlayingStep(md, uint8_t{40});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(cell(0, 8).IsClassSet("mdPlayNow") && lane.getShownPlayStep() == 8, "step 41 playing does not light its column");
			snap("-play64");
			Access::setPlayingStep(md, std::nullopt);
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			cell(0, 9).Click();
			context.Update();
			require(md.getPattern()->hasTrig(0, 41) && cell(0, 9).IsClassSet("mdEdStepTrig"), "a click on PAS 33–64 did not set step 42");
			mdJucePlugin::ControllerAutomationTestAccess::pauseEdits(md);
			// SON's PAS on the same steps: OUVRIR DANS SON keeps the page JOUER shows; there a focused step past
			// 32 takes a lock, and the other page clears the focus
			{
				const auto& grid = mdJucePlugin::EditorIdentityTestAccess::steps(*editor);
				const auto step = [&](const int _column) -> Rml::Element& { return element(doc, "mdEdStep" + std::to_string(_column)); };
				element(doc, "mdPlayTrack0").Click();
				element(doc, "mdPlayOpen").Click();
				context.Update();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(visible(element(doc, "mdEdPageSound")) && grid.getStepPage() == 1
					&& element(doc, "mdEdStepPage1").IsClassSet("mdEdSelected") && text("mdEdStep0") == "33" && text("mdEdStep31") == "64"
					&& step(8).IsClassSet("mdEdStepTrig") && step(9).IsClassSet("mdEdStepTrig") && !step(0).IsClassSet("mdEdStepTrig")
					&& step(16).IsClassSet("mdEdStepOut") && !step(15).IsClassSet("mdEdStepOut")
					&& text("mdEdStepsInfo").rfind("pattern B05 · 48 pas · 3 trigs · envoi", 0) == 0,
					"SON's PAS does not show steps 33 to 64: \"" + text("mdEdStepsInfo") + "\"");
				Access::setPlayingStep(md, uint8_t{40});
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(step(8).IsClassSet("mdEdStepNow") && grid.getShownPlayStep() == 8, "SON's PAS does not light step 41 playing");
				Access::setPlayingStep(md, std::nullopt);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				step(8).Click();
				context.Update();
				auto& lockKnob = element(doc, "mdEdLockKnob_FilterBase");
				require(grid.getFocus() == 40 && step(8).IsClassSet("mdEdStepFocus") && visible(lockKnob), "step 41 not focused for its locks");
				juceRmlUi::ElemValue::setValue(&lockKnob, 55.0f);
				context.Update();
				// FilterBase: lock row parameter 12
				require(md.getPattern()->lock(0, 12, 40) == 55 && text("mdEdLock_FilterBase") == "55"
					&& text("mdEdStepsInfo").rfind("pattern B05 · 48 pas · 3 trigs · pas 41 : 1 lock", 0) == 0,
					"a lock on step 41 not written: \"" + text("mdEdStepsInfo") + "\"");
				snap("-steps64");
				element(doc, "mdEdStepPage0").Click();
				context.Update();
				require(grid.getStepPage() == 0 && grid.getFocus() == -1 && text("mdEdStep0") == "1" && step(0).IsClassSet("mdEdStepTrig")
					&& !step(8).IsClassSet("mdEdStepFocus") && !visible(lockKnob), "PAS 1–32 kept the focus on step 41");
				tabButton(doc, "mdEdit", "2").Click();
				context.Update();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			}
			// LONGUEUR shows the length; 40 steps grey steps 41 and up, their trigs kept
			require(text("mdPlayLength") == "LONGUEUR 48", "LONGUEUR does not show the pattern's length");
			// As the menu does: the length, then the write once the edits pause
			require(md.setPatternLength(40), "length 40 refused");
			md.sendPatternSoon();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(text("mdPlayLength") == "LONGUEUR 40" && cell(0, 8).IsClassSet("mdEdStepOut") && !cell(0, 7).IsClassSet("mdEdStepOut")
				&& md.getPattern()->hasTrig(0, 41) && text("mdPlayInfo") == "pattern B05 · 40 pas · 1 trig",
				"a length of 40 not shown: \"" + text("mdPlayInfo") + "\"");
			mdJucePlugin::ControllerAutomationTestAccess::pauseEdits(md);
			// A pattern of 32 steps again: steps 1 to 32, PAS 33–64 off
			readPattern(mdAutomationTest::makeMdPatternDump(20, 32, trigs, 77), 32);
			require(lane.getStepPage() == 0 && text("mdPlayHead0") == "1" && element(doc, "mdPlayPage1").IsClassSet("mdEdOff")
				&& cell(0, 0).IsClassSet("mdEdStepTrig") && text("mdPlayLength") == "LONGUEUR 32", "a short pattern still shows steps 33 to 64");

			// COPIER VERS: a slot the library does not know asks to confirm (REMPLACER B06 ?); the second click
			// copies the pattern shown there, read back as sent; read back different, the copy is refused
			{
				using PatternCopy = mdJucePlugin::Controller::PatternCopy;
				const auto endsWith = [&](const std::string& _text, const std::string& _end)
				{
					return _text.size() >= _end.size() && _text.compare(_text.size() - _end.size(), _end.size(), _end) == 0;
				};
				require(text("mdPlayCopy") == "COPIER VERS…" && !element(doc, "mdPlayCopy").IsClassSet("mdEdOff"), "COPIER VERS not offered");
				mdJucePlugin::EditorIdentityTestAccess::copyPatternTo(*editor, 21);
				context.Update();
				require(lane.isCopyArmed() && text("mdPlayCopy") == "REMPLACER B06 ?" && md.getPatternCopy().state == PatternCopy::None,
					"a slot not known empty did not ask to confirm");
				element(doc, "mdPlayCopy").Click();
				context.Update();
				const auto copy = md.getPatternCopy();
				require(!lane.isCopyArmed() && copy.state == PatternCopy::Writing && copy.from == 20 && copy.to == 21,
					"REMPLACER did not copy B05 to B06");
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(endsWith(text("mdPlayInfo"), "32 pas · 47 trigs · copie vers B06…") && text("mdPlayCopy") == "COPIE…",
					"the line does not tell the copy: \"" + text("mdPlayInfo") + "\"");
				// The firmware: B06 read back as the copy
				md.parseSysexMessage(mdAutomationTest::makeMdPatternDump(21, 32, trigs, 77), synthLib::MidiEventSource::Device);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(md.getPatternCopy().state == PatternCopy::Copied && endsWith(text("mdPlayInfo"), "47 trigs · copié sur B06")
					&& text("mdPlayCopy") == "COPIER VERS…", "the copy read back as sent not told: \"" + text("mdPlayInfo") + "\"");
				// B07 read back without its trigs
				mdJucePlugin::EditorIdentityTestAccess::copyPatternTo(*editor, 22);
				element(doc, "mdPlayCopy").Click();
				md.parseSysexMessage(mdAutomationTest::makeMdPatternDump(22, 32, std::array<uint32_t, 16>{}, 77),
					synthLib::MidiEventSource::Device);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(md.getPatternCopy().state == PatternCopy::Refused && endsWith(text("mdPlayInfo"), " · copie sur B07 refusée"),
					"a copy read back different not refused: \"" + text("mdPlayInfo") + "\"");
				snap("-play-copy");
			}

			element(doc, "mdPlayParam0").Click();
			element(doc, "mdPlayTrack0").Click();
			// OUVRIR DANS SON shows the edited track's sound
			element(doc, "mdPlayOpen").Click();
			context.Update();
			require(visible(element(doc, "mdEdPageSound")) && controller.getCurrentPart() == 0, "OUVRIR DANS SON did not open SON");
			Access::setMachines(md, kitMachines);
		}
#else
		// JOUER (Monomachine): the current pattern under the steps' numbers, each trig with its note, a trig
		// with locks in orange, steps 33 to 64 on the second page; the edited track's piano roll, and the lane
		// of one of its parameters: the Kit value in grey, a lock in orange, on the steps that play. For the
		// snapshots, B05 of 48 steps on a Kit of bass, lead, pad, arpeggio, hats and kick: the bass with AMP
		// VOL locked at 90 on step 1 and FILT BASE locked on steps 9, 25 and 41.
		{
			using View = mdJucePlugin::MmPatternView;
			using Access = mdJucePlugin::ControllerAutomationTestAccess;
			auto& mm = dynamic_cast<mdJucePlugin::Controller&>(controller);
			const auto kitMachines = Access::machines(mm);
			Access::setMachines(mm, {4, 3, 6, 8, 2, 5});
			auto pattern = md::automation::sysex::MmPatternEditor::fromDump(mdAutomationTest::makeMmPatternDump(20, 48));
			require(pattern.has_value(), "MM test pattern not built");
			using Notes = std::vector<std::pair<uint8_t, uint8_t>>;
			const auto play = [&](const uint8_t _track, const Notes& _notes)
			{
				for(const auto& [step, note] : _notes)
					require(pattern->setTrig(_track, step, note), "MM test pattern not built");
			};
			play(0, {{0, 36}, {3, 36}, {6, 39}, {8, 36}, {10, 43}, {12, 34}, {14, 36}, {16, 36}, {19, 36}, {22, 39}, {24, 36},
				{26, 43}, {28, 34}, {30, 36}, {32, 36}, {35, 36}, {38, 41}, {40, 36}, {42, 43}, {44, 46}, {46, 48}});
			play(1, {{0, 67}, {2, 70}, {4, 72}, {7, 70}, {10, 67}, {12, 65}, {14, 67}, {16, 75}, {20, 74}, {24, 72}, {28, 70},
				{32, 67}, {34, 70}, {36, 72}, {39, 74}, {42, 75}, {44, 77}, {46, 79}});
			play(2, {{0, 60}, {16, 63}, {32, 65}});
			constexpr uint8_t arpeggio[] = {72, 67, 75, 67};
			for(uint8_t step = 0; step < 48; step += 2)
				play(3, {{step, arpeggio[step / 2 % 4]}});
			for(uint8_t step = 2; step < 48; step += 4)
				play(4, {{step, 60}});
			for(uint8_t step = 0; step < 48; step += 4)
				play(5, {{step, 24}});
			const auto volume = md::automation::sysex::mmLockBit(md::automation::monomachine::Amplification, 5);
			const auto base = md::automation::sysex::mmLockBit(md::automation::monomachine::Filter, 0);
			require(pattern->setLock(0, volume, 0, 90) && pattern->setLock(0, base, 8, 40) && pattern->setLock(0, base, 24, 70)
				&& pattern->setLock(0, base, 40, 100), "MM test locks not set");
			require(mm.requestPattern(), "pattern read refused");
			mm.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, 0x72, 0x04, 20, 0xf7}, synthLib::MidiEventSource::Device);
			const auto dump = pattern->toDump();
			mm.parseSysexMessage(pluginLib::SysEx(dump.begin(), dump.end()), synthLib::MidiEventSource::Device);
			require(mm.getMmPattern() && mm.getMmPattern()->slot == 20, "pattern B05 not read");
			element(doc, "editTrack0").Click();
			tabButton(doc, "mdEdit", "2").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			const auto cell = [&](const int _track, const int _step) -> Rml::Element&
			{
				return element(doc, "mmPlayStep" + std::to_string(_track) + "_" + std::to_string(_step));
			};
			require(text("mmPlayInfo") == "pattern B05 · 48 pas · 90 trigs", "JOUER does not name the pattern: \"" + text("mmPlayInfo") + "\"");
			require(text("mmPlayHead0") == "1" && text("mmPlayHead31") == "32" && text("mmPlayTrack0") == "01 SWAVE-SAW",
				"JOUER does not number the steps or name the tracks");
			require(text("mmPlayStep0_0") == "C2" && cell(0, 0).IsClassSet("mdPlayLocked") && cell(0, 8).IsClassSet("mdPlayLocked")
				&& text("mmPlayStep0_3") == "C2" && cell(0, 3).IsClassSet("mdEdStepTrig") && !cell(0, 3).IsClassSet("mdPlayLocked")
				&& text("mmPlayStep0_1").empty() && !cell(0, 1).IsClassSet("mdEdStepTrig") && text("mmPlayStep2_0") == "C4"
				&& text("mmPlayStep1_4") == "C5", "JOUER grid does not show the notes");
			const auto& view = mdJucePlugin::EditorIdentityTestAccess::mmPattern(*editor);
			require(view.rollNote(0) == 36 && view.rollNote(6) == 39 && view.rollNote(1) == -1
				&& View::rollRange(*mm.getMmPattern(), 0) == std::pair<uint8_t, uint8_t>{29, 53}
				&& View::noteName(60) == "C4" && View::noteName(39) == "D#2", "the roll does not show track 1's notes");
			require(text("mmPlayRollInfo") == "piste 01 · pas 1 à 32 · 14 notes · de F1 à F3", "roll line wrong: \"" + text("mmPlayRollInfo") + "\"");
			require(view.barValue(0) == 90 && view.barLocked(0) && view.barValue(3) >= 0 && !view.barLocked(3) && view.barValue(1) == -1
				&& text("mmPlayLaneInfo").rfind("piste 01 · AMP VOL : 1 lock · kit ", 0) == 0
				&& text("mmPlayParamPage1") == "AMP · 1" && text("mmPlayParamPage2") == "FILT · 3" && text("mmPlayParam5") == "VOL · 1"
				&& element(doc, "mmPlayParam5").IsClassSet("mdEdSelected"), "lane does not show AMP VOL's lock: \"" + text("mmPlayLaneInfo") + "\"");
			// The sequencer plays step 4: the tracks with a trig there light their LEDs (track 1 has one)
			Access::setPlayingStep(mm, uint8_t{3});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			const auto stepPattern = mm.getMmPattern();
			for(int track = 0; track < 6; ++track)
			{
				const auto n = std::to_string(track);
				const bool trig = stepPattern->hasTrig(static_cast<uint8_t>(track), 3);
				require(element(doc, "mdEdTrackLed" + n).IsClassSet("mdTrackHit") == trig
					&& element(doc, "mmPlayLed" + n).IsClassSet("mdTrackHit") == trig,
					"track " + std::to_string(track + 1) + "'s LEDs do not follow its trig on the playing step");
			}
			require(element(doc, "mmPlayLed0").IsClassSet("mdTrackHit"), "track 1's LED not lit by its trig on step 4");
			// The sequencer plays step 5: its column is lit
			Access::setPlayingStep(mm, uint8_t{4});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(cell(0, 4).IsClassSet("mdPlayNow") && cell(5, 4).IsClassSet("mdPlayNow") && element(doc, "mmPlayHead4").IsClassSet("mdPlayNow")
				&& view.getShownPlayColumn() == 4, "the playing step's column is not lit");
			snap("-play");
			// Step 37, on the steps not shown: no column until they are
			Access::setPlayingStep(mm, uint8_t{36});
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(view.getShownPlayColumn() == -1 && !cell(0, 4).IsClassSet("mdPlayNow"), "a column is lit for a step not shown");
			// FILT keeps the index (DEC, no lock); BASE shows its three locks
			element(doc, "mmPlayParamPage2").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(text("mmPlayLaneInfo").rfind("piste 01 · FILT DEC : 0 locks", 0) == 0 && !view.barLocked(0) && text("mmPlayParam5") == "DEC"
				&& text("mmPlayParam0") == "BASE · 3", "choosing FILT did not change the lane");
			element(doc, "mmPlayParam0").Click();
			tabButton(doc, "mmPlayBottom", "1").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(visible(element(doc, "mmPlayLanePage")) && !visible(element(doc, "mmPlayRollPage")) && view.barValue(8) == 40
				&& view.barLocked(8) && view.barLocked(24) && !view.barLocked(0), "LANE does not show FILT BASE's locks");
			snap("-lane");
			tabButton(doc, "mmPlayBottom", "0").Click();
			// Steps 33 to 64: F2 on step 39, past the length from step 49
			element(doc, "mmPlayPage1").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(text("mmPlayStep0_6") == "F2" && view.rollNote(6) == 41 && cell(0, 16).IsClassSet("mdEdStepOut")
				&& !cell(0, 15).IsClassSet("mdEdStepOut") && text("mmPlayHead0") == "33" && text("mmPlayHead16") == "49"
				&& element(doc, "mmPlayHead16").IsClassSet("mdEdStepOut") && element(doc, "mmPlayPage1").IsClassSet("mdEdSelected"),
				"steps 33 to 64 not shown");
			require(view.getShownPlayColumn() == 4 && cell(0, 4).IsClassSet("mdPlayNow"), "step 37 not lit once steps 33 to 64 show");
			Access::setPlayingStep(mm, std::nullopt);
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			require(view.getShownPlayColumn() == -1 && !cell(0, 4).IsClassSet("mdPlayNow"), "a column stays lit once stopped");
			element(doc, "mmPlayPage0").Click();
			// A track name makes it the edited track; the roll follows
			element(doc, "mmPlayTrack2").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(controller.getCurrentPart() == 2 && element(doc, "mmPlayTrack2").IsClassSet("mdEdSelected")
				&& view.rollNote(0) == 60 && view.rollNote(4) == -1, "a track name did not take the roll");
			element(doc, "mmPlayParamPage1").Click();
			element(doc, "mmPlayParam5").Click();
			element(doc, "mmPlayTrack0").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();

			// Editing: a click on step 2 of track 1 (no trig) puts the note of the trig before it (C2); a click in
			// the roll moves it to D#2, a second one there clears it, a third puts F2. Written once the clicks
			// pause, through the Device's SYSEX RECV write; the pattern read back as sent, the write is told.
			{
				using PatternWrite = mdJucePlugin::Controller::PatternWrite;
				using PatternCopy = mdJucePlugin::Controller::PatternCopy;
				auto& edit = mdJucePlugin::EditorIdentityTestAccess::editMmPattern(*editor);
				const auto readBack = [&](md::automation::sysex::MmPatternEditor& _sent)
				{
					const auto sent = _sent.toDump();
					mm.parseSysexMessage(pluginLib::SysEx(sent.begin(), sent.end()), synthLib::MidiEventSource::Device);
					mdJucePlugin::EditorIdentityTestAccess::present(*editor);
					context.Update();
				};
				cell(0, 1).Click();
				context.Update();
				require(mm.getMmPattern()->note(0, 1) == uint8_t{36} && Access::patternWriteWaiting(mm),
					"a click on a step did not set a trig playing the note before it");
				// The roll of track 1: F1 (29) to F3 (53), a row a semitone; on a roll of 320 by 250, step 2's
				// column and F2's row
				require(edit.rollCell(15.0f, 125.0f, 320.0f, 250.0f) == std::make_pair(uint8_t{1}, uint8_t{41})
					&& !edit.rollCell(-1.0f, 125.0f, 320.0f, 250.0f), "a click in the roll does not land on its step and note");
				edit.placeNote(1, 39);
				require(mm.getMmPattern()->note(0, 1) == uint8_t{39}, "a click in the roll did not move the note");
				edit.placeNote(1, 39);
				require(!mm.getMmPattern()->hasTrig(0, 1), "a click on the note there did not clear it");
				edit.placeNote(1, 41);
				Access::pauseEdits(mm);
				require(mm.getMmPattern()->note(0, 1) == uint8_t{41} && mm.getPatternWrite() == PatternWrite::Pending
					&& !Access::patternWriteWaiting(mm), "the edits were not written once they paused");
				require(pattern->setTrig(0, 1, 41), "MM test pattern not edited");
				readBack(*pattern);
				require(mm.getPatternWrite() == PatternWrite::Written && text("mmPlayStep0_1") == "F2",
					"the write read back as sent not told");

				// LANE (AMP VOL): a press locks it on step 2 at the height pressed, a double click clears the lock, a
				// step without a trig takes none; the lock is written as the notes are
				const auto volume = md::automation::sysex::mmLockBit(md::automation::monomachine::Amplification, 5);
				tabButton(doc, "mmPlayBottom", "1").Click();
				context.Update();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				edit.editLock(1, uint8_t{64});
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(mm.getMmPattern()->lock(0, volume, 1) == uint8_t{64} && view.barLocked(1) && view.barValue(1) == 64
					&& Access::patternWriteWaiting(mm), "a press in the lane did not lock AMP VOL on step 2");
				edit.editLock(1, std::nullopt);
				edit.editLock(2, uint8_t{64});
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(!mm.getMmPattern()->lock(0, volume, 1) && !view.barLocked(1) && !mm.getMmPattern()->lock(0, volume, 2),
					"a double click did not clear the lock, or a step without a trig took one");
				edit.editLock(1, uint8_t{100});
				Access::pauseEdits(mm);
				require(mm.getPatternWrite() == PatternWrite::Pending && pattern->setLock(0, volume, 1, 100), "the lock not written");
				readBack(*pattern);
				require(mm.getPatternWrite() == PatternWrite::Written && view.barValue(1) == 100, "the lock not read back as written");

				// The roll moved: OCTAVE + shows an octave higher, a click there puts C4 on step 3, and the roll stays
				// where it was moved; OCTAVE – back down; another track shows the notes it plays
				tabButton(doc, "mmPlayBottom", "0").Click();
				context.Update();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				const auto fitted = view.getRollRange();
				element(doc, "mmPlayRollUp").Click();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(view.getRollRange() == std::make_pair(static_cast<uint8_t>(fitted.first + 12), static_cast<uint8_t>(fitted.second + 12)),
					"OCTAVE + did not show an octave higher");
				edit.placeNote(2, 60);
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(mm.getMmPattern()->note(0, 2) == uint8_t{60} && view.getRollRange().first == fitted.first + 12,
					"a note put in the moved roll did not stay, or the roll moved back");
				element(doc, "mmPlayRollDown").Click();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(view.getRollRange().first == fitted.first, "OCTAVE – did not show an octave lower");
				element(doc, "mmPlayTrack1").Click();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				element(doc, "mmPlayTrack0").Click();
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				require(view.getRollRange() == View::rollRange(*mm.getMmPattern(), 0), "another track did not bring the roll back to its notes");

				// TOUT EFFACER, clicked twice: every trig goes, and the pattern is written
				element(doc, "mmPlayClear").Click();
				element(doc, "mmPlayClear").Click();
				context.Update();
				const auto cleared = mm.getMmPattern();
				require(cleared && std::all_of(cleared->trigs.begin(), cleared->trigs.end(), [](const uint64_t _t) { return _t == 0; })
					&& mm.getPatternWrite() == PatternWrite::Pending, "TOUT EFFACER did not clear and write the pattern");
				pattern->clear();
				readBack(*pattern);
				require(mm.getPatternWrite() == PatternWrite::Written && text("mmPlayInfo").rfind("pattern B05 · 48 pas · 0 trigs", 0) == 0,
					"the cleared pattern not read back: \"" + text("mmPlayInfo") + "\"");

				// COPIER VERS B06: confirmed, sent with B06's number through the Device's write, read back as sent
				edit.copyTo(21);
				context.Update();
				require(edit.isCopyArmed() && text("mmPlayCopy") == "REMPLACER B06 ?", "COPIER VERS did not ask to confirm");
				element(doc, "mmPlayCopy").Click();
				context.Update();
				const auto copy = mm.getPatternCopy();
				require(copy.state == PatternCopy::Writing && copy.from == 20 && copy.to == 21, "REMPLACER did not copy B05 to B06");
				require(pattern->setSlot(21), "MM test pattern not numbered B06");
				readBack(*pattern);
				require(mm.getPatternCopy().state == PatternCopy::Copied && text("mmPlayInfo").find("copié sur B06") != std::string::npos,
					"the copy read back not told: \"" + text("mmPlayInfo") + "\"");
				snap("-play-edit");
			}
			Access::setMachines(mm, kitMachines);
		}
#endif

		// JOUER, CHAÎNE: the project's chain, built from the machine's pattern, and what it does, under a
		// tab of the bottom block: the second on the Machinedrum, the third on the Monomachine.
		{
			constexpr bool machinedrum = g_model == md::MachineModel::Machinedrum;
			const std::string bottom = machinedrum ? "mdPlayBottom" : "mmPlayBottom";
			using Entries = std::vector<mdJucePlugin::ChainControl::Entry>;
			using View = mdJucePlugin::ChainView;
			using State = mdJucePlugin::ChainControl::State;
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			auto& chain = processor.getChainControl();
			const auto present = [&]
			{
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				context.Update();
			};
			const auto has = [&](const std::string& _id, const char* _part) { return text(_id).find(_part) != std::string::npos; };
			tabButton(doc, "mdEdit", "2").Click();
			tabButton(doc, bottom, machinedrum ? "1" : "2").Click();
			context.Update();
			present();
			require(visible(element(doc, "mdPlayChainPage")) && !visible(element(doc, machinedrum ? "mdPlayLanePage" : "mmPlayRollPage")),
				"CHAÎNE not shown");
			require(text("mdChainState") == "chaîne inactive" && text("mdChainSlot0") == "—"
				&& element(doc, "mdChainSlot0").IsClassSet("mdEdUnread"), "an empty chain not shown so");

			// The machine on A04: AJOUTER adds it, twice, then the second becomes A05 and the first plays twice
			const uint8_t product = g_model == md::MachineModel::Monomachine ? 0x03 : 0x02;
			md.parseSysexMessage({0xf0, 0x00, 0x20, 0x3c, product, 0x00, 0x72, 0x04, 3, 0xf7}, synthLib::MidiEventSource::Device);
			present();
			require(text("mdChainAdd") == "AJOUTER A04", "AJOUTER does not name the machine's pattern: \"" + text("mdChainAdd") + "\"");
			require(element(doc, "mdChainBank0").IsClassSet("mdEdUnread"), "the pattern selector active without an entry");
			element(doc, "mdChainAdd").Click();
			element(doc, "mdChainPassesUp").Click();
			element(doc, "mdChainAdd").Click();
			// The selector: bank B keeps the number (B04), number 05 keeps the bank (B05), bank A again (A05)
			element(doc, "mdChainBank1").Click();
			require(chain.getEntries() == Entries{{3, 2}, {19, 1}}, "bank B did not make the chosen entry B04");
			element(doc, "mdChainNumber4").Click();
			element(doc, "mdChainBank0").Click();
			present();
			require(chain.getEntries() == Entries{{3, 2}, {4, 1}}, "the chain is not A04 x2, A05");
			require(element(doc, "mdChainBank0").IsClassSet("mdEdSelected") && element(doc, "mdChainNumber4").IsClassSet("mdEdSelected")
				&& !element(doc, "mdChainNumber3").IsClassSet("mdEdSelected") && !element(doc, "mdChainBank1").IsClassSet("mdEdSelected"),
				"the selector does not show the chosen entry's A05");
			require(has("mdChainSlot0", "A04") && has("mdChainSlot0", "×2") && has("mdChainSlot0", "…")
				&& has("mdChainSlot1", "A05") && element(doc, "mdChainSlot1").IsClassSet("mdEdSelected")
				&& !element(doc, "mdChainSlot0").IsClassSet("mdEdSelected"), "slots do not show A04 x2 and A05 chosen");
			require(text("mdChainState") == "chaîne inactive", "the chain turned itself on");
			// A05 moved first, then removed; A04 chosen by a click
			element(doc, "mdChainMoveLeft").Click();
			require(chain.getEntries() == Entries{{4, 1}, {3, 2}}, "‹ DÉPLACER did not move A05 first");
			element(doc, "mdChainRemove").Click();
			element(doc, "mdChainSlot0").Click();
			present();
			require(chain.getEntries() == Entries{{3, 2}} && element(doc, "mdChainSlot0").IsClassSet("mdEdSelected"),
				"RETIRER did not leave A04 x2");

			// Turned on: the length first, then the host
			element(doc, "mdChainEnable").Click();
			present();
			require(chain.isEnabled() && element(doc, "mdChainEnable").IsPseudoClassSet("checked")
				&& text("mdChainState") == "lecture de la longueur des patterns…", "a chain without length not waiting for it");
			chain.setLength(3, 32);
			chain.update(false);
			present();
			require(text("mdChainState") == "en attente : la machine ne suit pas l'hôte (SYSTÈME, SUIVRE L'HÔTE)"
				&& has("mdChainSlot0", "32 pas"), "a chain while the machine does not follow the host not said so");
			require(chain.update(true) == State::Playing, "the chain does not play while following");
			present();
			require(text("mdChainState") == "prête : joue avec le transport de l'hôte", "a ready chain not said so");
			snap("-chain");

			// What it says while playing, and the names
			require(View::stateLine(State::Playing, {{3, 2}, {4, 1}}, md::ChainPlayer::Playing{0, 1})
				== "joue A04 (passage 2/2) · ensuite A05"
				&& View::stateLine(State::Playing, {{3, 2}, {4, 1}}, md::ChainPlayer::Playing{1, 0}) == "joue A05 · ensuite A04"
				&& View::patternName(0) == "A01" && View::patternName(127) == "H16", "the playing line or a pattern name wrong");

			// Emptied and off again, back to the lane
			element(doc, "mdChainClear").Click();
			element(doc, "mdChainEnable").Click();
			chain.update(false);
			present();
			require(chain.getEntries().empty() && !chain.isEnabled() && text("mdChainState") == "chaîne inactive",
				"VIDER and CHAÎNE ACTIVE did not empty and stop the chain");
			tabButton(doc, bottom, "0").Click();
			context.Update();
		}

		// BIBLIO: read when it first shows, one request at a time, every Kit then every pattern; a Kit shows
		// its machines without being loaded, a pattern its length and Kit; RELIRE reads them again.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			using Access = mdJucePlugin::ControllerAutomationTestAccess;
			const auto present = [&]
			{
				mdJucePlugin::EditorIdentityTestAccess::present(*editor);
				context.Update();
			};
			const auto kits = md.getKitLibrarySize();
			const auto patterns = mdJucePlugin::Controller::PatternLibrarySize;
			require(kits == (g_model == md::MachineModel::Monomachine ? 128u : 64u), "library size is not the machine's Kit count");
			require(!md.isReadingLibrary() && !md.isLibraryRead(), "library read before BIBLIO showed");
			tabButton(doc, "mdEdit", "3").Click();
			context.Update();
			present();
			require(md.isReadingLibrary() && md.getLibraryProgress() == 0 && text("mdLibKit0") == "01  —"
				&& element(doc, "mdLibKit0").IsClassSet("mdEdUnread") && text("mdLibInfo") == "lecture des kits : 0 / " + std::to_string(kits) + "…"
				&& text("mdLibRead") == "LECTURE…", "showing BIBLIO did not start reading: \"" + text("mdLibInfo") + "\"");
			// Kit 4 is not answered: the timer skips it
			for(uint8_t slot = 0; slot < kits; ++slot)
			{
				if(slot == 3)
				{
					Access::expireLibraryRequest(md);
					continue;
				}
				std::vector<uint16_t> machines(g_trackCount, slot % 2 ? g_pickMachine : g_otherFamilyMachine);
				md.parseSysexMessage(mdAutomationTest::makeKitDump(g_model, slot, 64, machines, "KIT " + std::to_string(slot + 1)),
					synthLib::MidiEventSource::Device);
			}
			present();
			require(md.isReadingLibrary() && md.getLibraryProgress() == kits
				&& text("mdLibInfo") == "lecture des patterns : 0 / " + std::to_string(patterns) + "…", "patterns not read after the Kits");
			// A01, 16 steps with trigs; A02, empty; B06, 32 steps; the others not answered
			const auto patternDump = [&](const uint8_t _slot, const uint8_t _length, const bool _trigs)
			{
#if defined(MD_EDITOR_SECTION_TEST_MM)
				auto edited = md::automation::sysex::MmPatternEditor::fromDump(mdAutomationTest::makeMmPatternDump(_slot, _length));
				require(edited.has_value(), "MM test pattern not built");
				for(uint8_t step = 0; _trigs && step < _length; step += 4)
					require(edited->setTrig(0, step, 48), "MM test pattern not built");
				const auto dump = edited->toDump();
				return pluginLib::SysEx(dump.begin(), dump.end());
#else
				std::array<uint32_t, 16> trigs{};
				trigs[0] = _trigs ? 0x1111u : 0u;
				return mdAutomationTest::makeMdPatternDump(_slot, _length, trigs);
#endif
			};
			for(uint8_t slot = 0; slot < patterns; ++slot)
			{
				if(slot == 0 || slot == 1 || slot == 21)
					md.parseSysexMessage(patternDump(slot, slot == 21 ? 32 : 16, slot != 1), synthLib::MidiEventSource::Device);
				else
					Access::expireLibraryRequest(md);
			}
			require(!md.isReadingLibrary() && md.isLibraryRead() && md.getLibraryProgress() == kits + patterns,
				"reading did not end after the last pattern");
			require(md.getLibraryKit(11) && md.getLibraryKit(11)->read && md.getLibraryKit(11)->name == "KIT 12"
				&& !md.getLibraryKit(3)->read, "library did not keep the Kits read and skip the one not answered");
			const auto first = md.getLibraryPattern(0);
			const auto empty = md.getLibraryPattern(1);
			require(first && first->read && first->length == 16 && first->kit == 0 && first->trigs == uint16_t{4}
				&& empty && empty->read && empty->trigs == uint16_t{0} && !md.getLibraryPattern(2)->read
				&& md.getLibraryPattern(21)->length == 32, "library did not keep the patterns read");
			present();
			require(text("mdLibInfo") == std::to_string(kits - 1) + " kits et 3 patterns lus (126 sans réponse)" && text("mdLibRead") == "RELIRE",
				"the library's line does not count what was read: \"" + text("mdLibInfo") + "\"");
			require(text("mdLibKit11") == "12  KIT 12" && text("mdLibKit3") == "04  —" && !element(doc, "mdLibKit11").IsClassSet("mdEdUnread"),
				"library cells do not show the Kits read");
			require(element(doc, "mdLibKit" + std::to_string(md.getCurrentKit())).IsClassSet("mdLibCurrent"), "the loaded Kit is not marked");
			element(doc, "mdLibKit11").Click();
			present();
			require(text("mdLibDetail").rfind("KIT 12 · KIT 12", 0) == 0 && text("mdLibMachine0") == std::string("01  ") + g_pickName
				&& element(doc, "mdLibKit11").IsClassSet("mdEdSelected"), "a Kit does not show its machines: \"" + text("mdLibDetail") + "\"");
			snap("-library");
			// PATTERNS: length and Kit, an empty one greyed, the machine's marked; a click shows one
			tabButton(doc, "mdLib", "1").Click();
			context.Update();
			present();
			require(visible(element(doc, "mdLibPatternsPage")) && !visible(element(doc, "mdLibKitsPage")), "PATTERNS not shown");
			require(text("mdLibPattern0") == "A01  16 pas · kit 01" && text("mdLibPattern2") == "A03  —" && text("mdLibPattern21") == "B06  32 pas · kit 01"
				&& element(doc, "mdLibPattern1").IsClassSet("mdLibEmpty") && element(doc, "mdLibPattern2").IsClassSet("mdEdUnread")
				&& element(doc, "mdLibPattern" + std::to_string(md.getCurrentPattern())).IsClassSet("mdLibCurrent"),
				"pattern cells do not show the patterns read");
			element(doc, "mdLibPattern0").Click();
			present();
			require(text("mdLibPatternDetail") == "A01 · 16 pas · kit 01 · 4 trigs" && element(doc, "mdLibPattern0").IsClassSet("mdEdSelected"),
				"a pattern does not show its detail: \"" + text("mdLibPatternDetail") + "\"");
			snap("-library-patterns");
			// COPIER takes A01; COLLER onto B06, which has trigs, asks to confirm (REMPLACER), then copies: A01 read,
			// sent as B06 and read back, B06's cell showing the copy. Onto A02, known empty, at once; not answered,
			// the copy fails.
			{
				using PatternCopy = mdJucePlugin::Controller::PatternCopy;
				const auto line = [&] { return text("mdLibPatternDetail"); };
				const auto serial = md.getPatternCopy().serial;
				// B06's trigs: one every 4 steps of its 32 on the Monomachine, the first 16's on the Machinedrum
				const std::string b06 = std::string("B06 · 32 pas · kit 01 · ") + (g_model == md::MachineModel::Monomachine ? "8" : "4")
					+ " trigs · à coller : A01";
				require(element(doc, "mdLibPaste").IsClassSet("mdEdOff") && !element(doc, "mdLibCopy").IsClassSet("mdEdOff"),
					"COLLER offered before COPIER, or COPIER not offered");
				element(doc, "mdLibCopy").Click();
				element(doc, "mdLibPattern21").Click();
				present();
				require(!element(doc, "mdLibPaste").IsClassSet("mdEdOff") && line() == b06
					&& element(doc, "mdLibPattern0").IsClassSet("mdLibSource"), "COPIER did not take A01: \"" + line() + "\"");
				element(doc, "mdLibPaste").Click();
				present();
				require(text("mdLibPaste") == "REMPLACER B06 ?" && md.getPatternCopy().serial == serial,
					"COLLER onto a pattern with trigs did not ask to confirm");
				element(doc, "mdLibPaste").Click();
				present();
				auto copy = md.getPatternCopy();
				require(copy.state == PatternCopy::Reading && copy.from == 0 && copy.to == 21 && text("mdLibPaste") == "COPIE…"
					&& line() == b06 + " · copie de A01 vers B06…", "REMPLACER did not start the copy: \"" + line() + "\"");
				md.parseSysexMessage(patternDump(0, 16, true), synthLib::MidiEventSource::Device);
				require(md.getPatternCopy().state == PatternCopy::Writing, "A01 read, the copy not sent");
				md.parseSysexMessage(patternDump(21, 16, true), synthLib::MidiEventSource::Device);
				present();
				require(md.getPatternCopy().state == PatternCopy::Copied && text("mdLibPattern21") == "B06  16 pas · kit 01"
					&& line() == "B06 · 16 pas · kit 01 · 4 trigs · à coller : A01 · A01 copié sur B06" && text("mdLibPaste") == "COLLER",
					"the copy read back not shown: \"" + line() + "\"");
				snap("-library-copy");
				// Onto A02, empty: at once; no answer, the copy fails
				element(doc, "mdLibPattern1").Click();
				element(doc, "mdLibPaste").Click();
				present();
				copy = md.getPatternCopy();
				require(copy.state == PatternCopy::Reading && copy.to == 1, "COLLER onto an empty pattern asked to confirm");
				Access::expirePatternCopy(md);
				present();
				require(md.getPatternCopy().state == PatternCopy::Failed && line().find("copie de A01 vers A02 sans réponse") != std::string::npos,
					"a copy not answered did not fail: \"" + line() + "\"");
			}
			// RELIRE reads everything again
			element(doc, "mdLibRead").Click();
			present();
			require(md.isReadingLibrary() && md.getLibraryProgress() == 0 && text("mdLibPattern0") == "A01  —", "RELIRE did not read again");
			for(size_t item = 0; item < kits + patterns; ++item)
				Access::expireLibraryRequest(md);
			require(!md.isReadingLibrary() && md.isLibraryRead(), "the second reading did not end");
			tabButton(doc, "mdLib", "0").Click();
			tabButton(doc, "mdEdit", "0").Click();
			context.Update();
		}

		if(png)
		{
			// the editor is shown (see above); the front panel, then the stacked tall window
			snap("-son");
#if !defined(MD_EDITOR_SECTION_TEST_MM)
			element(doc, "mdEdStep0").Click();
			snap("-steps");
			element(doc, "mdEdStep0").Click();
#endif
			element(doc, "mdEdMachineChange").Click();
			snap("-picker");
			element(doc, "mdEdMachineChange").Click();
#if !defined(MD_EDITOR_SECTION_TEST_MM)
			element(doc, "editMaster").Click();
			snap("-master");
			element(doc, "editTrack0").Click();
#endif
			element(doc, "mdViewPanel").Click();
			snap("-panel");
			component->setSize(1100, 1200);
			snap("-stacked");
			element(doc, "mdViewEditor").Click();
			component->setSize(1100, 740);
			snap("-curves");
			element(doc, "mdViewPanel").Click();
			component->setSize(1100, 606);
			element(doc, "mdViewEditor").Click();
			component->setLookAndFeel(nullptr);
		}

		std::cout << g_name << ": PASS" << std::endl;
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << g_name << ": FAIL: " << e.what() << std::endl;
		return 1;
	}
}
