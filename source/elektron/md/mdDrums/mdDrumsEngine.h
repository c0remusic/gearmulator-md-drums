#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MachineRunner.h"

#include "mdProtocol/mdlfosettings.h"

namespace mdDrums
{
	// The Machinedrum's engines without its OS (mdEngine), as a plug-in drives them: 16 tracks, each with a
	// machine, the 24 track parameters and a level, triggered by notes, rendered at the engine's own 44.1 kHz.
	// A change or a trigger takes effect at the next 32-sample block the engine renders.
	class Engine
	{
	public:
		static constexpr double SampleRate = 44100.0;
		static constexpr int TrackCount = 16;
		static constexpr int ParamCount = 24;	// SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST VOL PAN DEL REV LFOS LFOD LFOM
		static constexpr int BlockSize = 32;

		// The parameter names, in parameter order (SYN1-8 are named per machine: MachineInfo::params).
		static const std::array<const char*, ParamCount>& paramNames();
		// What every track starts with: filter open, no effect, VOL 100, PAN centre, LFO at zero depth (mdEngine
		// itself starts at 0 everywhere, which is silent). SYN1-8 start at the machine's own defaults.
		static const std::array<uint8_t, ParamCount>& paramDefaults();
		static constexpr int DefaultLevel = 100;
		// The machine each track starts with: TRX-BD, -SD, -XT, -CP, -RS, -CB, -CH, -OH, -CY, -MA, -CL, -XC, -B2,
		// -S2, then EFM-BD and -SD.
		static uint8_t defaultMachine(int _track);

		// The 8 MB Machinedrum UW flash image with OS 1.63: GEARMULATOR_MD_FIRMWARE_BIN, then
		// elektron_sps1-1uw_os1.63.bin in _folders and in the folders the Gearmulator plug-ins use. Empty if none.
		static std::vector<uint8_t> findFlashImage(const std::vector<std::filesystem::path>& _folders);

		// Throws md::fw::FirmwareError or std::runtime_error when the image holds no usable OS.
		explicit Engine(const std::vector<uint8_t>& _flashImage);
		~Engine();

		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		const std::vector<md::engine::MachineInfo>& machines() const;

		void setMachine(int _track, uint8_t _machineId);
		void setParam(int _track, int _param, int _value);	// 0-127
		// A Lock (0-127, ADR 0003) for _track's Hit in the next block: the parameter plays it at once until the track's
		// next Hit, which plays the Kit's value again unless it has its own; dropped if no Hit of _track comes in the block
		void lock(int _track, int _param, int _value);
		void setLevel(int _track, int _level);				// 0-127
		// A muted track is silent and drops its Hits, firing neither its Link nor its Choke
		void setMute(int _track, bool _mute);
		void setTempo(double _bpm);
		void trigger(int _track, int _velocity);			// 1-127

		// The Kit's Links and Chokes (trig and mute groups): a Hit of _track also hits _target at the same velocity, or
		// silences _target until its own next Hit, as OS 1.63 plays them (md::engine::HostModel::setLink). NoTrack,
		// or any track out of range, switches one off.
		static constexpr int NoTrack = -1;
		void setLink(int _track, int _target);
		void setChoke(int _track, int _target);

		// A track's LFO: the fields SET LFO PARAM ($62) sets, or a Kit's whole 36-byte block (the settings, then the
		// running LFO's state), which a Kit load copies into the running LFO as the OS does
		static constexpr int LfoBytes = 36;
		void setLfo(int _track, const md::LfoSettings& _lfo);
		void loadLfo(int _track, const uint8_t* _block);

		// Outputs: the dry main mix (no master effects) left and right, then each track alone (after its effects
		// and VOL, before PAN: the MD's individual-output formula).
		static constexpr int OutputCount = 2 + TrackCount;

		// Tracks (bit n = track n + 1) that leave the main mix and play on their own output only.
		void setSeparateOutputs(uint32_t _tracks);

		// _count samples of each output, full scale 1.0; _outputs holds OutputCount pointers, nullptr for an output
		// not wanted.
		void render(float* const* _outputs, size_t _count);
		void render(float* _left, float* _right, size_t _count);

	private:
		struct State;
		std::unique_ptr<State> m_state;
	};
}
