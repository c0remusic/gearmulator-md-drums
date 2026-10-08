// MD Drums' v22 skin against the approved mockup (Design/HANDOFF.md, tickets 17 and 18 of the editor map), with the
// software renderer at 100 %, view by view (the Track, Mix and Master tabs, the Kits, the machine browser, the LFO's
// target menu):
// - every box exported from the mockup (Design/v22-boxes.txt, by v22-export-boxes.js) is the box of the skin's
//   element of the same ID, to half a pixel;
// - every text baseline among them sits on a multiple of 4 (a 0 x 0 inline-block probe on the text's line), and no
//   text overflows its box;
// - each of the 576 host parameters has a control in the skin, Track by Track;
// - input to pixel: a knob dragged and a list row clicked show on the next frame, and the event, RmlUi's update and
//   the software render take 33 ms at most;
// - the meters and Hit lights follow the Processor's Telemetry (ticket 20).
// MD_DRUMS_SKIN_PNG names a prefix for a picture of each view.

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsKnob.h"
#include "mdDrumsMeterView.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlPlugin/rmlParameterBinding.h"
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

	struct View
	{
		std::string name;
		std::vector<std::pair<std::string, Box>> boxes;
	};

	// v22-boxes.txt: "[view]" starts a view, then "key x y width height"; "#" lines are comments
	std::vector<View> readViews()
	{
		const std::string path = std::string(MD_DRUMS_DESIGN_DIR) + "/v22-boxes.txt";
		std::ifstream file(path);
		require(file.good(), "cannot read " + path);
		std::vector<View> views;
		std::string line;
		while(std::getline(file, line))
		{
			if(line.empty() || line[0] == '#')
				continue;
			if(line[0] == '[')
			{
				views.push_back({line.substr(1, line.find(']') - 1), {}});
				continue;
			}
			std::istringstream in(line);
			std::string key;
			Box b;
			if(!views.empty() && in >> key >> b.x >> b.y >> b.w >> b.h)
				views.back().boxes.emplace_back(key, b);
		}
		return views;
	}

	// The mockup's elements that carry text, by their keys
	bool hasText(const std::string& _key)
	{
		if(_key.rfind("k_", 0) == 0 || _key.rfind("rule_", 0) == 0 || _key.find("_scope") != std::string::npos
			|| _key.rfind("strip_sep", 0) == 0)
			return false;
		if(_key.rfind("screen_", 0) == 0)
			return _key.find("_title") != std::string::npos;
		for(const auto* suffix : {"_pan", "_meter", "_fader", "surface"})
		{
			const std::string s(suffix);
			if(_key.size() >= s.size() && _key.compare(_key.size() - s.size(), s.size(), s) == 0)
				return false;
		}
		static const std::set<std::string> boxesOnly{"row_sel_bg", "row_sel_mark", "main_meter", "velocity_bar", "play",
			"syn_group1", "strip_sel_bg", "strip_sel_bar", "slot_sel_bg", "slot_play_mark", "kits_sep", "kit_note",
			"lfomenu"};
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
			require(editor->getTrackView() != nullptr, "the default skin is not the v22 one");
			component->setLookAndFeel(&lookAndFeel);
		}

		~Fixture()
		{
			component->setLookAndFeel(nullptr);
			processor.destroyEditorState();
		}

		Rml::Context& context() const { return *component->getContext(); }

		Rml::Element* find(const std::string& _id) const { return editor->findChild(_id, false); }

		void layout() const
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
			context().Update();
		}

		// A view of v22-boxes.txt: its tab, and its overlay open
		void showView(const std::string& _view) const
		{
			using Overlay = mdDrums::TrackView::Overlay;
			const auto* tabElement = find(_view == "mix" ? "strip1_num" : _view == "master" ? "echo_title" : "caption");
			require(tabElement && editor->selectTabWithElement(tabElement), "no tab for the view " + _view);
			editor->getTrackView()->setOverlay(_view == "kits" ? Overlay::Kits : _view == "browser" ? Overlay::Browser
				: _view == "lfomenu" ? Overlay::LfoMenu : Overlay::None);
			layout();
		}

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

	// Elements as wide as their text in the mockup: on their row, and ending 16 px inside the screen where they do
	void textSized(Fixture& _f, const char* _key, const float _y, const float _right, std::vector<std::string>& _wrong)
	{
		auto* element = _f.find(_key);
		require(element != nullptr, std::string("no element ") + _key);
		const auto b = _f.box(*element);
		if(std::abs(b.y - _y) > 0.5f || std::abs(b.h - 24) > 0.5f)
			_wrong.push_back(std::string(_key) + ": " + describe(b) + ", not on its title's row");
		if(_right > 0 && std::abs(b.x + b.w - _right) > 0.5f)
			_wrong.push_back(std::string(_key) + " ends at " + std::to_string(b.x + b.w));
	}

	void boxes(Fixture& _f)
	{
		size_t matched = 0, total = 0;
		std::vector<std::string> wrong;
		for(const auto& view : readViews())
		{
			_f.showView(view.name);
			for(const auto& [key, want] : view.boxes)
			{
				++total;
				auto* element = _f.find(key);
				if(!element)
				{
					wrong.push_back(view.name + " " + key + ": no element");
					continue;
				}
				const auto got = _f.box(*element);
				if(std::abs(got.x - want.x) > 0.5f || std::abs(got.y - want.y) > 0.5f || std::abs(got.w - want.w) > 0.5f
					|| std::abs(got.h - want.h) > 0.5f)
					wrong.push_back(view.name + " " + key + ": " + describe(got) + ", the mockup " + describe(want));
				else
					++matched;
			}
			if(view.name == "track")
			{
				textSized(_f, "lfo_target", 624, 0, wrong);
				textSized(_f, "lfo_assign", 624, 1280, wrong);
			}
			else if(view.name == "lfomenu")
				textSized(_f, "lfomenu_done", 624, 1280, wrong);
		}
		_f.showView("track");
		std::printf("boxes: %zu of %zu as the mockup's\n", matched, total);
		for(const auto& w : wrong)
			std::printf("  %s\n", w.c_str());
		require(total > 800, "the mockup's boxes are missing");
		require(wrong.empty(), "boxes differ from the mockup's");
	}

	void baselines(Fixture& _f)
	{
		auto& document = *_f.component->getDocument();
		size_t texts = 0;
		std::vector<std::string> off, overflow;
		for(const auto& view : readViews())
		{
			_f.showView(view.name);
			std::vector<std::pair<std::string, Rml::Element*>> probes;
			for(const auto& [key, want] : view.boxes)
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
			_f.layout();
			for(const auto& [key, probe] : probes)
			{
				++texts;
				const auto y = probe->GetAbsoluteOffset(Rml::BoxArea::Border).y / _f.dp();
				const auto step = std::round(y / 4.0f) * 4.0f;
				if(std::abs(y - step) > 0.5f)
				{
					char text[96];
					(void)std::snprintf(text, sizeof(text), "%s %s: %.2f", view.name.c_str(), key.c_str(), y);
					off.push_back(text);
				}
				auto* element = probe->GetParentNode();
				if(element->GetScrollWidth() > element->GetClientWidth() + 0.5f)
					overflow.push_back(view.name + " " + key);
				element->RemoveChild(probe);
			}
		}
		_f.showView("track");
		std::printf("baselines: %zu texts, %zu off the 4 px steps, %zu overflowing\n", texts, off.size(), overflow.size());
		for(const auto& o : off)
			std::printf("  baseline %s\n", o.c_str());
		for(const auto& o : overflow)
			std::printf("  overflows %s\n", o.c_str());
		require(off.empty(), "baselines off the 4 px steps");
		require(overflow.empty(), "texts overflow their boxes");
	}

	// Each of the 576 host parameters has a control: the ones bound to the shown Track, once that Track is shown
	void bound(Fixture& _f)
	{
		auto& controller = _f.processor.getController();
		auto* binding = _f.editor->getRmlParameterBinding();
		size_t bound = 0, total = 0;
		for(uint8_t t = 0; t < mdDrums::Controller::TrackCount; ++t)
		{
			_f.editor->setCurrentPart(t);
			for(const auto& [index, parameters] : controller.getExposedParameters())
				for(auto* p : parameters)
				{
					if(p->getPart() != t)
						continue;
					++total;
					std::vector<Rml::Element*> elements;
					binding->getElementsForParameter(elements, p, false);
					if(!elements.empty())
						++bound;
					else
						std::printf("  track %d: %s has no control\n", t + 1, p->getDescription().name.c_str());
				}
		}
		_f.editor->setCurrentPart(0);
		std::printf("bound: %zu of %zu host parameters have a control\n", bound, total);
		require(total == mdDrums::Controller::ParameterCount, "not every host parameter was counted");
		require(bound == total, "a host parameter has no control");
	}

	using Clock = std::chrono::steady_clock;

	double since(const Clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - _start).count();
	}

	void inputToPixel(Fixture& _f)
	{
		_f.showView("track");
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

	bool isAccent(const juce::Colour _c)
	{
		return std::abs(_c.getRed() - 0x6f) < 12 && std::abs(_c.getGreen() - 0xd1) < 12 && std::abs(_c.getBlue() - 0xc4) < 12;
	}

	// The meters and the Hit lights (ticket 20), from the Processor's Telemetry: peaks shown on the next frame on the
	// -60 to 0 dBFS scale, falling 20 dB a second; a Hit lights its Track's numbers for 140 ms
	void meters(Fixture& _f)
	{
		auto* view = _f.editor->getMeterView();
		require(view != nullptr, "the editor has no meter view");
		auto& telemetry = _f.processor.getTelemetry();
		_f.showView("mix");
		const double now = 1.0e6;
		view->update(now);
		telemetry.raisePeak(0, 1.0f);		// Main left at 0 dBFS
		telemetry.raisePeak(1, 0.1f);		// right at -20
		telemetry.raisePeak(2, 0.5f);		// Track 1 at -6
		telemetry.hit(0);
		view->update(now + 10);
		_f.frame();
		const auto strip = _f.box(*_f.find("strip1_meter")), main = _f.box(*_f.find("main_meter"));
		const auto stripTop = strip.y + strip.h * (1.0f - view->level(2));
		std::printf("meters: Track 1 at %.3f (top at y %.1f), Main %.3f and %.3f\n", view->level(2), stripTop,
			view->level(0), view->level(1));
		require(std::abs(view->level(2) - 0.9f) < 0.01f && std::abs(view->level(1) - 0.6667f) < 0.01f, "the scale is not -60 to 0 dBFS");
		require(isAccent(_f.pixel(strip.x + 2, stripTop + 6)) && !isAccent(_f.pixel(strip.x + 2, stripTop - 6)),
			"the strip's meter does not show its level");
		require(isAccent(_f.pixel(main.x + 60, main.y + 2)) && !isAccent(_f.pixel(main.x + 50, main.y + 6))
			&& isAccent(_f.pixel(main.x + 30, main.y + 6)), "the Main meter does not show its levels");
		require(_f.find("strip1_num")->IsClassSet("trig") && _f.find("row1_num")->IsClassSet("trig"),
			"a Hit does not light its Track's numbers");
		view->update(now + 10 + 200);
		require(!_f.find("strip1_num")->IsClassSet("trig"), "a Hit's light lasts past 140 ms");
		view->update(now + 10 + 1000);
		std::printf("meters: Track 1 a second later at %.3f\n", view->level(2));
		require(std::abs(view->level(2) - (60.0f - 26.0f) / 60.0f) < 0.01f, "the meters do not fall 20 dB a second");

		// The list's scopes: half a second of Track 1 at -6 dBFS, read at 60 Hz, fills the right half of its scope
		_f.showView("track");
		double t = now + 2000;
		for(int i = 0; i < 30; ++i, t += 1000.0 / 60.0)
		{
			telemetry.raisePeak(2, 0.5f);
			view->update(t);
		}
		_f.frame();
		const auto scope = _f.box(*_f.find("row1_scope"));
		const auto centre = scope.y + scope.h * 0.5f;
		const auto right = _f.pixel(scope.x + scope.w - 3, centre).getBrightness();
		const auto left = _f.pixel(scope.x + 2, centre).getBrightness();
		std::printf("scopes: Track 1's right end %.2f, left end %.2f bright\n", right, left);
		require(right > 0.5f && left < 0.3f, "the scope does not show the last second of its Track");
	}

	void pictures(Fixture& _f, const std::string& _prefix)
	{
		for(const auto& view : readViews())
		{
			_f.showView(view.name);
			for(int i = 0; i < 3; ++i)
				_f.frame();
			juce::FileOutputStream out{juce::File(juce::String(_prefix + view.name + ".png"))};
			out.setPosition(0);
			out.truncate();
			juce::PNGImageFormat().writeImageToStream(_f.image, out);
		}
		_f.showView("track");
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
			pictures(f, png);

		std::string failures;
		for(const auto& [name, check] : std::vector<std::pair<std::string, void (*)(Fixture&)>>{
			{"boxes", boxes}, {"baselines", baselines}, {"bound", bound}, {"input to pixel", inputToPixel},
			{"meters", meters}})
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
