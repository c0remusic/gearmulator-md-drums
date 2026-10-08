#include "mdDrumsEngine.h"

#include "MdEngine.h"
#include "Firmware.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mdDrums
{
	namespace
	{
		constexpr const char* g_flashName = "elektron_sps1-1uw_os1.63.bin";
		constexpr size_t g_flashSize = 8 * 1024 * 1024;

		std::vector<uint8_t> readFlash(const std::filesystem::path& _path)
		{
			std::error_code error;
			if(!std::filesystem::is_regular_file(_path, error) || std::filesystem::file_size(_path, error) != g_flashSize)
				return {};
			std::ifstream file(_path, std::ios::binary);
			return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		}

		std::filesystem::path fromEnvironment(const char* _name)
		{
			const auto* value = std::getenv(_name);
			return value && *value ? std::filesystem::path(value) : std::filesystem::path();
		}
	}

	struct Engine::State
	{
		explicit State(md::fw::FlashOs _os) : engine(_os.firmware, std::move(_os.osImage)) {}

		md::engine::Engine engine;
		md::engine::Engine::Output out;
		size_t used = BlockSize;	// samples of out already handed out
	};

	const std::array<const char*, Engine::ParamCount>& Engine::paramNames()
	{
		static const std::array<const char*, ParamCount> names{
			"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
			"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
			"DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
		return names;
	}

	const std::array<uint8_t, Engine::ParamCount>& Engine::paramDefaults()
	{
		static const std::array<uint8_t, ParamCount> defaults{
			64, 64, 64, 64, 64, 64, 64, 64,
			0, 0, 64, 64, 0, 127, 0, 0,
			0, 100, 64, 0, 0, 64, 0, 0};
		return defaults;
	}

	uint8_t Engine::defaultMachine(const int _track)
	{
		// TRX's ids run from 16 in this order (mdLib/mdmachines.cpp); EFM-BD and -SD are 32 and 33.
		return static_cast<uint8_t>(_track < 14 ? 16 + _track : 32 + (_track - 14));
	}

	std::vector<uint8_t> Engine::findFlashImage(const std::vector<std::filesystem::path>& _folders)
	{
		if(const auto path = fromEnvironment("GEARMULATOR_MD_FIRMWARE_BIN"); !path.empty())
		{
			if(auto flash = readFlash(path); !flash.empty())
				return flash;
		}
		auto folders = _folders;
		if(const auto local = fromEnvironment("LOCALAPPDATA"); !local.empty())
			folders.push_back(local / "Programs" / "Gearmulator-Elektron");
		if(const auto home = fromEnvironment("USERPROFILE"); !home.empty())
			folders.push_back(home / "Documents" / "Gearmulator Preview" / "Machinedrum");
		if(const auto home = fromEnvironment("HOME"); !home.empty())
			folders.push_back(home / "Documents" / "Gearmulator Preview" / "Machinedrum");
		for(const auto& folder : folders)
		{
			if(auto flash = readFlash(folder / g_flashName); !flash.empty())
				return flash;
		}
		return {};
	}

	Engine::Engine(const std::vector<uint8_t>& _flashImage)
		: m_state(std::make_unique<State>(md::fw::loadFirmwareFromFlash(_flashImage)))
	{
		m_state->engine.loadRomBank(md::fw::loadRomBankFromFlash(_flashImage));
		auto& host = m_state->engine.host();
		for(int track = 0; track < TrackCount; ++track)
		{
			host.setMachine(track, defaultMachine(track));
			for(int param = 8; param < ParamCount; ++param)
				host.setParam(track, param, paramDefaults()[param]);
			host.setLevel(track, DefaultLevel);
		}
	}

	Engine::~Engine() = default;

	const std::vector<md::engine::MachineInfo>& Engine::machines() const
	{
		return m_state->engine.os().machines();
	}

	void Engine::setMachine(const int _track, const uint8_t _machineId)
	{
		m_state->engine.host().setMachine(_track, _machineId);
	}

	void Engine::setParam(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().setParam(_track, _param, _value);
	}

	void Engine::setLevel(const int _track, const int _level)
	{
		m_state->engine.host().setLevel(_track, _level);
	}

	void Engine::setMute(const int _track, const bool _mute)
	{
		m_state->engine.host().setMute(_track, _mute);
	}

	void Engine::setLink(const int _track, const int _target)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setLink(_track, _target);
	}

	void Engine::setChoke(const int _track, const int _target)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setChoke(_track, _target);
	}

	void Engine::setLfo(const int _track, const md::LfoSettings& _lfo)
	{
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().setLfo(_track, _lfo.track, _lfo.parameter, _lfo.shape1, _lfo.shape2, _lfo.update);
	}

	void Engine::loadLfo(const int _track, const uint8_t* _block)
	{
		static_assert(LfoBytes == md::engine::HostModel<>::kLfoBytes);
		if(_track >= 0 && _track < TrackCount)
			m_state->engine.host().loadLfo(_track, _block);
	}

	void Engine::setTempo(const double _bpm)
	{
		m_state->engine.host().setTempo(_bpm);
	}

	void Engine::trigger(const int _track, const int _velocity)
	{
		m_state->engine.host().trigger(_track, std::clamp(_velocity, 1, 127));
	}

	void Engine::setSeparateOutputs(const uint32_t _tracks)
	{
		m_state->engine.dryMute = _tracks & ((1u << TrackCount) - 1);
	}

	void Engine::render(float* _left, float* _right, const size_t _count)
	{
		std::array<float*, OutputCount> outputs{};
		outputs[0] = _left;
		outputs[1] = _right;
		render(outputs.data(), _count);
	}

	void Engine::render(float* const* _outputs, const size_t _count)
	{
		auto& state = *m_state;
		constexpr float scale = 1.0f / 8388608.0f;
		size_t done = 0;
		while(done < _count)
		{
			if(state.used == BlockSize)
			{
				if(!state.engine.render(state.out))
					throw std::runtime_error("Machinedrum engine fault: " + state.engine.fault());
				state.used = 0;
			}
			const auto count = std::min(_count - done, BlockSize - state.used);
			for(int channel = 0; channel < 2; ++channel)
			{
				if(auto* output = _outputs[channel])
					for(size_t i = 0; i < count; ++i)
						output[done + i] = static_cast<float>(state.out.mix.main[state.used + i][channel]) * scale;
			}
			for(int track = 0; track < TrackCount; ++track)
			{
				auto* output = _outputs[2 + track];
				if(!output)
					continue;
				const auto vol = state.engine.host().mixerInput(track).mix[1];
				const auto& samples = state.out.tracks[track];
				for(size_t i = 0; i < count; ++i)
					output[done + i] = static_cast<float>(md::engine::Mixer::solo(samples[state.used + i], vol)) * scale;
			}
			state.used += count;
			done += count;
		}
	}
}
