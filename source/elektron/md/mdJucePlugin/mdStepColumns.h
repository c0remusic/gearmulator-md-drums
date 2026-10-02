#pragma once

#include <array>
#include <cstdint>

namespace juce
{
	class Graphics;
}

namespace mdJucePlugin::stepColumns
{
	// The 32 columns of JOUER's grid, as gen_editor.py lays them out: a step's cell 24 dp wide in a 29 dp
	// column. A canvas under the grid (lane, piano roll) spans the 32 columns from the first cell.
	constexpr uint8_t Count = 32;
	constexpr float CellFraction = 24.0f / 29.0f;

	// Over what is drawn already: a lighter band behind every other beat of 4 steps, a line between the
	// two bars, as behind the grid
	void paintBeats(juce::Graphics& _g, float _width, float _height);

	// A lane: on each step with a value (0..127, -1 for none) a bar of its height on the step's cell, grey
	// for the Kit's value, orange with a dot above for a lock
	void paintLane(juce::Graphics& _g, float _width, float _height, const std::array<int, Count>& _values,
		const std::array<bool, Count>& _locks);
}
