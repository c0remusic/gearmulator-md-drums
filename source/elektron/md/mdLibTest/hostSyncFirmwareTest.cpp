#include "sysexPanelDriver.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdhostsync.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

// Drives md::HostSync against the real firmware. Following the host must make
// START and MIDI clock play the machine, and make it stop when the clock stops
// or STOP arrives; releasing it must bring the factory setting back. On the
// Monomachine this runs its menu macro, also from menus left elsewhere.
namespace
{
	using namespace md::test;
	namespace sysex = md::automation::sysex;
	using Target = md::HostSync::Target;
	using State = md::HostSync::State;

	class Machine
	{
	public:
		Machine(const md::MachineModel _model, std::vector<uint8_t> _rom, const char* _path)
			: m_model(_model), m_sync(_model)
		{
			synthLib::DeviceCreateParams params;
			params.romData = std::move(_rom);
			params.romName = _path;
			params.customData = md::deviceCustomData(_model);
			m_device = std::make_unique<md::Device>(params);
			advanceFrames(hardware(), md::g_samplerate * 20);
			require(hardware().isFirmwareMidiReady(), "firmware boot incomplete");
			m_slot = globalSlot();
		}

		md::Hardware& hardware() { return m_device->getHardware(); }

		struct Activity
		{
			double peak = 0;			// audio peak of the last half second
			unsigned ledChanges = 0;	// front-panel LED changes in the last half second
			uint32_t changedBanks = 0;	// bit per LED bank that changed in the last half second
		};

		// Renders _seconds, sending MIDI clock at _bpm (none at 0), servicing the
		// host sync. A running sequencer moves its step LEDs; a halted one does not.
		Activity run(const double _seconds, const double _bpm = 0)
		{
			constexpr uint32_t block = 256;
			const double samplesPerTick = _bpm > 0 ? md::g_samplerate * 60.0 / (_bpm * 24.0) : 0.0;
			double nextTick = 0;
			Activity tail;
			auto leds = ledBanks();
			const auto frames = static_cast<uint32_t>(_seconds * md::g_samplerate);
			for(uint32_t done = 0; done < frames; done += block)
			{
				while(samplesPerTick > 0 && nextTick <= done)
				{
					realtime(0xf8);
					nextTick += samplesPerTick;
				}
				hardware().processAudio(block, 0);
				m_frames += block;
				const auto now = ledBanks();
				if(done + md::g_samplerate / 2 >= frames)
				{
					tail.peak = std::max(tail.peak, blockPeak(block));
					bool changed = false;
					for(size_t bank = 0; bank < now.size(); ++bank)
					{
						if(now[bank] == leds[bank] || !m_watchedBanks.count(bank))
							continue;
						tail.changedBanks |= 1u << bank;
						changed = true;
					}
					if(changed)
						++tail.ledChanges;
				}
				leds = now;
				serviceSync();
			}
			return tail;
		}

		void realtime(const uint8_t _status)
		{
			require(hardware().sendMidi({synthLib::MidiEventSource::Host, _status}), "realtime byte rejected");
		}

		// Runs until the host sync settles on _expected, or fails.
		void settle(const Target _target, const State _expected)
		{
			m_sync.setTarget(_target);
			for(unsigned i = 0; i < 60; ++i)
			{
				run(0.5);
				const auto state = m_sync.getState();
				if(state == State::Failed)
					break;
				if(state == _expected && (_target != Target::Release || m_sync.getTarget() == Target::Leave))
				{
					std::printf("hostSyncFirmwareTest: %s settled after %.1f s\n", name(), (i + 1) * 0.5);
					return;
				}
			}
			std::fprintf(stderr, "hostSyncFirmwareTest: %s state=%d target=%d\n", name(),
				static_cast<int>(m_sync.getState()), static_cast<int>(m_sync.getTarget()));
			throw std::runtime_error("host sync did not settle");
		}

		sysex::GlobalSync currentSync()
		{
			const auto sync = sysex::parseGlobalSync(m_model, globalDump());
			require(sync.has_value(), "Global sync unreadable");
			return *sync;
		}

