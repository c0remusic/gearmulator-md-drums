#pragma once

#include "mdDrumsFilterResponse.h"

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
	struct Frame;

	// The Filter and EQ screen (Design/HANDOFF.md, "Filter and EQ"; ticket 22): the shown Track's EQ and filter response
	// from the engine's own coefficients (FilterResponse), 20 Hz to 20 kHz, +24 to -29 dB, in 2 px accent over the
	// graduations 100, 1k, 10k and "0 dB", the EQ's point as a 6 x 6 ink square on the curve, and the note "SRR n" when
	// SRR is above 0. The editor's frame repaints it when EQF, EQG, FLTF, FLTW, FLTQ or the shown Track change; the curve
	// is computed as it paints (once a frame at most, about 50 us, and not while hidden).
	class FilterView
	{
	public:
		static constexpr double LowHz = 20.0;
		static constexpr double Decades = 3.0;	// to 20 kHz
		static constexpr float DbAbove = 24.0f;	// at the plot's top

		explicit FilterView(Editor& _editor);
		~FilterView();

		FilterView(const FilterView&) = delete;
		FilterView& operator=(const FilterView&) = delete;

		// The editor's frame: true when it changed the document. The response comes from the Processor's tables, taken
		// when it first has them: a Device made under an open editor (a flash image chosen, ticket 35) brings the curve
		bool step(const Frame& _frame);

		const FilterResponse* response() const { return m_response.get(); }
		// Where the plot puts _hz and _db, in the screen's canvas, in dp
		static float xOf(double _hz);
		static float yOf(double _db);

	private:
		static constexpr size_t SrrIndex = 5;	// in m_parameters, after the curve's five

		void paint(juce::Graphics& _g, float _dp) const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		std::unique_ptr<FilterResponse> m_response;
		juceRmlUi::ElemCanvas* m_canvas = nullptr;
		std::array<std::array<pluginLib::Parameter*, 6>, 16> m_parameters{};	// each Track's EQF EQG FLTF FLTW FLTQ SRR
		int m_srr = -1;				// as the note shows it; -1: not yet
		bool m_curve = false;		// the canvas painted with the response's settings
	};
}
