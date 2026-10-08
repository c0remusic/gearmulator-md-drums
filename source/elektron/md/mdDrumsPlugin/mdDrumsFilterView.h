#pragma once

#include "mdDrumsFilterResponse.h"

#include "baseLib/event.h"

#include <array>
#include <memory>
#include <string>

namespace juce
{
	class Graphics;
}

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;

	// The Filter and EQ screen (Design/HANDOFF.md, "Filter and EQ"; ticket 22): the shown Track's EQ and filter response
	// from the engine's own coefficients (FilterResponse), 20 Hz to 20 kHz, +24 to -29 dB, in 2 px accent over the
	// graduations 100, 1k, 10k and "0 dB", the EQ's point as a 6 x 6 ink square on the curve, and the note "SRR n" when
	// SRR is above 0. It repaints when EQF, EQG, FLTF, FLTW, FLTQ or the shown Track change, computing the curve as it
	// paints (once a frame at most, about 50 us).
	class FilterView
	{
	public:
		static constexpr double LowHz = 20.0;
		static constexpr double Decades = 3.0;	// to 20 kHz
		static constexpr float DbAbove = 24.0f;	// at the plot's top

		FilterView(Editor& _editor, std::shared_ptr<const md::engine::TrackFx::Tables> _tables);
		~FilterView();

		FilterView(const FilterView&) = delete;
		FilterView& operator=(const FilterView&) = delete;

		const FilterResponse* response() const { return m_response.get(); }
		// Reads the parameters again (their notifications reach it later, from the message loop)
		void refresh() { update(); }
		// Where the plot puts _hz and _db, in the screen's canvas, in dp
		static float xOf(double _hz);
		static float yOf(double _db);

	private:
		void onPartChanged(uint8_t _part);
		void update();
		void paint(juce::Graphics& _g) const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		std::unique_ptr<FilterResponse> m_response;
		Rml::Element* m_screen = nullptr;
		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		uint8_t m_part = 0;
		baseLib::EventListener<uint8_t> m_onPartChanged;
		std::array<baseLib::EventListener<pluginLib::Parameter*>, 6> m_onChanged;
	};
}