		void observeGlobal()
		{
			m_sync.onGlobalDump(globalDump());
		}

		const char* name() const { return m_model == md::MachineModel::Monomachine ? "MM" : "MD"; }
		md::MachineModel model() const { return m_model; }

		// LED banks whose changes count as sequencer activity; all of them by default.
		void watchBanks(std::set<size_t> _banks) { m_watchedBanks = std::move(_banks); }

	private:
		std::array<uint8_t, md::FrontPanel::g_ledBankCount> ledBanks() const
		{
			const auto panel = m_device->getHardware().getFrontPanelSnapshot();
			std::array<uint8_t, md::FrontPanel::g_ledBankCount> banks{};
			for(size_t bank = 0; bank < banks.size(); ++bank)
				banks[bank] = panel.getLedBankRaw(static_cast<uint8_t>(md::FrontPanel::g_firstLedBank + bank));
			return banks;
		}

		std::set<size_t> m_watchedBanks = [] {
			std::set<size_t> all;
			for(size_t bank = 0; bank < md::FrontPanel::g_ledBankCount; ++bank)
				all.insert(bank);
			return all;
		}();

		double blockPeak(const uint32_t _frames)
		{
			double peak = 0;
			for(unsigned channel = 0; channel < 2; ++channel)
			{
				for(uint32_t frame = 0; frame < _frames; ++frame)
				{
					int32_t sample = hardware().getAudioOutputs()[channel][frame] & 0xffffff;
					if(sample & 0x800000)
						sample -= 0x1000000;
					peak = std::max(peak, std::abs(static_cast<double>(sample)));
				}
			}
			return peak;
		}

		void send(const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_bytes.begin(), _bytes.end());
			require(hardware().sendMidi(event), "SysEx rejected");
		}

