// Loads the real Machinedrum skin (or the Monomachine skin with MD_EDITOR_SECTION_TEST_MM) headless
// and checks the editor below the front panel: window size, dp geometry of the grid, parameter
// binding, tabs and the front-panel fold.

#include "mdEditor.h"
#include "mdPluginEditorState.h"
#include "mdPluginProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "jucePluginLib/controller.h"
#include "mdAutomationTestSupport.h"
#include "mdController.h"
#include "mdLib/mdmachines.h"

#include "RmlUi/Core/Context.h"
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

	// Tops of the SON blocks are relative to the scrolling page.
#if defined(MD_EDITOR_SECTION_TEST_MM)
	constexpr auto g_model = md::MachineModel::Monomachine;
	// Picker: family 1 is SID, whose only machine is SID-6581 (3); family 0 (GND) holds GND-SIN (1).
	constexpr uint16_t g_pickMachine = 3;
	constexpr const char* g_pickName = "SID-6581";
	constexpr const char* g_pickFamily = "SID";
	constexpr uint16_t g_otherFamilyMachine = 1;
	constexpr float g_pickerTop = 16 + 88 + 4;
	constexpr const char* g_name = "mmEditorSectionTest";
	constexpr int g_trackCount = 6;
	constexpr float g_trackTabPitch = 180, g_trackTabWidth = 168;
	// SON: 54 knobs + 2 faders; MIX: 3 values, a level fader and a mute LED per track
	constexpr size_t g_boundElements = 54 + 2 + 6 * 5;
	constexpr Block g_blocks[] = {
		{"mdEdMachine", 16, 16, 1068, 88},
		{"mdEdOsc", 16, 116, 348, 356},
		{"mdEdAmp", 376, 116, 348, 356},
		{"mdEdFilter", 736, 116, 348, 356},
		{"mdEdEffects", 16, 484, 348, 356},
		{"mdEdLfo", 376, 484, 708, 356},
	};
#else
	constexpr auto g_model = md::MachineModel::Machinedrum;
	// Picker: family 1 is TRX; TRX-BD is 16. Family 0 (GND) holds GND-SN (1).
	constexpr uint16_t g_pickMachine = 16;
	constexpr const char* g_pickName = "TRX-BD";
	constexpr const char* g_pickFamily = "TRX";
	constexpr uint16_t g_otherFamilyMachine = 1;
	constexpr float g_pickerTop = 116 + 88 + 4;
	constexpr const char* g_name = "mdEditorSectionTest";
	constexpr int g_trackCount = 16;
	constexpr float g_trackTabPitch = 63, g_trackTabWidth = 60;
	// SON: 21 knobs + 3 faders; MIX: 4 values, a level fader and a mute LED per track
	constexpr size_t g_boundElements = 21 + 3 + 16 * 6;
	constexpr Block g_blocks[] = {
		{"mdEdSteps", 16, 16, 1068, 88},
		{"mdEdMachine", 16, 116, 1068, 88},
		// two rows whose blocks share one height
		{"mdEdSource", 16, 216, 438, 384},
		{"mdEdFilter", 466, 216, 348, 384},
		{"mdEdMix", 826, 216, 258, 384},
		{"mdEdColour", 16, 612, 348, 272},
		{"mdEdLfo", 376, 612, 708, 272},
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

		// window: 1100 x 1000 dp at scale 1
		const auto docSize = component->getDocumentSize();
		require(docSize.x == 1100 && docSize.y == 1000,
			"document is " + std::to_string(docSize.x) + "x" + std::to_string(docSize.y) + ", expected 1100x1000");

		// front panel open: panel 570, header 56, track strip 48, then the scrolling page
		requireRect(element(doc, "mdFrontPanel"), 0, 0, 1100, 570, "front panel");
		require(!visible(element(doc, "mdFrontPanelFolded")), "folded bar visible while the panel is open");
		constexpr int lastTrack = g_trackCount - 1;
		requireRect(element(doc, "editTrack0"), 16, 570 + 56 + 8, g_trackTabWidth, 40, "first track tab");
		requireRect(element(doc, "editTrack" + std::to_string(lastTrack)), 16 + lastTrack * g_trackTabPitch, 570 + 56 + 8,
			g_trackTabWidth, 40, "last track tab");
		require(doc.GetElementById("editTrack" + std::to_string(g_trackCount)) == nullptr, "more track tabs than tracks");
		constexpr float pageTop = 570 + 56 + 48;
		for(const auto& b : g_blocks)
			requireRect(element(doc, b.id), b.x, pageTop + b.y, b.w, b.h, b.id);
		requireTiled(element(doc, "mdEdTrackView"), "SON");
		const auto& first = g_blocks[0];

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
		requireRect(element(doc, "mdEdEcho"), 16, pageTop + 16, 528, 272, "RHYTHM ECHO (6 columns)");
		requireRect(element(doc, "mdEdDynamix"), 556, pageTop + 300, 528, 272, "DYNAMIX (6 columns)");
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
		}
