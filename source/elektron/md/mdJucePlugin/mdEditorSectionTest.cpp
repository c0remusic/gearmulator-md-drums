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
#include "mdLibraryView.h"
#include "mdMachinePicker.h"
#include "mdMasterEffectsView.h"
#include "mdOutputMetersView.h"
#include "mdPatternView.h"
#include "mdStepGrid.h"
#include "mdSystemPage.h"
#include "mdTrackRoutingView.h"
#include "mdUnreadValues.h"
#include "mdLib/mdmachines.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
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

		// The library Kit asked for is not answered within 2 s: the controller timer skips it
		static void expireLibraryRequest(Controller& _controller)
		{
			_controller.m_libraryRequestMs = Controller::milliseconds() - 2001;
			_controller.onControllerTimer();
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

		static const OutputMetersView& meters(const Editor& _editor) { return *_editor.m_outputMetersView; }
		static const PatternView& pattern(const Editor& _editor) { return *_editor.m_patternView; }

		// Every editor component the presentation timer refreshes, for the snapshots
		static void present(Editor& _editor)
		{
			updateMeters(_editor, juce::Time::getMillisecondCounterHiRes());
			updateSystem(_editor, juce::Time::getMillisecondCounterHiRes());
			if(_editor.m_patternView)
				_editor.m_patternView->update();
			if(_editor.m_libraryView)
				_editor.m_libraryView->update();
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

			element(doc, "editTrack1").Click();
			context.Update();
			require(element(doc, "mdEdMachineName").GetInnerRML() == "—", "MACHINE block kept the previous track's machine");
			element(doc, "editTrack0").Click();
			context.Update();
			require(element(doc, "mdEdMachineName").GetInnerRML() == g_pickName, "MACHINE block lost the machine of track 1");
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
			require(md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::Pending, "trig not written to the firmware");
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
		// JOUER: the current pattern on every track, a step with locks framed, and the lane
		// of the edited track: the Kit value in grey, a lock in orange, on the steps that play.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			std::array<uint32_t, 16> trigs{};
			trigs[0] = 0x11;            // track 1: steps 1 and 5, a lock of 77 on parameter 1 at step 1
			trigs[2] = 0xf0f0f0f0u;
			trigs[8] = 0xffffffffu;
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
			require(text("mdPlayInfo").rfind("pattern B05 · 32 pas · 50 trigs", 0) == 0, "JOUER does not name the pattern: \"" + text("mdPlayInfo") + "\"");
			require(cell(0, 0).IsClassSet("mdEdStepTrig") && cell(0, 0).IsClassSet("mdPlayLocked") && cell(0, 4).IsClassSet("mdEdStepTrig")
				&& !cell(0, 4).IsClassSet("mdPlayLocked") && !cell(0, 1).IsClassSet("mdEdStepTrig") && cell(2, 4).IsClassSet("mdEdStepTrig")
				&& !cell(2, 0).IsClassSet("mdEdStepTrig") && cell(8, 31).IsClassSet("mdEdStepTrig"), "JOUER grid does not show the pattern");
			const auto& lane = mdJucePlugin::EditorIdentityTestAccess::pattern(*editor);
			require(lane.barValue(0) == 77 && lane.barLocked(0) && lane.barValue(4) == 64 && !lane.barLocked(4) && lane.barValue(1) == -1,
				"lane does not show the lock and the Kit value");
			require(text("mdPlayLaneInfo") == "piste 01 · P1 : 1 lock · kit 64" && text("mdPlayParam0") == "P1 · 1",
				"lane does not count the locks: \"" + text("mdPlayLaneInfo") + "\"");
			element(doc, "mdPlayParam17").Click();
			context.Update();
			require(text("mdPlayLaneInfo") == "piste 01 · VOL : 0 locks · kit 64" && !lane.barLocked(0) && lane.barValue(0) == 64,
				"choosing VOL did not change the lane");
			// A track name makes it the edited track; its lane follows
			element(doc, "mdPlayTrack2").Click();
			context.Update();
			require(controller.getCurrentPart() == 2 && element(doc, "mdPlayTrack2").IsClassSet("mdEdSelected")
				&& lane.barValue(0) == -1 && lane.barValue(4) == 64, "a track name did not take the lane");
			// A double click writes a trig, as in PAS
			cell(1, 3).DispatchEvent(Rml::EventId::Dblclick, Rml::Dictionary());
			context.Update();
			require(md.getPattern()->hasTrig(1, 3) && md.getPatternWrite() == mdJucePlugin::Controller::PatternWrite::Pending
				&& cell(1, 3).IsClassSet("mdEdStepTrig"), "a double click did not write the trig");
			element(doc, "mdPlayParam0").Click();
			element(doc, "mdPlayTrack0").Click();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			snap("-play");
			// OUVRIR DANS SON shows the edited track's sound
			element(doc, "mdPlayOpen").Click();
			context.Update();
			require(visible(element(doc, "mdEdPageSound")) && controller.getCurrentPart() == 0, "OUVRIR DANS SON did not open SON");
		}
#endif

		// BIBLIO: LIRE LES KITS reads every stored Kit, one request at a time; a Kit
		// shows its machines without being loaded.
		{
			auto& md = dynamic_cast<mdJucePlugin::Controller&>(controller);
			tabButton(doc, "mdEdit", "3").Click();
			context.Update();
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			const auto kits = md.getKitLibrarySize();
			require(kits == (g_model == md::MachineModel::Monomachine ? 128u : 64u), "library size is not the machine's Kit count");
			require(text("mdLibKit0") == "01  —" && element(doc, "mdLibKit0").IsClassSet("mdEdUnread"), "library shows a Kit before reading");
			element(doc, "mdLibRead").Click();
			require(md.isReadingKitLibrary() && md.getKitLibraryProgress() == 0, "LIRE LES KITS did not start reading");
			// Slot 3 is not answered: the timer skips it after 2 s
			for(uint8_t slot = 0; slot < kits; ++slot)
			{
				if(slot == 3)
				{
					mdJucePlugin::ControllerAutomationTestAccess::expireLibraryRequest(md);
					continue;
				}
				std::vector<uint16_t> machines(g_trackCount, slot % 2 ? g_pickMachine : g_otherFamilyMachine);
				md.parseSysexMessage(mdAutomationTest::makeKitDump(g_model, slot, 64, machines, "KIT " + std::to_string(slot + 1)),
					synthLib::MidiEventSource::Device);
			}
			require(!md.isReadingKitLibrary() && md.getKitLibraryProgress() == kits, "reading did not end after the last Kit");
			require(md.getLibraryKit(11) && md.getLibraryKit(11)->read && md.getLibraryKit(11)->name == "KIT 12"
				&& !md.getLibraryKit(3)->read, "library did not keep the Kits read and skip the one not answered");
			mdJucePlugin::EditorIdentityTestAccess::present(*editor);
			context.Update();
			require(text("mdLibKit11") == "12  KIT 12" && text("mdLibKit3") == "04  —" && !element(doc, "mdLibKit11").IsClassSet("mdEdUnread"),
				"library cells do not show the Kits read");
			require(element(doc, "mdLibKit" + std::to_string(md.getCurrentKit())).IsClassSet("mdLibCurrent"), "the loaded Kit is not marked");
			element(doc, "mdLibKit11").Click();
			context.Update();
			require(text("mdLibDetail").rfind("KIT 12 · KIT 12", 0) == 0 && text("mdLibMachine0") == std::string("01  ") + g_pickName
				&& element(doc, "mdLibKit11").IsClassSet("mdEdSelected"), "a Kit does not show its machines: \"" + text("mdLibDetail") + "\"");
			snap("-library");
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
