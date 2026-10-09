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
// - the meters and Hit lights follow the Processor's Telemetry (ticket 20), and so does the Hit screen (ticket 21);
// - the Filter and EQ and LFO screens draw what the shown Track's parameters give, an LFO's target is underlined, ASSIGN
//   and Esc work as the HANDOFF says, and a machine chosen in the browser plays its Track (ticket 22);
// - the play key and Space play the shown Track at the velocity a drag sets, the wheel and the arrows step its machine
//   (ticket 23).
// MD_DRUMS_SKIN_PNG names a prefix for a picture of each view.

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFilterView.h"
#include "mdDrumsHitView.h"
#include "mdDrumsKitsView.h"
#include "mdDrumsKnob.h"
#include "mdDrumsLfoView.h"
#include "mdDrumsMeterView.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlPlugin/rmlParameterBinding.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "synthLib/plugin.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ElementText.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
					// The relays for Push 2 have no control: they aim at the Track the editor shows
					if(p->getPart() != t || p->getDescription().page == mdDrums::messages::pages::Focus)
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

	// The Hit screen (ticket 21): a capture the Telemetry holds shows on the next frame: its note, the window that fits
	// it, its waveform in ink, the playhead while it grows, the graduations' words
	void hitScreen(Fixture& _f)
	{
		auto* view = _f.editor->getHitView();
		require(view != nullptr, "the editor has no Hit view");
		auto& telemetry = _f.processor.getTelemetry();
		_f.showView("track");
		_f.editor->setCurrentPart(0);
		const double now = 2.0e6;
		view->update(now);
		require(_f.find("hit_note")->GetInnerRML() == "Not played yet", "the Hit screen's note before a Hit");

		// A Hit decaying from 0.4 over 200 columns, 600 columns in (0.109 s): audible for 0.127 s, so a window of 0.2 s
		telemetry.beginCapture(0, 110);
		for(int c = 0; c < 600; ++c)
		{
			const auto a = static_cast<int16_t>(0.4f * std::exp(-c / 200.0f) * 32767.0f);
			telemetry.addColumn(0, static_cast<int16_t>(-a), a);
		}
		view->update(now + 10);
		_f.frame();
		const auto screen = _f.box(*_f.find("screen_hit"));
		const auto left = screen.x + 16, width = screen.w - 32, top = screen.y + 48, mid = screen.y + (216 - 36 + 48) / 2.0f;
		const auto playhead = left + static_cast<float>(600 * 8 / 44100.0 / view->window()) * width;
		const auto ink = _f.pixel(left + 1, mid - 30).getBrightness();
		const bool accentHead = isAccent(_f.pixel(playhead, top + 4));
		std::printf("hit screen: window %.1f s, note \"%s\", waveform %.2f bright, playhead at x %.0f %s\n", view->window(),
			_f.find("hit_note")->GetInnerRML().c_str(), ink, playhead, accentHead ? "drawn" : "missing");
		require(view->window() == 0.2, "the window does not fit the Hit");
		require(_f.find("hit_note")->GetInnerRML() == "Last hit, velocity 110", "the Hit screen's note");
		require(view->playing() && accentHead, "no playhead while the Hit plays");
		require(ink > 0.85f, "the Hit's waveform is not drawn in ink");
		auto* tick2 = _f.find("hit_tick2");
		auto* tick5 = _f.find("hit_tick5");
		require(tick2->GetInnerRML() == "50 ms" && tick2->IsVisible(true) && !tick5->IsVisible(true),
			"the graduations do not follow the window");

		// The capture stops growing: the playhead goes
		view->update(now + 200);
		require(!view->playing(), "the playhead stays after the Hit");
	}

	void set(Fixture& _f, const char* _name, const uint8_t _part, const int _value)
	{
		auto* p = _f.processor.getController().getParameter(_name, _part);
		require(p != nullptr, std::string("no parameter ") + _name);
		p->setUnnormalizedValueNotifyingHost(_value, pluginLib::Parameter::Origin::Ui);
		// A parameter tells the views later, from the message loop, which the test does not run
		_f.editor->getFilterView()->refresh();
		_f.editor->getLfoView()->refresh();
	}

	// A click as the mouse makes it, at an element's centre
	void click(Fixture& _f, const std::string& _id)
	{
		auto* element = _f.find(_id);
		require(element != nullptr, "no element " + _id);
		const auto b = _f.box(*element);
		const auto r = _f.dp();
		auto& context = _f.context();
		context.ProcessMouseMove(juce::roundToInt((b.x + b.w * 0.5f) * r), juce::roundToInt((b.y + b.h * 0.5f) * r), 0);
		context.ProcessMouseButtonDown(0, 0);
		context.ProcessMouseButtonUp(0, 0);
	}

	// The texts the screens' views write, on the 4 px steps like the mockup's
	void viewBaselines(Fixture& _f, const std::vector<std::string>& _ids)
	{
		auto& document = *_f.component->getDocument();
		for(const auto& id : _ids)
		{
			auto* element = _f.find(id);
			require(element != nullptr, "no element " + id);
			auto probe = document.CreateElement("span");
			probe->SetProperty("display", "inline-block");
			probe->SetProperty("width", "0dp");
			probe->SetProperty("height", "0dp");
			auto* p = element->AppendChild(std::move(probe));
			_f.layout();
			const auto y = p->GetAbsoluteOffset(Rml::BoxArea::Border).y / _f.dp();
			element->RemoveChild(p);
			require(std::abs(y - std::round(y / 4.0f) * 4.0f) <= 0.5f, id + "'s baseline at " + std::to_string(y));
		}
	}

	// The brightest accent of a 3 x 3 patch: a 2 px stroke anti-aliased lands on whole pixels or between them
	bool accentNear(Fixture& _f, const float _x, const float _y)
	{
		for(int dy = -1; dy <= 1; ++dy)
			for(int dx = -1; dx <= 1; ++dx)
				if(isAccent(_f.pixel(_x + dx, _y + dy)))
					return true;
		return false;
	}

	// The Filter and EQ screen (ticket 22): the response the shown Track's EQ and filter give, on the next frame
	void filterScreen(Fixture& _f)
	{
		auto* view = _f.editor->getFilterView();
		require(view != nullptr && view->response() != nullptr, "the editor has no Filter and EQ view with the engine's tables");
		_f.showView("track");
		_f.editor->setCurrentPart(0);
		const auto screen = _f.box(*_f.find("screen_filter"));
		// The canvas starts after the screen's rule
		const auto at = [&](const double _hz, const double _db) { return std::make_pair(screen.x + 1 + mdDrums::FilterView::xOf(_hz), screen.y + mdDrums::FilterView::yOf(_db)); };

		// Untouched: 0 dB at 1 kHz, -4.7 dB at 20 kHz
		for(const auto& [name, value] : std::vector<std::pair<const char*, int>>{{"EQF", 64}, {"EQG", 64}, {"FLTF", 0},
			{"FLTW", 127}, {"FLTQ", 0}, {"SRR", 0}})
			set(_f, name, 0, value);
		_f.frame();
		const auto [x1k, y1k] = at(1000.0, view->response()->gain(1000.0));
		const auto [x20k, y20k] = at(19000.0, view->response()->gain(19000.0));
		std::printf("filter screen untouched: %.2f dB at 1 kHz (y %.1f), %.2f dB at 19 kHz (y %.1f)\n", view->response()->gain(1000.0),
			y1k, view->response()->gain(19000.0), y20k);
		require(accentNear(_f, x1k, y1k) && accentNear(_f, x20k, y20k), "the untouched response is not drawn");
		require(!_f.find("filter_srr")->IsVisible(true), "SRR 0 shows a note");

		// FLTF 64 and FLTQ 127: the high-pass's peak, +18 dB at 327 Hz; the EQ boosted at EQF 32, its point on the curve
		set(_f, "FLTF", 0, 64);
		set(_f, "FLTQ", 0, 127);
		set(_f, "EQF", 0, 100);
		set(_f, "EQG", 0, 127);
		set(_f, "SRR", 0, 12);
		_f.frame();
		const auto peak = view->response()->gain(327.0);
		const auto [xp, yp] = at(327.0, peak);
		const auto centre = view->response()->eqCentre();
		const auto [xe, ye] = at(centre, view->response()->gain(centre));
		const auto point = _f.pixel(xe, ye).getBrightness();
		std::printf("filter screen FLTF 64 FLTQ 127: %.1f dB at 327 Hz (x %.0f y %.1f); EQ point at %.0f Hz (x %.0f y %.1f) %.2f bright; note \"%s\"\n",
			peak, xp, yp, centre, xe, ye, point, _f.find("filter_srr")->GetInnerRML().c_str());
		require(peak > 15.0 && accentNear(_f, xp, yp), "the resonant peak is not drawn");
		require(point > 0.9f, "the EQ's point is not drawn in ink on the curve");
		require(_f.find("filter_srr")->GetInnerRML() == "SRR 12" && _f.find("filter_srr")->IsVisible(true), "SRR 12's note");
		viewBaselines(_f, {"filter_tick1", "filter_tick2", "filter_tick3", "filter_zero", "filter_srr"});
	}

	// The LFO screen (ticket 22): the wave its target takes, zoomed on what it reaches; the names it underlines
	void lfoScreen(Fixture& _f)
	{
		auto* view = _f.editor->getLfoView();
		require(view != nullptr, "the editor has no LFO view");
		_f.showView("track");
		_f.editor->setCurrentPart(0);
		// Track 1's LFO on its own FLTF at 64, a triangle at full depth
		for(const auto& [name, value] : std::vector<std::pair<const char*, int>>{{"LfoTrack", 0}, {"LfoParam", 12},
			{"LfoShape1", 0}, {"LfoShape2", 0}, {"LfoMode", 0}, {"LFOS", 64}, {"LFOD", 127}, {"LFOM", 0}, {"FLTF", 64}})
			set(_f, name, 0, value);
		_f.frame();
		const auto screen = _f.box(*_f.find("screen_lfo"));
		int top = 0, foot = 0;
		for(float x = screen.x + 68; x < screen.x + 384; x += 1.0f)
		{
			top += isAccent(_f.pixel(x, screen.y + 49));
			foot += isAccent(_f.pixel(x, screen.y + 103));
		}
		std::printf("lfo screen: TRI at LFOD 127 on FLTF 64 reaches %s to %s, %zu ticks; accent pixels %d at its top, %d at its foot\n",
			_f.find("lfo_low")->GetInnerRML().c_str(), _f.find("lfo_high")->GetInnerRML().c_str(), view->trace().words.size(), top, foot);
		require(_f.find("lfo_high")->GetInnerRML() == "127" && _f.find("lfo_low")->GetInnerRML() == "0", "the values the wave reaches");
		require(top > 3 && foot > 3, "the wave does not fill the plot");
		require(!_f.find("lfo_none")->IsVisible(true), "a wave with depth says there is none");
		viewBaselines(_f, {"lfo_high", "lfo_low"});

		// The name it modulates, underlined: ink, with a 2 px accent rule 4 px under its baseline (y 560)
		auto* label = _f.find("n_FLTF");
		require(label->IsClassSet("lfo") && !_f.find("n_FLTW")->IsClassSet("lfo"), "FLTF is not underlined, or FLTW is");
		const auto lb = _f.box(*label);
		std::vector<int> rows;
		for(int y = 556; y < 572; ++y)
			if(isAccent(_f.pixel(lb.x + 32, static_cast<float>(y))))
				rows.push_back(y);
		std::printf("lfo underline under FLTF: rows");
		for(const auto y : rows)
			std::printf(" %d", y);
		std::printf("\n");
		require(rows == std::vector<int>{564, 565}, "the underline is not 2 px, 4 px under the name's baseline");

		// No depth: no wave, the reason instead; no underline. Then the depth back, for the pictures
		set(_f, "LFOD", 0, 0);
		_f.frame();
		require(_f.find("lfo_none")->IsVisible(true) && !_f.find("lfo_high")->IsVisible(true), "LFOD 0 does not say so");
		require(!label->IsClassSet("lfo"), "a name stays underlined without depth");
		viewBaselines(_f, {"lfo_none"});
		set(_f, "LFOD", 0, 127);
	}

	// ASSIGN (ticket 22): armed, its knobs ringed and the title asking; a knob of another Track gives the LFO its target
	// and its own Track shows again; Esc disarms it, as it closes the overlays
	void assign(Fixture& _f)
	{
		auto* view = _f.editor->getLfoView();
		_f.showView("track");
		_f.editor->setCurrentPart(0);
		auto& controller = _f.processor.getController();
		set(_f, "LfoTrack", 0, 0);
		set(_f, "LfoParam", 0, 12);
		click(_f, "lfo_assign");
		_f.frame();
		auto* knob = dynamic_cast<mdDrums::Knob*>(_f.find("k_DIST"));
		const auto kb = _f.box(*knob);
		const bool ring = isAccent(_f.pixel(kb.x + 32, kb.y - 0.5f)) || isAccent(_f.pixel(kb.x + 32, kb.y - 1.0f));
		std::printf("assign: armed %d, \"%s\", asking %d, DIST a target %d, its ring %s\n", view->isArmed(),
			_f.find("lfo_assign")->GetInnerRML().c_str(), _f.find("lfo_assigning")->IsVisible(true), knob->isTarget(),
			ring ? "drawn" : "missing");
		require(view->isArmed() && _f.find("lfo_assign")->GetInnerRML() == "Cancel" && _f.find("lfo_assign")->IsClassSet("on"),
			"Assign does not arm");
		require(_f.find("lfo_assigning")->IsVisible(true) && !_f.find("lfo_target")->IsVisible(true), "the title does not ask for a knob");
		require(knob->isTarget() && ring, "the knobs are not targets with their rings");

		// Track 3 shown, its DIST clicked: LFO 1 on track 3's DIST, track 1 shown again, disarmed
		click(_f, "row3_num");
		require(controller.getCurrentPart() == 2 && view->isArmed(), "showing another Track disarmed ASSIGN");
		click(_f, "k_DIST");
		_f.frame();
		const auto lfoTrack = controller.getParameter("LfoTrack", 0)->getUnnormalizedValue();
		const auto lfoParam = controller.getParameter("LfoParam", 0)->getUnnormalizedValue();
		std::printf("assign: picked LFO 1 on track %d parameter %d, track %d shown, armed %d, DIST %d\n", lfoTrack + 1, lfoParam,
			controller.getCurrentPart() + 1, view->isArmed(), controller.getParameter("DIST", 2)->getUnnormalizedValue());
		require(lfoTrack == 2 && lfoParam == 16, "the knob clicked is not the LFO's target");
		require(controller.getCurrentPart() == 0 && !view->isArmed() && !knob->isTarget(), "the LFO's Track is not shown again");
		require(controller.getParameter("DIST", 2)->getUnnormalizedValue() == 0, "the knob clicked moved");

		// Esc: disarms, back to the LFO's Track; then closes the browser as Cancel
		click(_f, "lfo_assign");
		click(_f, "row5_num");
		_f.context().ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		require(!view->isArmed() && controller.getCurrentPart() == 0, "Esc does not disarm ASSIGN");
		_f.editor->getTrackView()->setOverlay(mdDrums::TrackView::Overlay::Browser);
		_f.context().ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		require(_f.editor->getTrackView()->getOverlay() == mdDrums::TrackView::Overlay::None, "Esc does not close the browser");
		std::printf("assign: Esc disarms and closes the browser\n");
		set(_f, "LfoTrack", 0, 0);
		set(_f, "LfoParam", 0, 12);
	}

	// Listen while choosing (ticket 22): ticked by default, kept in the settings; a machine chosen plays the Track
	void listen(Fixture& _f)
	{
		auto& controller = dynamic_cast<mdDrums::Controller&>(_f.processor.getController());
		auto* tracks = _f.editor->getTrackView();
		_f.showView("browser");
		require(tracks->isListening() && _f.find("preview_listen")->IsClassSet("on"), "Listen while choosing is not ticked");
		const auto pending = controller.pendingAuditions();
		click(_f, "mach_17");
		require(controller.getParameter("Machine", 0)->getUnnormalizedValue() == 17, "the cell did not choose its machine");
		require((controller.pendingAuditions() & 1) && !(pending & 1), "a machine chosen does not play the Track");
		click(_f, "preview_listen");
		require(!tracks->isListening() && !_f.find("preview_listen")->IsClassSet("on"), "the box does not untick");
		click(_f, "preview_listen");
		require(tracks->isListening(), "the box does not tick again");
		std::printf("listen: a machine chosen plays the Track, the box ticks and unticks\n");
		click(_f, "browser_cancel");
	}

	// Playing the Track and stepping its machine (ticket 23): the play key and Space at the head band's velocity, which a
	// drag sets; the wheel on the machine's name; the browser's arrows and Enter
	void playAndStep(Fixture& _f)
	{
		auto& controller = dynamic_cast<mdDrums::Controller&>(_f.processor.getController());
		auto* tracks = _f.editor->getTrackView();
		const auto flush = [&] { controller.processRealtimeParameterChanges(256); };
		_f.showView("track");
		_f.editor->setCurrentPart(0);
		flush();
		require(_f.find("velocity")->GetInnerRML() == "100" && std::abs(_f.box(*_f.find("velocity_fill")).w - 50.0f) < 0.5f,
			"the velocity does not start at 100");

		// A drag up of 15 px: 10 steps
		auto& context = _f.context();
		const auto r = _f.dp();
		const auto vb = _f.box(*_f.find("velocity"));
		const auto vx = vb.x + vb.w * 0.5f, vy = vb.y + vb.h * 0.5f;
		context.ProcessMouseMove(juce::roundToInt(vx * r), juce::roundToInt(vy * r), 0);
		context.ProcessMouseButtonDown(0, 0);
		for(int i = 1; i <= 5; ++i)
			context.ProcessMouseMove(juce::roundToInt(vx * r), juce::roundToInt((vy - 3.0f * i) * r), 0);
		context.ProcessMouseButtonUp(0, 0);
		_f.layout();
		const auto fill = _f.box(*_f.find("velocity_fill")).w;
		std::printf("velocity: dragged 15 px up to %s, its bar %.0f px\n", _f.find("velocity")->GetInnerRML().c_str(), fill);
		require(tracks->getPlayVelocity() == 110 && _f.find("velocity")->GetInnerRML() == "110" && std::abs(fill - 55.0f) < 0.5f,
			"the drag did not set the velocity 1.5 px a step");

		// The play key, as it goes down, and Space: the shown Track at that velocity
		auto* play = _f.find("play");
		const auto pb = _f.box(*play);
		context.ProcessMouseMove(juce::roundToInt((pb.x + 32) * r), juce::roundToInt((pb.y + 32) * r), 0);
		context.ProcessMouseButtonDown(0, 0);
		require((controller.pendingAuditions() & 1) && controller.auditionVelocity(0) == 110, "the play key does not play the Track");
		context.ProcessMouseButtonUp(0, 0);
		flush();
		_f.editor->setCurrentPart(3);
		context.ProcessKeyDown(Rml::Input::KI_SPACE, 0);
		require((controller.pendingAuditions() & 8) && controller.auditionVelocity(3) == 110, "Space does not play the shown Track");
		flush();
		_f.editor->setCurrentPart(0);
		std::printf("play: the key and Space play the shown Track at 110\n");

		// The wheel down on the machine's name: the browser's next machine, and the Track plays
		auto* machine = controller.getParameter("Machine", 0);
		set(_f, "Machine", 0, 28);
		flush();
		const auto mb = _f.box(*_f.find("machine"));
		context.ProcessMouseMove(juce::roundToInt((mb.x + 40) * r), juce::roundToInt((mb.y + 20) * r), 0);
		context.ProcessMouseWheel(Rml::Vector2f(0.0f, 1.0f), 0);
		const auto wheeled = machine->getUnnormalizedValue();
		context.ProcessMouseWheel(Rml::Vector2f(0.0f, -1.0f), 0);
		std::printf("machine: the wheel steps TRX-B2 to %d and back to %d\n", wheeled, machine->getUnnormalizedValue());
		require(wheeled == 32 && machine->getUnnormalizedValue() == 28, "the wheel does not step the machine in the browser's order");
		require(controller.pendingAuditions() & 1, "a machine stepped does not play the Track while listening");
		flush();

		// The browser: down, right (the next family's first), up, Enter keeps
		tracks->setOverlay(mdDrums::TrackView::Overlay::Browser);
		context.ProcessKeyDown(Rml::Input::KI_DOWN, 0);
		const auto down = machine->getUnnormalizedValue();
		context.ProcessKeyDown(Rml::Input::KI_RIGHT, 0);
		const auto right = machine->getUnnormalizedValue();
		context.ProcessKeyDown(Rml::Input::KI_LEFT, 0);
		context.ProcessKeyDown(Rml::Input::KI_LEFT, 0);
		const auto left = machine->getUnnormalizedValue();
		context.ProcessKeyDown(Rml::Input::KI_RETURN, 0);
		std::printf("browser keys: down %d, right %d, left twice %d, Enter keeps: overlay %d\n", down, right, left,
			static_cast<int>(tracks->getOverlay()));
		require(down == 32 && right == 48 && left == 16, "the browser's arrows do not step machines and families");
		require(tracks->getOverlay() == mdDrums::TrackView::Overlay::None && machine->getUnnormalizedValue() == 16,
			"Enter does not keep the machine");
		flush();
		set(_f, "Machine", 0, 28);
		tracks->setPlayVelocity(100);
	}

	// The Kits (ticket 26): the overlay shows the Bank's Slots and the chosen one's panel, a Slot clicked on the next frame;
	// Load asks again while the Kit played differs from its Slot, then plays it; Save is lit while it differs and Ctrl+S
	// saves it; Save here into an empty Slot makes it the one played; Rename types the name in place; Delete asks again
	// and Esc leaves the question; the arrows choose and Enter loads; Export writes the chosen Slot and Import fills the
	// empty ones from the chosen one on
	void kits(Fixture& _f)
	{
		using Overlay = mdDrums::TrackView::Overlay;
		auto& controller = dynamic_cast<mdDrums::Controller&>(_f.processor.getController());
		auto* view = _f.editor->getKitsView();
		auto* bank = _f.processor.getBank();
		require(view != nullptr && bank != nullptr, "no Kits view or no Bank");
		auto& context = _f.context();
		const auto text = [&](const std::string& _id) { return std::string(_f.find(_id)->GetInnerRML()); };
		const auto has = [&](const std::string& _id, const std::string& _what) { return text(_id).find(_what) != std::string::npos; };
		const auto visible = [&](const std::string& _id) { return _f.find(_id)->IsVisible(); };
		const auto saveLit = [&] { view->refresh(); return _f.find("kit_save")->IsClassSet("on"); };
		const auto clickNow = [&](const std::string& _id) { _f.layout(); click(_f, _id); _f.layout(); };

		_f.showView("track");
		_f.editor->getTrackView()->setOverlay(Overlay::Kits);
		_f.layout();
		require(view->isOpen() && has("slot1", "TRX UW") && has("slot16", "SEACLONES") && has("slot17", "Empty"),
			"the overlay does not show the Bank");

		// A Slot clicked: lit and its panel on the next frame
		_f.frame();
		auto* slot4 = _f.find("slot4");
		const auto sb = _f.box(*slot4);
		const auto r = _f.dp();
		context.ProcessMouseMove(juce::roundToInt((sb.x + 40) * r), juce::roundToInt((sb.y + 12) * r), 0);
		_f.frame();
		const auto start = Clock::now();
		context.ProcessMouseButtonDown(0, 0);
		context.ProcessMouseButtonUp(0, 0);
		_f.frame();
		const auto ms = since(start);
		const auto lit = _f.box(*_f.find("slot_sel_bg"));
		const auto kit4 = *bank->slot(3);
		const auto* machine = md::machines::find(md::MachineModel::Machinedrum, kit4.machine(0));
		std::printf("kits: slot 04 chosen, event to pixel %.2f ms; lit at y %g, \"%s\", track 1 %s\n", ms, lit.y,
			text("kit_caption").c_str(), machine ? std::string(machine->name).c_str() : "--");
		require(view->chosen() == 3 && std::abs(lit.y - 296) < 0.5f && text("kit_caption") == "Slot 04"
			&& has("kit_name_big", "P-I UW") && machine && has("kitm1", std::string(machine->name)),
			"a Slot clicked is not chosen with its panel");
		require(ms <= 33.0, "a Slot clicked took over 33 ms to reach the pixels");

		// Load, the Kit played changed: asked again, then played
		set(_f, "FLTF", 0, 77);
		require(saveLit(), "Save is not lit with a parameter changed");
		clickNow("kit_load");
		require(view->confirming() && std::string(view->confirming()) == "load" && visible("kit_confirm")
			&& text("kit_confirm") == "Load anyway" && visible("kit_cancel") && !visible("kit_load"),
			"Load does not ask again when the Kit played has changed");
		clickNow("kit_confirm");
		require(controller.playedSlot() == 3 && controller.playedKit() == kit4 && !saveLit() && text("kit_slot") == "04"
			&& has("kit_name", "P-I UW") && view->note() == "Playing 04, P-I UW", "Load anyway did not play the Slot");
		std::printf("kits: Load asked again, then \"%s\"\n", view->note().c_str());

		// Save lit by a change, Ctrl+S
		set(_f, "FLTF", 0, kit4.parameters[0][12] == 90 ? 91 : 90);
		require(saveLit(), "Save is not lit with a parameter changed");
		context.ProcessKeyDown(Rml::Input::KI_S, Rml::Input::KM_CTRL | Rml::Input::KM_META);
		require(!saveLit() && *bank->slot(3) == controller.playedKit(), "Ctrl+S did not save the Kit in its Slot");

		// Save here into an empty Slot: it is the one played
		clickNow("slot20");
		require(!visible("kit_load") && visible("kit_savehere") && has("kit_name_big", "Empty"), "an empty Slot's panel is wrong");
		clickNow("kit_savehere");
		require(controller.playedSlot() == 19 && bank->slot(19) && *bank->slot(19) == controller.playedKit()
			&& view->note() == "Saved in slot 20", "Save here did not put the Kit played into slot 20");

		// Rename in place: Enter keeps the name, for the Slot and the Kit played
		clickNow("kit_rename");
		_f.layout();
		require(view->isRenaming() && visible("kit_name_edit") && !visible("kit_name_big"), "Rename does not open the name");
		view->setRenameText("MY KIT");
		context.ProcessKeyDown(Rml::Input::KI_RETURN, 0);
		require(!view->isRenaming() && bank->slot(19)->displayName() == "MY KIT" && controller.playedKit().displayName() == "MY KIT"
			&& !saveLit() && has("kit_name", "MY KIT"), "Rename did not name the Slot and the Kit played");

		// Delete: asked again, Esc leaves the question; asked and confirmed, the Slot is empty and Save lit; Ctrl+S puts
		// the Kit back
		clickNow("kit_delete");
		require(view->confirming() && text("kit_confirm") == "Delete it", "Delete does not ask again");
		context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		require(!view->confirming() && view->isOpen() && bank->slot(19), "Esc did not leave the question alone");
		clickNow("kit_delete");
		clickNow("kit_confirm");
		require(!bank->slot(19) && saveLit() && has("slot20", "Empty"), "Delete did not empty the Slot played");
		context.ProcessKeyDown(Rml::Input::KI_S, Rml::Input::KM_CTRL | Rml::Input::KM_META);
		require(bank->slot(19) && !saveLit(), "Ctrl+S did not put the Kit back into its emptied Slot");

		// The arrows choose, Enter loads
		context.ProcessKeyDown(Rml::Input::KI_DOWN, 0);
		const auto down = view->chosen();
		context.ProcessKeyDown(Rml::Input::KI_LEFT, 0);
		const auto left = view->chosen();
		context.ProcessKeyDown(Rml::Input::KI_UP, 0);
		const auto up = view->chosen();
		context.ProcessKeyDown(Rml::Input::KI_RETURN, 0);
		std::printf("kits: arrows down %d, left %d, up %d (0-based), Enter plays slot %02d\n", down, left, up,
			controller.playedSlot() + 1);
		require(down == 20 && left == 4 && up == 3 && controller.playedSlot() == 3, "the arrows or Enter do not work");

		// Export the chosen Slot, import it into the empty ones from slot 40 on
		const auto file = std::filesystem::temp_directory_path() / "mdDrumsSkinTest_kit.syx";
		require(view->exportFile(file), "Export failed: " + view->note());
		view->choose(39);
		require(view->importFile(file) && bank->slot(39) && *bank->slot(39) == *bank->slot(3)
			&& view->note() == "Imported 1 kit from mdDrumsSkinTest_kit.syx into slot 40", "Import failed: " + view->note());
		std::filesystem::remove(file);
		std::printf("kits: exported slot 04, \"%s\"\n", view->note().c_str());

		context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		require(_f.editor->getTrackView()->getOverlay() == Overlay::None && !view->isOpen(), "Esc did not close the Kits");
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

		std::string failures;
		for(const auto& [name, check] : std::vector<std::pair<std::string, void (*)(Fixture&)>>{
			{"boxes", boxes}, {"baselines", baselines}, {"bound", bound}, {"input to pixel", inputToPixel},
			{"meters", meters}, {"hit screen", hitScreen}, {"filter screen", filterScreen}, {"lfo screen", lfoScreen},
			{"assign", assign}, {"listen", listen}, {"play and step", playAndStep}, {"kits", kits}})
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
		// After the checks: the views as they left them (the Hit screen with its capture)
		if(const auto* png = std::getenv("MD_DRUMS_SKIN_PNG"); png && *png)
			pictures(f, png);
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
