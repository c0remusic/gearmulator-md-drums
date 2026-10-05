#include "mdOutputMetersView.h"

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "juceRmlUi/rmlElemCanvas.h"

#include "RmlUi/Core/Element.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace mdJucePlugin
{
	namespace
	{
		constexpr double g_holdMilliseconds = 1000.0;
		// Falling meters lose 20 dB per second
		constexpr double g_fallPerSecond = 0.1;
		// From about -1 dBFS the meter turns orange
		constexpr float g_hot = 0.891f;
		// Positions closer than this (a quarter of a dB) are not redrawn
		constexpr float g_step = 0.004f;
		// The dB line changes at most this often, so it can be read
		constexpr double g_levelTextMilliseconds = 200.0;

		const juce::Colour g_track(0xff2a2c30);
		const juce::Colour g_bar(0xffc9c5bc);
		const juce::Colour g_hotBar(0xffff6b1f);
		const juce::Colour g_peak(0xfff4f1ea);

		using Output = md::automation::sysex::TrackOutput;

		bool feeds(const Output _output, const uint8_t _bus)
		{
			switch(_bus)
			{
			case 0: return _output == Output::Main || _output == Output::A || _output == Output::B;
			case 1: return _output == Output::C || _output == Output::D;
			default: return _output == Output::E || _output == Output::F;
			}
		}
	}

	OutputMetersView::OutputMetersView(AudioPluginAudioProcessor& _processor, Controller& _controller, Rml::Element& _document)
		: m_processor(_processor)
		, m_controller(_controller)
		, m_model(_processor.getModel())
	{
		for(uint8_t bus = 0; bus < BusCount; ++bus)
		{
			auto& state = m_buses[bus];
			const auto id = std::to_string(bus);
			state.area = _document.GetElementById("mdEdMeters" + id);
			if(state.area)
			{
				state.canvas = juceRmlUi::ElemCanvas::create(state.area);
				state.canvas->setRepaintGraphicsCallback([this, bus](juce::Image& _image, juce::Graphics& _g)
				{
					paint(m_buses[bus], _image, _g);
				});
			}
			state.level = _document.GetElementById("mdEdBusLevel" + id);
			state.tracks = _document.GetElementById("mdEdBusTracks" + id);
			state.warning = _document.GetElementById("mdEdBusWarning" + id);
			state.active = _document.GetElementById("mdEdBusActive" + id);
		}
	}

	float OutputMetersView::meterPosition(const float _level)
	{
		// -60 dBFS
		if(!(_level > 0.001f))
			return 0.0f;
		return std::clamp((20.0f * std::log10(_level) + 60.0f) / 60.0f, 0.0f, 1.0f);
	}

	float OutputMetersView::levelPosition(const uint8_t _bus, const uint8_t _channel) const
	{
		return _bus < BusCount && _channel < 2 ? m_buses[_bus].meters[_channel].drawnLevel : 0.0f;
	}

	float OutputMetersView::holdPosition(const uint8_t _bus, const uint8_t _channel) const
	{
		return _bus < BusCount && _channel < 2 ? m_buses[_bus].meters[_channel].drawnHold : 0.0f;
	}

	bool OutputMetersView::isHot(const uint8_t _bus, const uint8_t _channel) const
	{
		return _bus < BusCount && _channel < 2 && m_buses[_bus].meters[_channel].drawnHot;
	}

	std::string OutputMetersView::trackList(const TrackOutputs& _outputs, const Output _output)
	{
		std::string text;
		int runStart = -1;
		const auto size = static_cast<int>(_outputs.size());
		for(int track = 0; track <= size; ++track)
		{
			const bool routed = track < size && _outputs[track] == _output;
			if(routed && runStart < 0)
				runStart = track;
			if(routed || runStart < 0)
				continue;
			// The run of tracks runStart .. track - 1, numbered from 1
			text += (text.empty() ? "" : ", ") + std::to_string(runStart + 1);
			if(track - runStart == 2)
				text += ", " + std::to_string(track);
			else if(track - runStart > 2)
				text += "–" + std::to_string(track);
			runStart = -1;
		}
		return text;
	}

	std::string OutputMetersView::routedTracks(const TrackOutputs& _outputs, const uint8_t _bus)
	{
		if(std::any_of(_outputs.begin(), _outputs.end(), [](const auto& _output) { return !_output; }))
			return "routage : en attente du Global";
		std::string text;
		const auto add = [&text](const std::string& _part)
		{
			text += (text.empty() ? "" : " · ") + _part;
		};
		if(_bus == 0)
		{
			// MAIN tracks are counted, the ones alone on A or B named
			const auto main = static_cast<int>(std::count(_outputs.begin(), _outputs.end(), std::optional<Output>(Output::Main)));
			if(main == static_cast<int>(_outputs.size()))
				add("MAIN : les " + std::to_string(main) + " pistes");
			else if(main > 0)
				add("MAIN : " + std::to_string(main) + (main == 1 ? " piste" : " pistes"));
		}
		const auto first = static_cast<uint8_t>(_bus * 2);
		for(uint8_t output = first; output < first + 2; ++output)
		{
			const auto list = trackList(_outputs, static_cast<Output>(output));
			if(!list.empty())
				add(std::string(1, static_cast<char>('A' + output)) + " : " + list);
		}
		return text.empty() ? "aucune piste" : text;
	}

	bool OutputMetersView::hasTracks(const TrackOutputs& _outputs, const uint8_t _bus)
	{
		return std::any_of(_outputs.begin(), _outputs.end(), [_bus](const auto& _output)
		{
			return _output && feeds(*_output, _bus);
		});
	}

	bool OutputMetersView::advance(Meter& _meter, const float _peak, const float _rms, const double _seconds,
		const double _nowMilliseconds)
	{
		const auto fall = static_cast<float>(std::pow(g_fallPerSecond, _seconds));
		_meter.level = std::max(_rms, _meter.level * fall);
		if(_peak >= _meter.hold)
		{
			_meter.hold = _peak;
			_meter.holdTime = _nowMilliseconds;
		}
		else if(_nowMilliseconds - _meter.holdTime > g_holdMilliseconds)
		{
			_meter.hold = std::max(_peak, _meter.hold * fall);
		}
		const auto level = meterPosition(_meter.level);
		const auto hold = meterPosition(_meter.hold);
		const auto hot = _meter.hold >= g_hot;
		const auto moved = [](const float _shown, const float _now)
		{
			return std::abs(_now - _shown) >= g_step || (_now == 0.0f) != (_shown == 0.0f);
		};
		if(!moved(_meter.drawnLevel, level) && !moved(_meter.drawnHold, hold) && hot == _meter.drawnHot)
			return false;
		_meter.drawnLevel = level;
		_meter.drawnHold = hold;
		_meter.drawnHot = hot;
		return true;
	}

	void OutputMetersView::paint(const Bus& _bus, juce::Image& _image, juce::Graphics& _g)
	{
		_image.clear(_image.getBounds());
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < 3.0f || h < 3.0f)
			return;
		// Two meters with a gap of a fifth of the width
		const auto gap = w / 5.0f;
		const auto meterWidth = (w - gap) / 2.0f;
		const auto lineHeight = std::max(1.5f, h / 60.0f);
		for(size_t channel = 0; channel < _bus.meters.size(); ++channel)
		{
			const auto& meter = _bus.meters[channel];
			const auto x = static_cast<float>(channel) * (meterWidth + gap);
			_g.setColour(g_track);
			_g.fillRect(x, 0.0f, meterWidth, h);
			if(meter.drawnLevel > 0.0f)
			{
				_g.setColour(meter.drawnHot ? g_hotBar : g_bar);
				_g.fillRect(x, h - meter.drawnLevel * h, meterWidth, meter.drawnLevel * h);
			}
			if(meter.drawnHold > 0.0f)
			{
				_g.setColour(g_peak);
				_g.fillRect(x, std::min(h - lineHeight, h - meter.drawnHold * h), meterWidth, lineHeight);
			}
		}
	}

	bool OutputMetersView::update(const double _nowMilliseconds)
	{
		const auto seconds = m_lastUpdate > 0.0 ? std::clamp((_nowMilliseconds - m_lastUpdate) / 1000.0, 0.0, 1.0) : 0.0;
		m_lastUpdate = _nowMilliseconds;
		const bool levelText = _nowMilliseconds - m_lastLevelText >= g_levelTextMilliseconds;
		if(levelText)
			m_lastLevelText = _nowMilliseconds;

		// Levels are taken either way; nothing is drawn while MIX is hidden, so a hidden
		// page does not ask for a new frame 60 times a second.
		const bool shown = m_buses[0].area && m_buses[0].area->IsVisible(true);
		auto& meters = m_processor.getOutputMeters();
		bool changed = false;
		for(uint8_t bus = 0; bus < BusCount; ++bus)
		{
			auto& state = m_buses[bus];
			float busHold = 0.0f;
			bool moved = false;
			for(uint8_t channel = 0; channel < state.meters.size(); ++channel)
			{
				const auto level = meters.take(static_cast<size_t>(bus * 2 + channel));
				moved |= advance(state.meters[channel], level.peak, level.rms, seconds, _nowMilliseconds);
				busHold = std::max(busHold, state.meters[channel].hold);
			}
			if(!shown)
			{
				// Drawn in full once shown again
				state.drawn = false;
				state.shownLevel = 1;
				continue;
			}
			if(state.canvas && (moved || !state.drawn))
			{
				state.canvas->repaint();
				state.drawn = true;
				changed = true;
			}

			if(state.level && levelText)
			{
				// Whole dB; "—" below -60 dBFS
				const auto decibels = busHold > 0.001f ? static_cast<int>(std::lround(20.0f * std::log10(busHold))) : -100;
				if(decibels != state.shownLevel)
				{
					state.shownLevel = decibels;
					state.level->SetInnerRML(decibels <= -60 ? std::string("—")
						: (decibels < 0 ? "−" + std::to_string(-decibels) : std::to_string(decibels)) + " dB");
					changed = true;
				}
			}
		}

		// The host turns buses on and off (the main bus is always on); the routing says
		// which tracks each bus carries, and which are muted by a bus the host left off.
		const auto routing = m_controller.getRoutingRevision();
		bool activeChanged = false;
		std::array<int, BusCount> active{};
		for(uint8_t bus = 0; bus < BusCount; ++bus)
		{
			const auto* hostBus = m_processor.getBus(false, bus);
			active[bus] = hostBus && hostBus->isEnabled() ? 1 : 0;
			activeChanged |= active[bus] != m_buses[bus].shownActive;
		}
		if(!shown || (routing == m_shownRouting && !activeChanged))
			return changed;
		m_shownRouting = routing;
		TrackOutputs outputs{};
		for(uint8_t track = 0; track < outputs.size(); ++track)
			outputs[track] = m_controller.getTrackOutput(track);
		static constexpr const char* names[] = {"Main A/B", "Out C/D", "Out E/F"};
		for(uint8_t bus = 0; bus < BusCount; ++bus)
		{
			auto& state = m_buses[bus];
			state.shownActive = active[bus];
			if(state.active)
			{
				state.active->SetClass("mdEdSelected", active[bus] != 0);
				state.active->SetInnerRML(active[bus] ? "ACTIF DANS LE DAW" : "INACTIF DANS LE DAW");
			}
			const bool machinedrum = m_model == md::MachineModel::Machinedrum;
			if(state.tracks)
				state.tracks->SetInnerRML(machinedrum ? routedTracks(outputs, bus) : std::string("routage des pistes : inconnu sur le MM"));
			if(state.warning)
			{
				const bool muted = machinedrum && !active[bus] && hasTracks(outputs, bus);
				state.warning->SetInnerRML(muted ? "pistes muettes : activer " + std::string(names[bus]) + " dans le DAW" : std::string());
			}
		}
		return true;
	}
}
