#pragma once

#include "mdLib/mdsysexautomation.h"
#include "mdLib/mdtypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;

	// The SORTIES block of MIX, one panel per output bus (A/B, C/D, E/F): two meters
	// (mdEdMeterFill<bus>_<channel> and the peak line mdEdMeterPeak<bus>_<channel>),
	// the highest peak in dB (mdEdBusLevel<bus>), the tracks routed to the bus
	// (mdEdBusTracks<bus>, Machinedrum) and whether the host has the bus on
	// (mdEdBusActive<bus>). Levels come from the processor's OutputMeters.
	class OutputMetersView
	{
	public:
		static constexpr uint8_t BusCount = 3;

		OutputMetersView(AudioPluginAudioProcessor& _processor, Controller& _controller, Rml::Element& _document);

		// Takes the levels measured since the last call and redraws what changed.
		// Returns true when it changed the DOM.
		bool update(double _nowMilliseconds);

		// Where a level sits on a meter: -60 dBFS and below at 0, 0 dBFS and above at 1.
		static float meterPosition(float _level);
		using TrackOutputs = std::array<std::optional<md::automation::sysex::TrackOutput>, md::automation::machinedrum::TrackCount>;
		// The tracks a Machinedrum routing sends to a bus, per output: "MAIN : 12 pistes ·
		// A : 9", "C : 13, 14 · D : 15", "aucune piste", or "routage : en attente du
		// Global" while one is unknown. Tracks numbered from 1, runs of three or more
		// as "1–4".
		static std::string routedTracks(const TrackOutputs& _outputs, uint8_t _bus);
		static std::string trackList(const TrackOutputs& _outputs, md::automation::sysex::TrackOutput _output);
		static bool hasTracks(const TrackOutputs& _outputs, uint8_t _bus);

	private:
		struct Meter
		{
			Rml::Element* fill = nullptr;
			Rml::Element* peak = nullptr;
			float level = 0.0f;           // shown RMS, falling 20 dB/s
			float hold = 0.0f;            // peak held 1 s, then falling 20 dB/s
			double holdTime = 0.0;
			float shownLevel = -1.0f;     // positions written to the DOM
			float shownHold = -1.0f;
			bool shownHot = false;
		};

		struct Bus
		{
			std::array<Meter, 2> meters{};
			Rml::Element* level = nullptr;
			Rml::Element* tracks = nullptr;
			Rml::Element* warning = nullptr;
			Rml::Element* active = nullptr;
			int shownLevel = 1;           // whole dB, 1 for not shown yet
			int shownActive = -1;
		};

		bool updateMeter(Meter& _meter, float _peak, float _rms, double _seconds, double _nowMilliseconds, bool _draw);

		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;
		const md::MachineModel m_model;
		std::array<Bus, BusCount> m_buses{};
		double m_lastUpdate = 0.0;
		double m_lastLevelText = 0.0;
		uint64_t m_shownRouting = ~uint64_t{0};
	};
}
