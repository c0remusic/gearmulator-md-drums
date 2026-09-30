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

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

	struct Block
	{
		const char* id;
		float x, y, w, h;
	};

	// Tops of the SON blocks are relative to the scrolling page.
#if defined(MD_EDITOR_SECTION_TEST_MM)
	constexpr auto g_model = md::MachineModel::Monomachine;
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
	constexpr const char* g_name = "mdEditorSectionTest";
	constexpr int g_trackCount = 16;
	constexpr float g_trackTabPitch = 63, g_trackTabWidth = 60;
	// SON: 21 knobs + 3 faders; MIX: 4 values, a level fader and a mute LED per track
	constexpr size_t g_boundElements = 21 + 3 + 16 * 6;
	constexpr Block g_blocks[] = {
		{"mdEdSteps", 16, 16, 1068, 88},
		{"mdEdMachine", 16, 116, 1068, 88},
		{"mdEdSource", 16, 216, 438, 384},
		{"mdEdFilter", 466, 216, 348, 356},
		{"mdEdColour", 466, 584, 348, 132},
		{"mdEdMix", 826, 216, 258, 276},
		{"mdEdLfo", 16, 728, 1068, 184},
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

		// categories: MIX replaces SON
		tabButton(doc, "mdEdit", "1").Click();
		context.Update();
		require(visible(element(doc, "mdEdPageMix")) && !visible(element(doc, "mdEdPageSound")), "MIX tab did not switch pages");
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