		// Forwards Global dumps of the active slot, as the plug-in's controller does.
		void serviceSync()
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware().readMidiOut(events);
			for(const auto& event : events)
			{
				const sysex::Message bytes(event.sysex.begin(), event.sysex.end());
				const auto dump = sysex::parseGlobalDump(m_model, bytes);
				if(dump && dump->slot == m_slot)
					m_sync.onGlobalDump(bytes);
			}
			md::HostSync::Actions actions;
			actions.sendSysex = [this](const sysex::Message& _message) { send(_message); };
			actions.sendPanel = [this](const md::PanelPacket& _packet)
			{
				return hardware().trySendPanelEvent(_packet.row, _packet.mask);
			};
			actions.requestGlobal = [this] { send(sysex::globalRequest(m_model, m_slot)); };
			m_sync.service(m_frames, 1, actions);
		}

		template<typename Accept>
		sysex::Message query(const sysex::Message& _request, const Accept& _accept)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware().readMidiOut(events);
			send(_request);
			for(unsigned attempt = 0; attempt < 40; ++attempt)
			{
				advanceFrames(hardware(), md::g_samplerate / 10);
				m_frames += md::g_samplerate / 10;
				events.clear();
				hardware().readMidiOut(events);
				for(const auto& event : events)
				{
					const sysex::Message bytes(event.sysex.begin(), event.sysex.end());
					if(_accept(bytes))
						return bytes;
				}
			}
			throw std::runtime_error("no SysEx answer");
		}

		uint8_t globalSlot()
		{
			const auto response = query(sysex::statusRequest(m_model, sysex::StatusParameter::Global),
				[this](const sysex::Message& _bytes)
				{
					const auto status = sysex::parseStatusResponse(m_model, _bytes);
					return status && status->parameter == sysex::StatusParameter::Global;
				});
			return sysex::parseStatusResponse(m_model, response)->value;
		}

		sysex::Message globalDump()
		{
			return query(sysex::globalRequest(m_model, m_slot), [this](const sysex::Message& _bytes)
			{
				const auto dump = sysex::parseGlobalDump(m_model, _bytes);
				return dump && dump->slot == m_slot;
			});
		}

		const md::MachineModel m_model;
		std::unique_ptr<md::Device> m_device;
		md::HostSync m_sync;
		uint64_t m_frames = 0;
		uint8_t m_slot = 0;
	};

	// START and clock run the sequencer; it halts without clock and after STOP.
	void requireFollowing(Machine& _machine)
	{
		_machine.realtime(0xfa);
		const auto playing = _machine.run(1.5, 180);
		const auto clockStopped = _machine.run(1.5);
		const auto resumed = _machine.run(1.0, 180);
		_machine.realtime(0xfc);
		const auto stopped = _machine.run(1.5, 180);
		std::printf("hostSyncFirmwareTest: %s following: LED changes playing=%u clockStopped=%u resumed=%u stopped=%u, "
			"banks %05x %05x %05x %05x, peaks %.0f %.0f %.0f %.0f\n", _machine.name(), playing.ledChanges,
			clockStopped.ledChanges, resumed.ledChanges, stopped.ledChanges, playing.changedBanks,
			clockStopped.changedBanks, resumed.changedBanks, stopped.changedBanks, playing.peak,
			clockStopped.peak, resumed.peak, stopped.peak);
		require(playing.peak > 100000 && playing.ledChanges >= 3 && resumed.ledChanges >= 3,
			"START and clock did not run the sequencer");
		require(clockStopped.ledChanges <= 1, "the sequencer kept running without clock");
		require(stopped.ledChanges <= 1, "the sequencer kept running after STOP");
	}

	// Leaves the Monomachine's menu cursors away from where the macro starts.
	void scatterMenus(Machine& _machine)
	{
		auto& hardware = _machine.hardware();
		using C = md::PanelControl;
		panelChord(hardware, C::Kit);
		panelTap(hardware, C::Enter);
		for(int i = 0; i < 3; ++i) panelTap(hardware, C::Down);	// MIDI SEQ
		panelTap(hardware, C::Up);								// FILE
		panelTap(hardware, C::Up);								// CONTROL
		panelTap(hardware, C::Right);
		for(int i = 0; i < 5; ++i) panelTap(hardware, C::Down);	// MECH SETTINGS
		panelTap(hardware, C::Up);
		panelTap(hardware, C::Up);								// CONTROL IN
		panelTap(hardware, C::Enter);
		panelTap(hardware, C::Down);
		panelTap(hardware, C::Down);							// PRG CHANGE
		exitMenus(hardware);
	}

	void run(const md::MachineModel _model, const char* _path)
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, _path), "could not read firmware");
		require(md::RomLoader::isRomForModel(rom, _model), "firmware fingerprint mismatch");
		Machine machine(_model, std::move(rom), _path);
		// The TEMPO LED beats with incoming clock even while stopped: it sits in
		// bank 0x23 on the Machinedrum and 0x26 on the Monomachine, whose bank 0x28
		// also blinks on its own.
		if(_model == md::MachineModel::Monomachine)
			machine.watchBanks({0, 1, 2, 3, 4, 5, 7});
		else
			machine.watchBanks({0, 1, 2, 4, 5});
		const auto factory = *md::HostSync::wantedSync(_model, Target::Release);
		require(machine.currentSync() == factory, "unexpected factory sync setting");

		machine.observeGlobal();
		machine.settle(Target::Follow, State::Following);
		require(machine.currentSync() == sysex::GlobalSync{true, true}, "Global does not follow the host");
		requireFollowing(machine);

		machine.settle(Target::Release, State::NotFollowing);
		require(machine.currentSync() == factory, "release did not restore the factory setting");

		if(_model == md::MachineModel::Monomachine)
			scatterMenus(machine);
		machine.settle(Target::Follow, State::Following);
		requireFollowing(machine);
		std::printf("hostSyncFirmwareTest: %s PASS\n", machine.name());
	}
}

int main()
{
	const auto* md = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const auto* mm = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if((!md || !*md) && (!mm || !*mm))
	{
		std::cout << "hostSyncFirmwareTest: SKIP (no firmware supplied)\n";
		return 77;
	}
	try
	{
		if(mm && *mm)
			run(md::MachineModel::Monomachine, mm);
		if(md && *md)
			run(md::MachineModel::Machinedrum, md);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::fprintf(stderr, "hostSyncFirmwareTest: FAIL %s\n", _error.what());
		return 1;
	}
}
