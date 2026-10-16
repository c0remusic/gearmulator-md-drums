#include "mdDrumsEngine.h"

#include "Dsp56.h"
#include "MdEngine.h"
#include "Firmware.h"

#include "mdProtocol/mdkit.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace mdDrums
{
	namespace
	{
		constexpr size_t g_flashSize = 8 * 1024 * 1024;

		std::vector<uint8_t> readFlash(const std::filesystem::path& _path)
		{
			std::error_code error;
			if(!std::filesystem::is_regular_file(_path, error) || std::filesystem::file_size(_path, error) != g_flashSize)
				return {};
			std::ifstream file(_path, std::ios::binary);
			std::vector<uint8_t> flash(g_flashSize);
			if(!file.read(reinterpret_cast<char*>(flash.data()), static_cast<std::streamsize>(flash.size())))
				return {};
			return flash;
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
	// note, plus what its machine's voice is quicker than the latest (ticket 36: voiceStart, AlignedStart), so that
	// every voice sounds as late as TRX-BD's. The contributions are the products the mixer DSP sums (Mixer::gains): the
	// level a Hit sets, PAN and the sends move with the Hit, and the sum wraps as the DSP's does, so equal delays give
	// the engine's own mix, shifted. A Track's own output waits MasterDelay more, as Main does after the master
	// effects.
	struct Engine::State
	{
		State(const md::fw::FlashOs& _os, const bool _master) : engine(_os.firmware, _os.osImage)
		{
			engine.mixOn = false;
			if(_master)
				engine.enableMaster(_os.firmware);
			pending.fill(-1);
		}

		// A block, plus the longest delay: the rest of a block, a voice held back to AlignedStart, MasterDelay on a
		// Track's own output
		static constexpr int Ring = 128;
		static constexpr int Mask = Ring - 1;
		static_assert(BlockSize + SampleAccurateDelay + AlignedStart + MasterDelay <= Ring);
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
		std::array<Lane, TrackCount> lanes{};
		std::array<std::array<uint32_t, 5>, TrackCount> lastWords{};	// each Track's mixer words in the last block

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

		std::array<bool, TrackCount> struck{};
		for(int t = 0; t < TrackCount; ++t)
		{
			struck[t] = pending[t] >= 0;
			if(!struck[t])
				continue;
			delay[t] = pending[t];
			pending[t] = -1;
		}

		// A Choke that silenced a Track in this block (ticket 38): the Track stops exactly where the Hit that cut it
		// sounds. What it still had waiting past that point goes; when it waits less than that Hit, it plays on with
		// its last block's words up to it, unless the Hit struck it too. A lower Track, which the engine silences at its
		// next tick, keeps sounding until then at its own delay.
		std::array<int, TrackCount> extend{};
		for(int t = 0; t < TrackCount && sampleAccurate; ++t)
		{
			const auto by = host.cutBy(t);
			if(by >= TrackCount)
				continue;
			// Up to the furthest a block written before this one reaches (its own output MasterDelay further)
			for(auto time = start + delay[by]; time < start + SampleAccurateDelay + AlignedStart; ++time)
			{
				lanes[t].mix[time & Mask] = {};
				lanes[t].solo[(time + MasterDelay) & Mask] = 0;
			}
			if(!struck[t])
				extend[t] = std::max(0, delay[by] - delay[t]);
		}

		for(int t = 0; t < TrackCount; ++t)
		{
			const auto words = host.mixerInput(t).mix;
			const auto last = lastWords[t];
			lastWords[t] = words;
			const auto& samples = out.tracks[t];
			if(std::all_of(samples.begin(), samples.end(), [](const int32_t _s) { return _s == 0; }))
				continue;
			auto& lane = lanes[t];
			// Samples _from to _to of the block with the mixer's words _words
			const auto mix = [&](const std::array<uint32_t, 5>& _words, const int _from, const int _to)
			{
				std::array<int32_t, Channels> gains{};
				const bool main = engine.mixer().gains(_words, (engine.dryMute >> t) & 1, gains);
				const auto vol = sx24(_words[1]);
				for(int i = _from; i < _to; ++i)
				{
					const auto slot = (start + i + delay[t]) & Mask;
					if(main)
						for(int c = 0; c < Channels; ++c)
							lane.mix[slot][c] = add(lane.mix[slot][c], mpy(gains[c], samples[i]));
					// Mixer::solo, before its limiter, as late as Main
					auto& solo = lane.solo[(slot + MasterDelay) & Mask];
					solo = add(solo, asl(mpy(samples[i], vol), 4));
				}
			};
			mix(last, 0, extend[t]);
			mix(words, extend[t], BlockSize);
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
		if(!engine.masterOn())
			main = dry;
		else if(!engine.processMaster(dry, rev, del, main))
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
			if(auto flash = readFlash(folder / FlashImageName); !flash.empty())
				return flash;
		}
		return {};
	}

	std::vector<uint8_t> Engine::readFlashImage(const std::filesystem::path& _file, std::string& _error)
	{
		auto flash = readFlash(_file);
		if(flash.empty())
		{
			_error = "That file is not an 8 MB flash image";
			return {};
		}
		try
		{
			(void)md::fw::loadFirmwareFromFlash(flash);
		}
		catch(const std::exception&)
		{
			_error = "No Machinedrum OS in that file";
			return {};
		}
		return flash;
	}

	Engine::Engine(const std::vector<uint8_t>& _flashImage)
		: Engine(md::fw::loadFirmwareFromFlash(_flashImage), md::fw::loadRomBankFromFlash(_flashImage), true)
	{
		// The machines' voice code and the master's compiled before the audio thread plays them (EngineT::warmUpVoices,
		// MasterEngine::warmUp)
		if(m_state->engine.warmUpVoices() < 0)
			throw std::runtime_error("Machinedrum voice DSP fault while compiling the machines");
		if(auto* master = m_state->engine.masterEngine(); master && !master->warmUp())
			throw std::runtime_error("Machinedrum mixer DSP fault while compiling the master effects");
	}

	Engine::Engine(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank, const bool _master)
		: m_state(std::make_unique<State>(_os, _master))
	{
		m_state->engine.loadRomBank(_bank);
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

	const md::engine::MachineInfo* Engine::machine(const uint8_t _id) const
	{
		return m_state->engine.os().machine(_id);
	}

	const md::engine::MachineInfo* Engine::findMachine(const std::vector<md::engine::MachineInfo>& _machines,
		const uint8_t _id)
	{
		const auto it = std::find_if(_machines.begin(), _machines.end(),
			[&](const md::engine::MachineInfo& _m) { return _m.id == _id; });
		return it == _machines.end() ? nullptr : &*it;
	}

	std::array<uint16_t, Engine::ParamCount> Engine::played(const int _track) const
	{
		std::array<uint16_t, ParamCount> words{};
		if(_track < 0 || _track >= TrackCount)
			return words;
		const auto* voice = m_state->engine.host().voiceParams(_track);
		std::copy_n(voice, ParamCount, words.begin());
		return words;
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
		const auto struck = state.engine.host().trigger(_track, std::clamp(_velocity, 1, 127));
		if(!state.sampleAccurate)
			return struck;

		// The Hit lands BlockSize - used samples from now: each Track it struck, its own and its Link's, waits the rest
		// of the 31, then what its machine's voice is quicker than the latest
		const auto delay = static_cast<int>(state.used) - 1;
		for(int t = 0; t < TrackCount; ++t)
			if(struck & (1u << t))
				state.pending[t] = delay + std::max(0, AlignedStart - voiceStart(state.engine.host().machine(t)));
		return struck;
	}

	int Engine::voiceStart(const uint8_t _machine)
	{
		// Measured on each machine's own output, a Hit again at velocity 127 with the Kit's defaults (mdDrumsEngineTest's
		// "voice starts"). The voices that start a block late are held back the least: TRX-BD and TRX-S2, and the ROM
		// machines; most voices start 4 to 8 samples after their block, which their own sound sets (a noise, an
		// attack), and count as 4. TRX-XT (40), TRX-XC (68) and TRX-CB (102) start later than TRX-BD, which sets
		// AlignedStart: they are not held back and sound that much later, as on the Machinedrum. A machine's first Hit
		// after it is chosen can start later still (P-I ML 260 samples, TRX-MA 27), its sound again.
		switch(_machine)
		{
		case 16:	// TRX-BD
		case 29:	// TRX-S2
			return AlignedStart;
		case 18:	// TRX-XT
			return 40;
		case 21:	// TRX-CB
			return 102;
		case 27:	// TRX-XC
			return 68;
		default:
			// ROM-01 to -32, then ROM-33 to -48
			return (_machine >= 128 && _machine < 160) || (_machine >= 176 && _machine < 192) ? 12 : CommonVoiceStart;
		}
	}

	void Engine::setSampleAccurate(const bool _on)
	{
		auto& state = *m_state;
		state.sampleAccurate = _on;
		if(_on)
			return;
		state.delay.fill(0);
		state.pending.fill(-1);
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

	void applyKit(Engine& _engine, const md::automation::sysex::MdKit& _kit)
	{
		using md::automation::sysex::MdKit;
		static_assert(MdKit::ParameterCount == Engine::ParamCount && MdKit::LfoSize == Engine::LfoBytes
			&& MdKit::MasterEffectsSize == Engine::MasterCount);
		for(uint8_t track = 0; track < Engine::TrackCount; ++track)
		{
			_engine.setMachine(track, _kit.machine(track));
			for(uint8_t parameter = 0; parameter < MdKit::ParameterCount; ++parameter)
				_engine.setParam(track, parameter, _kit.parameters[track][parameter]);
			_engine.setLevel(track, _kit.levels[track]);
			_engine.loadLfo(track, _kit.lfos[track].data());
			// MdKit::Off is past the Tracks, which switches a Link or a Choke off
			_engine.setLink(track, _kit.links[track]);
			_engine.setChoke(track, _kit.chokes[track]);
		}
		// The dump keeps the master effects in the engine's order
		for(int index = 0; index < Engine::MasterCount; ++index)
			_engine.setMaster(index, _kit.masterEffects[static_cast<size_t>(index)]);
	}
}
