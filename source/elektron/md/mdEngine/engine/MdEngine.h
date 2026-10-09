// Machinedrum One's engine: the OS's own control code (HostModel + MachineRunner), the voice DSP (VoiceEngine,
// emulated) and the mixer DSP's per-track effects and mix as native C++ (TrackFx, Mixer). One 32-sample block
// at a time: all 16 tracks, the dry main mix, the reverb/delay sends and each track's own output.
#pragma once
#include <algorithm>
#include <chrono>
#include <array>
#include <memory>
#include <vector>

#include <type_traits>
#include <vector>
#include "HostModel.h"
#include "MachineRunner.h"
#include "MasterEngine.h"
#include "Mixer.h"
#include "ParallelVoiceEngine.h"
#include "TrackFx.h"
#include "VoiceEngine.h"

namespace md::engine
{
	// TVoices: VoiceEngine (default, one DSP2 instance) or ParallelVoiceEngine (splits the 16 voices across
	// threads - see its header and HANDOFF.md, "per-machine cost profiled"). Template so both share this same
	// code; defined inline here (not in Engine.cpp, which is now just an include of this header, kept so
	// existing build commands that name engine/Engine.cpp as a source file still work) so each consumer gets
	// its own instantiation without needing whole-class explicit instantiation, which would force every
	// TVoices to support every constructor overload below.
	template<class TVoices = VoiceEngine>
	class EngineT
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kBlock = 32;

		// _osImage: the OS file's section 0 (the ColdFire OS), decompressed. Constructs TVoices with just the
		// firmware (VoiceEngine's own constructor).
		EngineT(const fw::Firmware& _fw, std::vector<uint8_t> _osImage)
			: m_tables(std::make_shared<const TrackFx::Tables>(_fw)), m_mixer(*m_tables)
		{
			m_os = std::make_unique<MachineRunner>(std::move(_osImage));
			m_voices = std::make_unique<TVoices>(_fw);
			init();
		}

		// As above, but forwards _voicesArg to TVoices' constructor too (e.g. ParallelVoiceEngine's group
		// count). Only ever instantiated for a TVoices whose constructor actually takes (firmware, _voicesArg).
		template<class TVoicesArg>
		EngineT(const fw::Firmware& _fw, std::vector<uint8_t> _osImage, TVoicesArg _voicesArg)
			: m_tables(std::make_shared<const TrackFx::Tables>(_fw)), m_mixer(*m_tables)
		{
			m_os = std::make_unique<MachineRunner>(std::move(_osImage));
			m_voices = std::make_unique<TVoices>(_fw, _voicesArg);
			init();
		}

		~EngineT() = default;

		HostModel<TVoices>& host() { return *m_host; }
		const MachineRunner& os() const { return *m_os; }
		// md-drums: the OS's tables that TrackFx and the Mixer read, immutable, for whoever runs a TrackFx of its own
		const std::shared_ptr<const TrackFx::Tables>& tables() const { return m_tables; }
		TVoices& voices() { return *m_voices; }

		// md-drums: the UW sample bank the ROM machines play (fw::loadRomBankFromFlash). The OS's boot copies it from
		// the flash into the voice DSP; this voice DSP starts without it, and every ROM machine is silent until then.
		void loadRomBank(const fw::RomBank& _bank)
		{
			for(const auto& entry : _bank.directory)
				m_voices->writeP(entry.addr, entry.words, std::size(entry.words));
			m_voices->writeP(fw::RomBank::DataAddr, _bank.data.data(), _bank.data.size());
		}

