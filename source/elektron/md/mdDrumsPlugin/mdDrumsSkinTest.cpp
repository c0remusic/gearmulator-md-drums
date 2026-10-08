// MD Drums' v22 skin against the approved mockup (Design/HANDOFF.md, ticket 17 of the editor map), with the software
// renderer at 100 %:
// - every box exported from the mockup (Design/v22-boxes.txt, by v22-export-boxes.js) is the box of the skin's
//   element of the same ID, to half a pixel;
// - every text baseline among them sits on a multiple of 4 (a 0 x 0 inline-block probe on the text's line), and no
//   text overflows its box;
// - every Track parameter the Track tab shows is bound to a control, Track by Track;
// - input to pixel: a knob dragged and a list row clicked show on the next frame, and the event, RmlUi's update and
//   the software render take 33 ms at most. MD_DRUMS_SKIN_PNG names a file for a picture of the tab.

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsKnob.h"
#include "mdDrumsProcessor.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "synthLib/plugin.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ElementText.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace juceRmlUi
{
	// The friend hook the editor tests use: one RmlUi frame, so that paint() has geometry
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

	struct Box
	{
		float x = 0, y = 0, w = 0, h = 0;
	};

	std::vector<std::pair<std::string, Box>> readBoxes()
	{
		const std::string path = std::string(MD_DRUMS_DESIGN_DIR) + "/v22-boxes.txt";
		std::ifstream file(path);
		require(file.good(), "cannot read " + path);
		std::vector<std::pair<std::string, Box>> boxes;
		std::string line;
		while(std::getline(file, line))
		{
			std::istringstream in(line);
			std::string key;
			Box b;
			if(in >> key >> b.x >> b.y >> b.w >> b.h)
				boxes.emplace_back(key, b);
		}
		return boxes;
	}

	// The mockup's elements that carry text, by their keys
	bool hasText(const std::string& _key)
	{
		if(_key.rfind("k_", 0) == 0 || _key.rfind("rule_", 0) == 0 || _key.find("_scope") != std::string::npos)
			return false;
		if(_key.rfind("screen_", 0) == 0)
			return _key.find("_title") != std::string::npos;
		static const std::set<std::string> boxesOnly{"surface", "row_sel_bg", "row_sel_mark", "main_meter",
			"velocity_bar", "play", "syn_group1"};
		return boxesOnly.count(_key) == 0;
	}

	struct Fixture
	{
		mdDrums::Processor& processor;
		mdDrums::Editor* editor = nullptr;
		juceRmlUi::RmlComponent* component = nullptr;
		juceRmlUi::LookAndFeel lookAndFeel;
		juce::Image image;

		explicit Fixture(mdDrums::Processor& _processor) : processor(_processor)
		{
			processor.setForceSoftwareRendererForSession(true);
			auto& state = processor.getOrCreateEditorState();
			editor = dynamic_cast<mdDrums::Editor*>(state.getEditor());
			require(editor != nullptr, "the processor did not create the editor");
			component = editor->getRmlComponent();
			require(component && component->getContext() && component->getDocument(), "the editor has no RmlUi document");
			require(editor->findChild("page_track", false) != nullptr, "the default skin is not the v22 one");
			component->setLookAndFeel(&lookAndFeel);
		}

		~Fixture()
		{
			component->setLookAndFeel(nullptr);
			processor.destroyEditorState();
		}

		Rml::Context& context() const { return *component->getContext(); }

		Rml::Element* find(const std::string& _id) const { return editor->findChild(_id, false); }

		// One frame: RmlUi's update, then the software render into an image
		void frame()
		{
			juceRmlUi::RenderingTestAccess::update(*component);
			image = juce::Image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
			lookAndFeel.getCurrentImage() = image;
			juce::Graphics g(image);
			component->paint(g);
		}

		float dp() const { return context().GetDensityIndependentPixelRatio(); }

		Box box(Rml::Element& _element) const
		{
			const auto offset = _element.GetAbsoluteOffset(Rml::BoxArea::Border);
			const auto size = _element.GetBox().GetSize(Rml::BoxArea::Border);
			const auto r = dp();
			return {offset.x / r, offset.y / r, size.x / r, size.y / r};
		}

		juce::Colour pixel(const float _x, const float _y) const
		{
			const auto r = dp();
			return image.getPixelAt(juce::roundToInt(_x * r), juce::roundToInt(_y * r));
		}
	};

	std::string describe(const Box& _b)
	{
		char text[96];
		(void)std::snprintf(text, sizeof(text), "%g %g %g %g", _b.x, _b.y, _b.w, _b.h);
		return text;
	}

	void boxes(Fixture& _f)
	{
		const auto expected = readBoxes();
		require(expected.size() > 200, "the mockup's boxes are missing");
		size_t matched = 0;
		std::vector<std::string> wrong;
		for(const auto& [key, want] : expected)
		{
			auto* element = _f.find(key);
			if(!element)
			{
				wrong.push_back(key + ": no element");
				continue;
			}
			const auto got = _f.box(*element);
			if(std::abs(got.x - want.x) > 0.5f || std::abs(got.y - want.y) > 0.5f || std::abs(got.w - want.w) > 0.5f
				|| std::abs(got.h - want.h) > 0.5f)
				wrong.push_back(key + ": " + describe(got) + ", the mockup " + describe(want));
			else
				++matched;
		}

		// As wide as their text in the mockup: the LFO's target and Assign on the title row, Assign ending 16 px inside
		for(const auto* key : {"lfo_target", "lfo_assign"})
		{
			auto* element = _f.find(key);
			require(element != nullptr, std::string("no element ") + key);
			const auto b = _f.box(*element);
			if(std::abs(b.y - 624) > 0.5f || std::abs(b.h - 24) > 0.5f)
				wrong.push_back(std::string(key) + ": " + describe(b) + ", not on the LFO title's row");
		}
		const auto assign = _f.box(*_f.find("lfo_assign"));
		if(std::abs(assign.x + assign.w - 1280) > 0.5f)
			wrong.push_back("lfo_assign ends at " + std::to_string(assign.x + assign.w) + ", not 1280");

		std::printf("boxes: %zu of %zu as the mockup's\n", matched, expected.size());
		for(const auto& w : wrong)
			std::printf("  %s\n", w.c_str());
		require(wrong.empty(), "boxes differ from the mockup's");
	}

	void baselines(Fixture& _f)
	{
		auto& document = *_f.component->getDocument();
		std::vector<std::pair<std::string, Rml::Element*>> probes;
		for(const auto& [key, want] : readBoxes())
		{
			if(!hasText(key))
				continue;
			auto* element = _f.find(key);
			if(!element)
				continue;
			// In a flex row (a group's name and its rule) the text is the first item's
			if(element->GetComputedValues().display() == Rml::Style::Display::Flex && element->GetNumChildren())
				element = element->GetChild(0);
			auto probe = document.CreateElement("span");
			probe->SetProperty("display", "inline-block");
			probe->SetProperty("width", "0dp");
			probe->SetProperty("height", "0dp");
			probes.emplace_back(key, element->AppendChild(std::move(probe)));
		}
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*_f.component);
			_f.context().Update();
		}

		std::vector<std::string> off, overflow;
		for(const auto& [key, probe] : probes)
		{
			const auto y = probe->GetAbsoluteOffset(Rml::BoxArea::Border).y / _f.dp();
			const auto step = std::round(y / 4.0f) * 4.0f;
			if(std::abs(y - step) > 0.5f)
			{
				char text[64];
				(void)std::snprintf(text, sizeof(text), "%s: %.2f", key.c_str(), y);
				off.push_back(text);
			}
			auto* element = probe->GetParentNode();
			if(element->GetScrollWidth() > element->GetClientWidth() + 0.5f)
				overflow.push_back(key);
			element->RemoveChild(probe);
		}
		std::printf("baselines: %zu texts, %zu off the 4 px steps, %zu overflowing\n", probes.size(), off.size(),
			overflow.size());
		for(const auto& o : off)
			std::printf("  baseline %s\n", o.c_str());
		for(const auto& o : overflow)
			std::printf("  overflows %s\n", o.c_str());
		require(off.empty(), "baselines off the 4 px steps");
		require(overflow.empty(), "texts overflow their boxes");
	}

	// The Track tab's controls, Track by Track: Machine, the 24 parameters, LFO shapes and mode, Mute, Solo
	void bound(Fixture& _f)
	{
		static const std::vector<std::string> names{"Machine", "SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7",
			"SYN8", "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV",
			"LFOS", "LFOD", "LFOM", "LfoShape1", "LfoShape2", "LfoMode", "Mute", "Solo"};
		size_t bound = 0, wanted = 0;
		for(uint8_t t = 0; t < mdDrums::Controller::TrackCount; ++t)
		{
			_f.editor->setCurrentPart(t);
			for(const auto& name : names)
			{
				if(name == "Machine")
					continue;	// the head band shows it; the browser (a later slice) sets it
				++wanted;
				if(!_f.editor->findChildreByParam(name, t).empty())
					++bound;
				else
					std::printf("  track %d: %s has no control\n", t + 1, name.c_str());
			}
		}
		_f.editor->setCurrentPart(0);
		std::printf("bound: %zu of %zu Track parameters have a control on the Track tab\n", bound, wanted);
		require(bound == wanted, "a Track parameter has no control");
	}

	using Clock = std::chrono::steady_clock;

	double since(const Clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - _start).count();
	}

	void inputToPixel(Fixture& _f)
	{
		auto& context = _f.context();
		const auto r = _f.dp();
		auto* knob = dynamic_cast<mdDrums::Knob*>(_f.find("k_FLTF"));
		require(knob != nullptr, "k_FLTF is not a vknob");
		const auto kb = _f.box(*knob);
		const auto cx = kb.x + kb.w * 0.5f, cy = kb.y + kb.h * 0.5f;
		auto& controller = _f.processor.getController();
		auto* fltf = controller.getParameter("FLTF", 0);
		const auto before = fltf->getUnnormalizedValue();

		// A drag up, 3 px a frame: 2 steps each, its arc on screen the frame it happens
		_f.frame();
		// The arc's start, at the track's lower left (radius 29 at 225 degrees), brightest of a 3 x 3 patch
		const auto arcPixel = [&]
		{
			float brightest = 0;
			for(int dy = -1; dy <= 1; ++dy)
				for(int dx = -1; dx <= 1; ++dx)
					brightest = std::max(brightest, _f.pixel(kb.x + 32 - 20.5f + dx, kb.y + 32 + 20.5f + dy).getBrightness());
			return brightest;
		};
		context.ProcessMouseMove(juce::roundToInt(cx * r), juce::roundToInt(cy * r), 0);
		context.ProcessMouseButtonDown(0, 0);
		_f.frame();
		double worst = 0, sum = 0;
		constexpr int moves = 30;
		for(int i = 1; i <= moves; ++i)
		{
			const auto start = Clock::now();
			context.ProcessMouseMove(juce::roundToInt(cx * r), juce::roundToInt((cy - 3.0f * i) * r), 0);
			_f.frame();
			const auto ms = since(start);
			worst = std::max(worst, ms);
			sum += ms;
		}
		context.ProcessMouseButtonUp(0, 0);
		const auto after = knob->getValue();
		const auto edited = arcPixel();
		std::printf("drag: FLTF %d to %.0f over %d frames, event to pixel %.2f ms mean, %.2f ms at worst\n", before,
			after, moves, sum / moves, worst);
		require(after >= static_cast<float>(before) + 2.0f * moves - 2.0f, "the drag did not move the knob 2 steps a frame");
		require(edited > 0.8f, "the knob's arc is not drawn after the drag");
		require(worst <= 33.0, "a dragged knob took over 33 ms to reach the pixels");

		// A click on track 03's row: the bands show it on the next frame
		auto* row = _f.find("row3_num");
		const auto rb = _f.box(*row);
		context.ProcessMouseMove(juce::roundToInt((rb.x + 8) * r), juce::roundToInt((rb.y + 12) * r), 0);
		_f.frame();
		const auto start = Clock::now();
		context.ProcessMouseButtonDown(0, 0);
		context.ProcessMouseButtonUp(0, 0);
		_f.frame();
		const auto ms = since(start);
		const auto mark = _f.box(*_f.find("row_sel_mark"));
		std::printf("click: track 03 shown, event to pixel %.2f ms; part %d, its row's mark at y %g\n", ms,
			controller.getCurrentPart(), mark.y);
		require(controller.getCurrentPart() == 2, "the click did not show track 03");
		require(std::abs(mark.y - 264) < 0.5f, "the shown row's mark did not move");
		require(_f.pixel(1, 270).getBrightness() > 0.5f, "the shown row's mark is not drawn on the next frame");
		require(ms <= 33.0, "a click on a row took over 33 ms to reach the pixels");
		_f.editor->setCurrentPart(0);
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		mdDrums::Processor processor(true);
		if(!dynamic_cast<const mdDrums::Device*>(processor.getPlugin().getDevice()))
		{
			std::cout << "mdDrumsSkinTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
			return 77;
		}
		Fixture f(processor);
		for(int i = 0; i < 4; ++i)
			f.frame();
		const auto size = f.component->getDocumentSize();
		std::printf("document %dx%d, dp ratio %.2f\n", static_cast<int>(size.x), static_cast<int>(size.y), f.dp());
		require(size.x == 1296 && size.y == 824, "the document is not 1296 x 824");

		if(const auto* png = std::getenv("MD_DRUMS_SKIN_PNG"))
		{
			juce::FileOutputStream out{juce::File(juce::String(png))};
			out.setPosition(0);
			out.truncate();
			juce::PNGImageFormat().writeImageToStream(f.image, out);
		}

		std::string failures;
		for(const auto& [name, check] : std::vector<std::pair<std::string, void (*)(Fixture&)>>{
			{"boxes", boxes}, {"baselines", baselines}, {"bound", bound}, {"input to pixel", inputToPixel}})
		{
			try
			{
				check(f);
			}
			catch(const std::exception& e)
			{
				failures += "\n  " + name + ": " + e.what();
			}
		}
		if(!failures.empty())
		{
			std::cout << "mdDrumsSkinTest: FAIL" << failures << '\n';
			return 1;
		}
		std::cout << "mdDrumsSkinTest: PASS\n";
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cout << "mdDrumsSkinTest: FAIL: " << e.what() << '\n';
		return 1;
	}
}
