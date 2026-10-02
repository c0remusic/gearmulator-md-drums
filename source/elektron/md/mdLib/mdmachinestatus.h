#pragma once

#include <atomic>
#include <cstdint>

#include "mdsysextransfer.h"

namespace md
{
	// What the plug-in's other threads need to know of a Device without taking the device lock, which
	// pauses the machine's rendering. The Device writes it on its rendering thread after every block,
	// and at the end of every access made with the rendering paused (Device::resumeRendering); any
	// thread reads it. The fields are read one by one, not as a transaction: whoever acts on one checks
	// it again under the lock.
	class MachineStatus
	{
	public:
		struct Values
		{
			// Takes MIDI and SysEx: DSPs booted, panel handshake done, MIDI receive ready, no project
			// state being restored (Hardware::isFirmwareMidiReady)
			bool firmwareReady = false;
			// A project state is being prepared, initialized or finalized
			bool restorePending = false;
			bool restoreFailed = false;
			// A project state finished its first-run initialization: Device::takeFinishedDeferredState
			bool deferredStateReady = false;
			// Machinedrum first run: the factory sample flash is still to be learned, then rebooted into
			bool factoryInitializationExpected = false;
			bool factoryReadyForReboot = false;
			bool parallelTransportActive = false;
			bool ramRecordingModeSupported = false;
			// The user's SysEx file import (Device::userSysexImportProgress): Idle when none was started
			MidiSysexTransferState userSysexState = MidiSysexTransferState::Idle;
			// The sequencer (Hardware::readSequencerPosition): whether it plays and its step, 0 for the
			// pattern's first; NoStep for a firmware whose addresses are not known
			static constexpr uint8_t NoStep = 0xff;
			bool sequencerPlaying = false;
			uint8_t sequencerStep = NoStep;
			uint64_t hardwareEpoch = 0;			// Device::hardwareEpoch: one more for every machine committed
			uint64_t restoreGeneration = 0;		// the project state restore the flags above refer to
		};

		Values read() const
		{
			const auto flags = m_flags.load(std::memory_order_acquire);
			Values values;
			values.firmwareReady = (flags & FirmwareReady) != 0;
			values.restorePending = (flags & RestorePending) != 0;
			values.restoreFailed = (flags & RestoreFailed) != 0;
			values.deferredStateReady = (flags & DeferredStateReady) != 0;
			values.factoryInitializationExpected = (flags & FactoryInitializationExpected) != 0;
			values.factoryReadyForReboot = (flags & FactoryReadyForReboot) != 0;
			values.parallelTransportActive = (flags & ParallelTransportActive) != 0;
			values.ramRecordingModeSupported = (flags & RamRecordingModeSupported) != 0;
			values.userSysexState = static_cast<MidiSysexTransferState>((flags >> SysexStateShift) & 0xff);
			values.sequencerStep = static_cast<uint8_t>((flags >> SequencerStepShift) & 0xff);
			values.sequencerPlaying = (flags & SequencerPlaying) != 0;
			values.hardwareEpoch = m_hardwareEpoch.load(std::memory_order_acquire);
			values.restoreGeneration = m_restoreGeneration.load(std::memory_order_acquire);
			return values;
		}

		uint64_t hardwareEpoch() const { return m_hardwareEpoch.load(std::memory_order_acquire); }

		// The Device only: on its rendering thread, or with the rendering paused.
		void publish(const Values& _values)
		{
			const uint32_t flags = (_values.firmwareReady ? FirmwareReady : 0u)
				| (_values.restorePending ? RestorePending : 0u)
				| (_values.restoreFailed ? RestoreFailed : 0u)
				| (_values.deferredStateReady ? DeferredStateReady : 0u)
				| (_values.factoryInitializationExpected ? FactoryInitializationExpected : 0u)
				| (_values.factoryReadyForReboot ? FactoryReadyForReboot : 0u)
				| (_values.parallelTransportActive ? ParallelTransportActive : 0u)
				| (_values.ramRecordingModeSupported ? RamRecordingModeSupported : 0u)
				| (static_cast<uint32_t>(_values.userSysexState) << SysexStateShift)
				| (static_cast<uint32_t>(_values.sequencerStep) << SequencerStepShift)
				| (_values.sequencerPlaying ? SequencerPlaying : 0u);
			// The counters first: a reader that sees the flags of a new machine also sees its epoch
			if(m_hardwareEpoch.load(std::memory_order_relaxed) != _values.hardwareEpoch)
				m_hardwareEpoch.store(_values.hardwareEpoch, std::memory_order_release);
			if(m_restoreGeneration.load(std::memory_order_relaxed) != _values.restoreGeneration)
				m_restoreGeneration.store(_values.restoreGeneration, std::memory_order_release);
			if(m_flags.load(std::memory_order_relaxed) != flags)
				m_flags.store(flags, std::memory_order_release);
		}

	private:
		enum Flag : uint32_t
		{
			FirmwareReady = 1u << 0,
			RestorePending = 1u << 1,
			RestoreFailed = 1u << 2,
			DeferredStateReady = 1u << 3,
			FactoryInitializationExpected = 1u << 4,
			FactoryReadyForReboot = 1u << 5,
			ParallelTransportActive = 1u << 6,
			RamRecordingModeSupported = 1u << 7,
			SequencerPlaying = 1u << 24
		};
		static constexpr uint32_t SysexStateShift = 8;	// bits 8 to 15: userSysexState
		static constexpr uint32_t SequencerStepShift = 16;	// bits 16 to 23: sequencerStep

		std::atomic<uint32_t> m_flags{0};
		std::atomic<uint64_t> m_hardwareEpoch{0};
		std::atomic<uint64_t> m_restoreGeneration{0};
	};
}
