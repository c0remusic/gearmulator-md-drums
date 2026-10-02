#include "mdStepColumns.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>

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

	void paintLane(juce::Graphics& _g, const float _width, const float _height, const std::array<int, Count>& _values,
		const std::array<bool, Count>& _locks)
	{
		paintBeats(_g, _width, _height);
		const auto pitch = _width / Count;
		const auto cell = pitch * CellFraction;
		const auto dot = std::max(3.0f, cell / 5.0f);
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
