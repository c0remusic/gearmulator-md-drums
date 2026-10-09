#include "mdDrumsEngine.h"

#include "Dsp56.h"
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

	// Sample-accurate Hits (ticket 19 of the editor map). The voice DSP starts a Hit at its next 32-sample block, 0 to 31
	// samples after the note. Each Track's contributions to the mix (main left and right, its REV and DEL sends, its
	// own output) go through a delay of 31 less that wait, set by its Hit, so that every Hit sounds 31 samples after its
	// note, its own voice's start aside. The contributions are the products the mixer DSP sums (Mixer::gains): the
	// level a Hit sets, PAN and the sends move with the Hit, and the sum wraps as the DSP's does, so equal delays give
	// the engine's own mix, shifted.
	struct Engine::State
	{
		explicit State(md::fw::FlashOs _os) : engine(_os.firmware, std::move(_os.osImage))
		{
			engine.mixOn = false;
			engine.enableMaster(_os.firmware);
			pending.fill(-1);
			chokeDelay.fill(-1);
		}

		static constexpr int Ring = 64;	// a block, plus the longest delay
		static constexpr int Mask = Ring - 1;
		static constexpr int Channels = 6;	// main L R, REV L R, DEL L R

		struct Lane
		{
			std::array<std::array<md::engine::d56::Acc, Channels>, Ring> mix{};
			std::array<md::engine::d56::Acc, Ring> solo{};	// the Track's own output, before the limiter
		};

		md::engine::Engine engine;
		md::engine::Engine::Output out;
		size_t used = BlockSize;	// samples of the output block already handed out

		bool sampleAccurate = true;
		int64_t blockStart = 0;	// the engine's time at the next block it renders
		std::array<int, TrackCount> delay{};
		std::array<int, TrackCount> pending{};		// the delay a Hit set, from the block it lands in; -1 none
		std::array<int, TrackCount> chokeDelay{};	// the delay of the Hit that chokes the Track; -1 none
		std::array<bool, TrackCount> wasChoked{};
		std::array<Lane, TrackCount> lanes{};

		// The output block: the master effects' inputs (the dry main mix and the two sends), Main after them, each Track's
		// own output
		md::engine::Mixer::Stereo dry{};
		md::engine::Mixer::Stereo rev{};
		md::engine::Mixer::Stereo del{};
		md::engine::Mixer::Stereo main{};
		std::array<std::array<int32_t, BlockSize>, TrackCount> solo{};

		void renderBlock();
	};

	void Engine::State::renderBlock()
	{
		using namespace md::engine::d56;
		if(!engine.render(out))
			throw std::runtime_error("Machinedrum engine fault: " + engine.fault());
		const auto start = blockStart;
		blockStart += BlockSize;
		auto& host = engine.host();

		for(int t = 0; t < TrackCount; ++t)
		{
			if(pending[t] >= 0)
			{
				delay[t] = pending[t];
				pending[t] = -1;
			}
			// A Choke the engine has just applied: the tail the Track would still play once the choking Hit sounds goes
			const auto choked = host.choked(t);
			if(choked && !wasChoked[t] && chokeDelay[t] >= 0)
			{
				for(auto time = start + chokeDelay[t]; time < start + delay[t]; ++time)
				{
					lanes[t].mix[time & Mask] = {};
					lanes[t].solo[time & Mask] = 0;
				}
				chokeDelay[t] = -1;
			}
			wasChoked[t] = choked;
		}

		for(int t = 0; t < TrackCount; ++t)
		{
			const auto& samples = out.tracks[t];
			if(std::all_of(samples.begin(), samples.end(), [](const int32_t _s) { return _s == 0; }))
				continue;
			const auto& words = host.mixerInput(t).mix;
			std::array<int32_t, Channels> gains{};
			const bool main = engine.mixer().gains(words, (engine.dryMute >> t) & 1, gains);
			const auto vol = sx24(words[1]);
			auto& lane = lanes[t];
			for(int i = 0; i < BlockSize; ++i)
			{
				const auto slot = (start + i + delay[t]) & Mask;
				if(main)
					for(int c = 0; c < Channels; ++c)
						lane.mix[slot][c] = add(lane.mix[slot][c], mpy(gains[c], samples[i]));
				// Mixer::solo, before its limiter
				lane.solo[slot] = add(lane.solo[slot], asl(mpy(samples[i], vol), 4));
			}
		}

		for(int i = 0; i < BlockSize; ++i)
		{
			const auto slot = (start + i) & Mask;
			std::array<Acc, Channels> sum{};
			for(int t = 0; t < TrackCount; ++t)
			{
				auto& lane = lanes[t];
				for(int c = 0; c < Channels; ++c)
					sum[c] = add(sum[c], lane.mix[slot][c]);
				lane.mix[slot] = {};
				solo[t][i] = lim(lane.solo[slot]);
				lane.solo[slot] = 0;
			}
			dry[i] = {md::engine::Mixer::mainSample(sum[0]), md::engine::Mixer::mainSample(sum[1])};
			rev[i] = {md::engine::Mixer::mainSample(sum[2]), md::engine::Mixer::mainSample(sum[3])};
			del[i] = {md::engine::Mixer::mainSample(sum[4]), md::engine::Mixer::mainSample(sum[5])};
		}
		if(!engine.processMaster(dry, rev, del, main))
			throw std::runtime_error("Machinedrum master effects fault: " + engine.fault());
	}

	const std::array<uint8_t, Engine::MasterCount>& Engine::masterDefaults()
	{
		// Gate Box DVOL PRED DEC DAMP HP LP GATE LEV, Rhythm Echo TIME MOD MFRQ FB FLTF FLTW MONO LEV, EQ LF LG HF HG PF PG
		// PQ GAIN, Dynamix ATCK REL TRHD RTIO KNEE HP OUTG MIX
		static const std::array<uint8_t, MasterCount> defaults{
			0, 0, 68, 50, 1, 82, 71, 109,
			16, 0, 32, 27, 0, 44, 0, 71,
			64, 64, 64, 64, 64, 64, 64, 127,
			127, 127, 127, 127, 127, 127, 0, 0};
		return defaults;
	}

	void Engine::setMaster(const int _index, const int _value)
	{
		m_state->engine.host().setMaster(_index, _value);
	}

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
		for(int index = 0; index < MasterCount; ++index)
			host.setMaster(index, masterDefaults()[index]);
	}

	Engine::~Engine() = default;

	static_assert(Engine::MasterDelay == md::engine::MasterEngine::MainDelay);
	static_assert(Engine::MasterCount == md::engine::HostModel<>::kMasterParams);

	const std::vector<md::engine::MachineInfo>& Engine::machines() const
	{
		return m_state->engine.os().machines();
	}

	std::shared_ptr<const md::engine::TrackFx::Tables> Engine::fxTables() const
	{
		return m_state->engine.tables();
	}

	void Engine::setMachine(const int _track, const uint8_t _machineId)
	{
		m_state->engine.host().setMachine(_track, _machineId);
	}

	void Engine::setParam(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().setParam(_track, _param, _value);
	}

	void Engine::lock(const int _track, const int _param, const int _value)
	{
		m_state->engine.host().lock(_track, _param, _value);
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

	uint32_t Engine::trigger(const int _track, const int _velocity)
	{
		if(_track < 0 || _track >= TrackCount)
			return 0;
		auto& state = *m_state;
		auto& host = state.engine.host();
		host.trigger(_track, std::clamp(_velocity, 1, 127));
		if(host.muted(_track))
			return 0;
		const auto link = host.link(_track);
		const bool linked = link < TrackCount && link != _track && !host.muted(link);
		const uint32_t struck = (1u << _track) | (linked ? 1u << link : 0u);
		if(!state.sampleAccurate)
			return struck;

		// The Hit lands BlockSize - used samples from now: its Track waits the rest of the 31, and so does its Link,
		// which the engine hits in the same block. Its Choke's target is cut as late after the choke as this Hit sounds.
		const auto delay = static_cast<int>(state.used) - 1;
		state.pending[_track] = delay;
		if(linked)
			state.pending[link] = delay;
		if(const auto choke = host.choke(_track); choke < TrackCount && choke != _track)
			state.chokeDelay[choke] = delay;
		return struck;
	}

	void Engine::setSampleAccurate(const bool _on)
	{
		auto& state = *m_state;
		state.sampleAccurate = _on;
		if(_on)
			return;
		state.delay.fill(0);
		state.pending.fill(-1);
		state.chokeDelay.fill(-1);
	}

	bool Engine::isSampleAccurate() const
	{
		return m_state->sampleAccurate;
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
				state.renderBlock();
				state.used = 0;
			}
			const auto count = std::min(_count - done, BlockSize - state.used);
			for(int channel = 0; channel < 2; ++channel)
			{
				if(auto* output = _outputs[channel])
					for(size_t i = 0; i < count; ++i)
						output[done + i] = static_cast<float>(state.main[state.used + i][channel]) * scale;
			}
			for(int track = 0; track < TrackCount; ++track)
			{
				auto* output = _outputs[2 + track];
				if(!output)
					continue;
				const auto& samples = state.solo[track];
				for(size_t i = 0; i < count; ++i)
					output[done + i] = static_cast<float>(samples[state.used + i]) * scale;
			}
			state.used += count;
			done += count;
		}
	}
}