		// md-drums: every audio machine's voice code compiled now rather than at its first Hit. The voice DSP's JIT compiles
		// a machine's code the first time a voice runs it: 1.5 to 8 ms in one 32-sample block (mdDrumsFirstHitTest), more
		// than an audio thread at 128 samples has. Each audio machine but RAM-R and RAM-P plays a Hit at its default
		// SYN1-8 for _blocks blocks on the voice DSP alone, its words computed by a copy of the OS; the DSP then gets its
		// memory and registers back (VoiceEngine::saveState), so that the engine sounds as if nothing had run. Call it
		// after loadRomBank, before the first render. Returns the machines run, or -1 if the voice DSP faulted.
		int warmUpVoices(const int _blocks = 4)
		{
			if constexpr(kParallel)
			{
				return 0;
			}
			else
			{
				const auto state = m_voices->saveState();
				const auto os = m_os->clone();
				std::vector<const MachineInfo*> machines;
				for(const auto& m : os->machines())
					if(!(m.id >= 0x60 && m.id <= 0x7b) && m.name.rfind("RAM", 0) != 0)	// MID/CTR have no audio
						machines.push_back(&m);
				typename TVoices::Block block;
				bool ok = true;
				for(size_t first = 0; first < machines.size() && ok; first += kTracks)
				{
					for(int b = 0; b < _blocks && ok; ++b)
					{
						for(size_t v = 0; v < static_cast<size_t>(kTracks) && first + v < machines.size(); ++v)
						{
							const auto& machine = *machines[first + v];
							std::array<uint16_t, 24> params{};
							params.fill(64 << 7);
							for(size_t p = 0; p < 8; ++p)
								params[p] = static_cast<uint16_t>(machine.defaults[p] << 7);
							uint32_t out[32];
							const int n = os->compute(machine.id, params.data(), out, 32, b == 0);
							if(n < 0)
								continue;
							out[0] = b == 0 ? static_cast<uint32_t>(machine.id) + 1 : 0;
							m_voices->setSlot(static_cast<int>(v), out, std::clamp(n, 1, static_cast<int>(TVoices::kSlotWords)));
						}
						ok = m_voices->renderBlock(block);
					}
				}
				m_voices->restoreState(*state);
				return ok ? static_cast<int>(machines.size()) : -1;
			}
		}

		struct Output
		{
			Mixer::Output mix;													// main, sends, individual outputs
			std::array<std::array<int32_t, kBlock>, kTracks> tracks{};			// each track after its effects
			Mixer::Stereo master{};		// md-drums: Main after the master effects (enableMaster), MasterEngine::MainDelay late
		};

