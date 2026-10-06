#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MachineRunner.h"

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
		void setLevel(int _track, int _level);				// 0-127
		void setMute(int _track, bool _mute);
		void setTempo(double _bpm);
		void trigger(int _track, int _velocity);			// 1-127

		// _count samples of the dry main mix (no master effects), full scale 1.0.
		void render(float* _left, float* _right, size_t _count);

	private:
		struct State;
		std::unique_ptr<State> m_state;
	};
}
