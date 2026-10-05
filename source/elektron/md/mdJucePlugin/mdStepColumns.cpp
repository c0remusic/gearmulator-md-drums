#include "mdStepColumns.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace mdJucePlugin::stepColumns
{
	namespace
	{
		const juce::Colour g_band(juce::Colours::white.withAlpha(0.05f));
		const juce::Colour g_bar(juce::Colours::white.withAlpha(0.18f));
		const juce::Colour g_kitBar(0xff8a8d93);
		const juce::Colour g_lockBar(0xffff6b1f);
	}

	void paintBeats(juce::Graphics& _g, const float _width, const float _height)
	{
		const auto pitch = _width / Count;
		const auto gap = pitch * (1.0f - CellFraction);
		_g.setColour(g_band);
		for(uint8_t beat = 1; beat < Count / 4; beat += 2)
			_g.fillRect(pitch * 4.0f * beat - gap / 2.0f, 0.0f, pitch * 4.0f, _height);
		_g.setColour(g_bar);
		_g.fillRect(pitch * 16.0f - gap / 2.0f - 1.0f, 0.0f, 2.0f, _height);
	}

	namespace
	{
		// A lock's dot above its bar: the room a bar of 127 leaves at the top
		float dotSize(const float _width)
		{
			return std::max(3.0f, _width / Count * CellFraction / 5.0f);
		}
	}

	int columnAt(const float _x, const float _width)
	{
		if(_x < 0.0f || _x >= _width || _width <= 0.0f)
			return -1;
		return std::min(static_cast<int>(_x / (_width / Count)), Count - 1);
	}

	uint8_t laneValueAt(const float _y, const float _width, const float _height)
	{
		const auto range = _height - dotSize(_width) - 3.0f;
		if(range <= 0.0f)
			return 0;
		return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround((_height - _y) * 127.0f / range)), 0, 127));
	}

	void paintLane(juce::Graphics& _g, const float _width, const float _height, const std::array<int, Count>& _values,
		const std::array<bool, Count>& _locks)
	{
		paintBeats(_g, _width, _height);
		const auto pitch = _width / Count;
		const auto cell = pitch * CellFraction;
		const auto dot = dotSize(_width);
		for(uint8_t step = 0; step < Count; ++step)
		{
			if(_values[step] < 0)
				continue;
			const auto top = _height - (_height - dot - 3.0f) * static_cast<float>(_values[step]) / 127.0f;
			const auto x = pitch * step;
			_g.setColour(_locks[step] ? g_lockBar : g_kitBar);
			_g.fillRect(x, top, cell, _height - top);
			if(_locks[step])
				_g.fillEllipse(x + (cell - dot) / 2.0f, top - dot - 1.0f, dot, dot);
		}
	}
}
