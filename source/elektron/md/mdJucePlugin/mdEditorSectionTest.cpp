// Loads the real Machinedrum skin headless and checks the editor below the front panel:
// window size, dp geometry of the grid, parameter binding, tabs and the front-panel fold.

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
}

int main()
{
	try
	{
		juce::ScopedJuceInitialiser_GUI gui;

		mdJucePlugin::AudioPluginAudioProcessor processor(md::MachineModel::Machinedrum,
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
		requireRect(element(doc, "editTrack0"), 16, 570 + 56 + 8, 60, 40, "track tab 1");
		requireRect(element(doc, "editTrack15"), 16 + 15 * 63, 570 + 56 + 8, 60, 40, "track tab 16");
		constexpr float pageTop = 570 + 56 + 48;
		requireRect(element(doc, "mdEdSource"), 16, pageTop + 16, 438, 216, "SOURCE (5 columns)");
		requireRect(element(doc, "mdEdFilter"), 466, pageTop + 16, 348, 216, "FILTRE (4 columns)");
		requireRect(element(doc, "mdEdMix"), 826, pageTop + 16, 258, 216, "MIX (3 columns)");
		requireRect(element(doc, "mdEdColour"), 16, pageTop + 244, 348, 132, "COULEUR (4 columns)");
		requireRect(element(doc, "mdEdLfo"), 376, pageTop + 244, 708, 132, "MODULATION (8 columns)");

		// every editor knob found its parameter (the binding writes min/max on the element)
		Rml::ElementList knobs;
		element(doc, "mdEditor").GetElementsByTagName(knobs, "knob");
		require(knobs.size() == 24 + 16 * 4, "expected 88 editor knobs (24 in SON, 64 in MIX), found " + std::to_string(knobs.size()));
		for(auto* k : knobs)
		{
			const auto* p = k->GetAttribute("param");
			require(p != nullptr, "editor knob without param");
			require(k->GetAttribute("max") != nullptr, "knob not bound to parameter " + p->Get<Rml::String>(k->GetCoreInstance()));
		}

		// track tabs choose the part edited by partCurrent
		auto& controller = processor.getController();
		element(doc, "editTrack3").Click();
		context.Update();
		require(controller.getCurrentPart() == 3, "track tab 4 did not select part 3");
		element(doc, "editTrack0").Click();
		context.Update();
		require(controller.getCurrentPart() == 0, "track tab 1 did not select part 0");

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
		requireRect(element(doc, "mdEdSource"), 16, 32 + 56 + 48 + 16, 438, 216, "SOURCE with the panel folded");
		element(doc, "mdPanelFold").Click();
		context.Update();
		require(visible(element(doc, "mdFrontPanel")) && !visible(element(doc, "mdFrontPanelFolded")), "unfold did not restore the front panel");
		requireRect(element(doc, "mdEdSource"), 16, pageTop + 16, 438, 216, "SOURCE after unfolding");

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
			element(doc, "mdPanelFold").Click();
			component->setLookAndFeel(nullptr);
		}

		std::cout << "mdEditorSectionTest: PASS" << std::endl;
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << "mdEditorSectionTest: FAIL: " << e.what() << std::endl;
		return 1;
	}
}
