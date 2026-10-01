#include "mdOutputMetersView.h"

#include "mdController.h"
#include "mdPluginProcessor.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Transform.h"
#include "RmlUi/Core/TransformPrimitive.h"

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
			for(uint8_t channel = 0; channel < state.meters.size(); ++channel)
			{
				state.meters[channel].fill = _document.GetElementById("mdEdMeterFill" + id + "_" + std::to_string(channel));
				state.meters[channel].peak = _document.GetElementById("mdEdMeterPeak" + id + "_" + std::to_string(channel));
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

	std::string OutputMetersView::trackList(const std::array<std::optional<Output>, md::automation::machinedrum::TrackCount>& _outputs,
		const Output _output)
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

	std::string OutputMetersView::routedTracks(const std::array<std::optional<Output>, md::automation::machinedrum::TrackCount>& _outputs,
		const uint8_t _bus)
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

	bool OutputMetersView::hasTracks(const std::array<std::optional<Output>, md::automation::machinedrum::TrackCount>& _outputs,
		const uint8_t _bus)
	{
		return std::any_of(_outputs.begin(), _outputs.end(), [_bus](const auto& _output)
		{
			return _output && feeds(*_output, _bus);
		});
	}

	bool OutputMetersView::updateMeter(Meter& _meter, const float _peak, const float _rms, const double _seconds,
		const double _nowMilliseconds, const bool _draw)
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

		if(!_draw)
		{
			// Hidden: drawn again in full once shown
			_meter.shownLevel = -1.0f;
			_meter.shownHold = -1.0f;
			return false;
		}
		bool changed = false;
		// Transforms only: a meter moving 60 times a second does not lay the page out again
		const auto level = meterPosition(_meter.level);
		if(_meter.fill && (std::abs(level - _meter.shownLevel) >= g_step || (level == 0.0f) != (_meter.shownLevel == 0.0f)))
		{
			_meter.shownLevel = level;
			// A zero scale is a singular transform, which RmlUi draws untransformed: hide the fill instead
			_meter.fill->SetClass("mdEdHidden", level <= 0.0f);
			if(level > 0.0f)
			{
				_meter.fill->SetProperty(Rml::PropertyId::Transform,
					Rml::Transform::MakeProperty(_meter.fill->GetCoreInstance(), {Rml::Transforms::ScaleY(level)}));
			}
			changed = true;
		}
		const auto hot = _meter.hold >= g_hot;
		if(_meter.fill && hot != _meter.shownHot)
		{
			_meter.shownHot = hot;
			_meter.fill->SetClass("mdEdMeterHot", hot);
			changed = true;
		}
		const auto hold = meterPosition(_meter.hold);
		if(_meter.peak && (std::abs(hold - _meter.shownHold) >= g_step || (hold == 0.0f) != (_meter.shownHold == 0.0f)))
		{
			_meter.shownHold = hold;
			_meter.peak->SetClass("mdEdHidden", hold <= 0.0f);
			_meter.peak->SetProperty(Rml::PropertyId::Transform,
				Rml::Transform::MakeProperty(_meter.peak->GetCoreInstance(), {Rml::Transforms::TranslateY((1.0f - hold) * 100.0f, Rml::Unit::PERCENT)}));
			changed = true;
		}
		return changed;
	}

	bool OutputMetersView::update(const double _nowMilliseconds)
	{
		const auto seconds = m_lastUpdate > 0.0 ? std::clamp((_nowMilliseconds - m_lastUpdate) / 1000.0, 0.0, 1.0) : 0.0;
		m_lastUpdate = _nowMilliseconds;
		const bool levelText = _nowMilliseconds - m_lastLevelText >= g_levelTextMilliseconds;
		if(levelText)
			m_lastLevelText = _nowMilliseconds;

		// Levels are taken either way; the DOM only changes while MIX is shown, so a
		// hidden page does not ask for a new frame 60 times a second.
		// The meter itself, not its fill, which is hidden while silent
		const auto* first = m_buses[0].meters[0].fill ? m_buses[0].meters[0].fill->GetParentNode() : nullptr;
		const bool shown = first && first->IsVisible(true);
		auto& meters = m_processor.getOutputMeters();
		bool changed = false;
		for(uint8_t bus = 0; bus < BusCount; ++bus)
		{
			auto& state = m_buses[bus];
			float busHold = 0.0f;
			for(uint8_t channel = 0; channel < state.meters.size(); ++channel)
			{
				const auto level = meters.take(static_cast<size_t>(bus * 2 + channel));
				changed |= updateMeter(state.meters[channel], level.peak, level.rms, seconds, _nowMilliseconds, shown);
				busHold = std::max(busHold, state.meters[channel].hold);
			}
			if(!shown)
			{
				state.shownLevel = 1;
				continue;
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