#endif

		// categories: MIX replaces SON
		tabButton(doc, "mdEdit", "1").Click();
		context.Update();
		require(visible(element(doc, "mdEdPageMix")) && !visible(element(doc, "mdEdPageSound")), "MIX tab did not switch pages");
		requireTiled(element(doc, "mdEdPageMix"), "MIX");
		tabButton(doc, "mdEdit", "0").Click();
		context.Update();
		require(visible(element(doc, "mdEdPageSound")) && !visible(element(doc, "mdEdPageMix")), "SON tab did not switch back");

		// fold the front panel: the editor moves up to y = 32 and grows
		element(doc, "mdPanelFold").Click();
		context.Update();
		require(!visible(element(doc, "mdFrontPanel")) && visible(element(doc, "mdFrontPanelFolded")), "fold did not hide the front panel");
		requireRect(element(doc, first.id), first.x, 32 + 56 + 48 + first.y, first.w, first.h, "first block with the panel folded");
		element(doc, "mdPanelFold").Click();
		context.Update();
		require(visible(element(doc, "mdFrontPanel")) && !visible(element(doc, "mdFrontPanelFolded")), "unfold did not restore the front panel");
		requireRect(element(doc, first.id), first.x, pageTop + first.y, first.w, first.h, "first block after unfolding");

		// Optional: MD_EDITOR_TEST_PNG=<prefix> writes snapshots of the SON and MIX pages for review.
		if(const char* png = std::getenv("MD_EDITOR_TEST_PNG"))
		{
			juceRmlUi::LookAndFeel lookAndFeel;
			component->setLookAndFeel(&lookAndFeel);
			const auto snap = [&](const std::string& _suffix)
			{
				juce::Image image;
				for(int frame = 0; frame < 6; ++frame)
				{
					juceRmlUi::RenderingTestAccess::update(*component);
					image = juce::Image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
					lookAndFeel.getCurrentImage() = image;
					juce::Graphics g(image);
					component->paint(g);
				}
				juce::FileOutputStream out(juce::File(std::string(png) + _suffix + ".png"));
				out.setPosition(0);
				out.truncate();
				juce::PNGImageFormat().writeImageToStream(image, out);
			};
			snap("-son");
			tabButton(doc, "mdEdit", "1").Click();
			snap("-mix");
			tabButton(doc, "mdEdit", "0").Click();
			element(doc, "mdPanelFold").Click();
			snap("-son-folded");
#if !defined(MD_EDITOR_SECTION_TEST_MM)
			element(doc, "mdEdStep0").Click();
			snap("-steps-folded");
			element(doc, "mdEdStep0").Click();
#endif
			element(doc, "mdEdMachineChange").Click();
			snap("-picker-folded");
			element(doc, "mdEdMachineChange").Click();
			tabButton(doc, "mdEdit", "1").Click();
			snap("-mix-folded");
			tabButton(doc, "mdEdit", "0").Click();
#if !defined(MD_EDITOR_SECTION_TEST_MM)
			element(doc, "editMaster").Click();
			snap("-master-folded");
			element(doc, "editTrack0").Click();
#endif
			element(doc, "mdPanelFold").Click();
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