		// md-drums: the master effects (MasterEngine), off until this is called (it allocates the mixer DSP, ~9 MB, and
		// runs its init). With the mixer on, render() then runs them on its buses into Output::master; without it, the
		// caller runs them on its own buses (processMaster). Either way the OS's parameter words of each tick reach them.
		void enableMaster(const fw::Firmware& _fw)
		{
			if(!m_master)
				m_master = std::make_unique<MasterEngine>(_fw);
		}
		bool masterOn() const { return m_master != nullptr; }
		MasterEngine* masterEngine() { return m_master.get(); }
		bool processMaster(const Mixer::Stereo& _main, const Mixer::Stereo& _rev, const Mixer::Stereo& _del, Mixer::Stereo& _out)
		{
			if(!m_master)
				return false;
			const auto tm0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			const bool ok = m_master->process(_main, _rev, _del, _out);
			if(timingOn)
				masterUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tm0).count();
			return ok;
		}

		// Optional stage timing (microseconds, accumulated): track effects, and the mix. The OS tick and the voice DSP
		// are HostModel's (host().tickUs / dspUs).
		uint32_t dryMute = 0;	// tracks kept out of the dry main mix (see Mixer::process); set by the caller between renders
		// md-drums: false leaves Output::mix untouched, for a caller that mixes Output::tracks itself (mixer())
		bool mixOn = true;
		const Mixer& mixer() const { return m_mixer; }
		bool timingOn = false;
		double fxUs = 0, mixUs = 0, masterUs = 0;

		static constexpr bool kParallel = std::is_same_v<TVoices, ParallelVoiceEngine>;

		// One track's effects: a settled silent track's chain gives the same again while its input stays silent and
		// its state (which holds its params) stays the same, so it is skipped. Exact - see HANDOFF.md, "fixed
		// per-block cost". Touches only track _t's state, so tracks can run on different threads (each with its own
		// TrackFx: its scratch buffers are per instance).
		void fxTrack(TrackFx& _fx, const int _t, const int32_t* _src, int32_t* _dst)
		{
			TrackFx::setParams(m_state[_t], m_host->mixerInput(_t).fx.data());
			const bool silentIn = std::all_of(_src, _src + kBlock, [](int32_t s) { return s == 0; });
			if(skipSettled && silentIn && m_settled[_t] && m_state[_t].y == m_settledState[_t].y)
			{
				std::fill(_dst, _dst + kBlock, 0);
				return;
			}
			const auto before = m_state[_t];
			_fx.process(m_state[_t], _src, _dst);
			m_settled[_t] = silentIn && m_state[_t].y == before.y && std::all_of(_dst, _dst + kBlock, [](int32_t s) { return s == 0; });
			if(m_settled[_t])
				m_settledState[_t] = m_state[_t];
		}

		bool render(Output& _out)
		{
			m_host->timingOn = timingOn;
			m_out = &_out;
			if(!m_host->renderBlock(m_voiceOut))
				return false;
			// md-drums: the master words the tick computed, before the master runs on this block (the OS sends them while
			// the voice DSP renders, so they land on the mixer DSP between two of its blocks here)
			if(m_master)
			{
				typename HostModel<TVoices>::MasterWords words;
				if(m_host->takeMasterWords(words))
					m_master->setWords(words.y, words.words.data(), words.count);
			}
			const auto tf0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			std::array<const int32_t*, kTracks> in{};
			std::array<std::array<uint32_t, 5>, kTracks> mix{};
			for(int t = 0; t < kTracks; ++t)
			{
				// Parallel: the groups' threads already ran their tracks' effects (see init()).
				if constexpr(!kParallel)
					fxTrack(*m_fxs[0], t, m_voiceOut[t].data(), _out.tracks[t].data());
				in[t] = _out.tracks[t].data();
				mix[t] = m_host->mixerInput(t).mix;
			}
			if(!mixOn)
				return true;
			if(!timingOn)
			{
				m_mixer.process(in.data(), mix.data(), _out.mix, dryMute);
				return !m_master || processMaster(_out.mix.main, _out.mix.rev, _out.mix.del, _out.master);
			}
			const auto tf1 = std::chrono::steady_clock::now();
			m_mixer.process(in.data(), mix.data(), _out.mix, dryMute);
			const auto tf2 = std::chrono::steady_clock::now();
			fxUs += std::chrono::duration<double, std::micro>(tf1 - tf0).count();
			mixUs += std::chrono::duration<double, std::micro>(tf2 - tf1).count();
			return !m_master || processMaster(_out.mix.main, _out.mix.rev, _out.mix.del, _out.master);
		}

		const std::string& fault() const
		{
			return m_master && !m_master->faultReason().empty() ? m_master->faultReason() : m_voices->faultReason();
		}

		bool skipSettled = true;	// false: run every track's effects every block (for verifying the skip)

	private:
		void init()
		{
			m_host = std::make_unique<HostModel<TVoices>>(*m_os, *m_voices);
			for(auto& s : m_state)
				TrackFx::init(s);
			m_fxs.push_back(std::make_unique<TrackFx>(*m_tables));
			if constexpr(kParallel)
			{
				// Each group's thread runs the effects of its own tracks right after its voices render.
				for(int g = 1; g < m_voices->groupCount(); ++g)
					m_fxs.push_back(std::make_unique<TrackFx>(*m_tables));
				m_voices->setPost([this](int _g, const typename TVoices::Block& _blk)
				{
					for(int t = 0; t < kTracks; ++t)
						if(m_voices->groupOf(t) == _g)
							fxTrack(*m_fxs[static_cast<size_t>(_g)], t, _blk[t].data(), m_out->tracks[t].data());
				});
			}
		}

		std::unique_ptr<MachineRunner> m_os;
		std::unique_ptr<TVoices> m_voices;
		std::unique_ptr<HostModel<TVoices>> m_host;
		std::shared_ptr<const TrackFx::Tables> m_tables;	// md-drums: shared, so that the editor measures with them (tables())
		std::vector<std::unique_ptr<TrackFx>> m_fxs;	// one per voice group (one when not parallel)
		Output* m_out = nullptr;
		Mixer m_mixer;
		std::unique_ptr<MasterEngine> m_master;	// md-drums: the master effects, once enabled
		std::array<TrackFx::State, kTracks> m_state{};
		std::array<TrackFx::State, kTracks> m_settledState{};
		std::array<bool, kTracks> m_settled{};
		typename TVoices::Block m_voiceOut{};
	};

	using Engine = EngineT<VoiceEngine>;
	// Constructed with (firmware, osImage, groupCount) - see ParallelVoiceEngine.h.
	using ParallelEngine = EngineT<ParallelVoiceEngine>;
}
