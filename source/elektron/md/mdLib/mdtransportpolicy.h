#pragma once

#include "mdtypes.h"

#include <cstddef>
#include <cstdint>

namespace md
{
	// Scheduling and host-port limits used to interleave the ColdFire and two
	// DSPs on one host thread. These values preserve the current runtime
	// behavior; keeping them together makes model differences and units explicit.
	struct TransportPolicy
	{
		double backgroundQuantumMicroseconds;
		uint64_t catchUpMaxDspCycles;
		size_t hostReceiveIrqMinWords;
		size_t hostReceiveQueueCapacityWords;
		size_t hostTransmitBackpressureThresholdWords;
		uint64_t hostTransmitBackpressureReleaseUcCycles;
		bool exactEssiCycleDeadlines;
		// Content offset of the producer->mixer link direction, in codec
		// frames (parallel-transport spec §2: the ratified 1-2 frame internal
		// latency budget). The mixer->producer back-channel stays causal.
		// 0 disables dating entirely (the documented fallback).
		// MD_LINK_PIPELINE_DEPTH overrides for experiments.
		double linkPipelineDepthFrames;
		// Pair mode: how far the DSP worker may run ahead of the UC. A host
		// word lands that much late in DSP time, plus up to one worker chunk.
		// MD_PAIR_LEAD_US overrides for experiments.
		double pairDspLeadMicroseconds;
	};

	namespace detail
	{
		// Monomachine pair lead: an idle worker sits at its lead, so every host
		// word landed that late. At one quantum (30 us) the GND SIN gates
		// failed now and then (silent note, clicks), at 45 and 60 us always;
		// at 10 us the throughput is the same and 0 costs about 25 points.
		inline constexpr TransportPolicy g_monomachinePolicy{30.0, 100'000, 1, 16, 4, 200'000, true, 1.0, 10.0};
		inline constexpr TransportPolicy g_machinedrumPolicy{125.0, 100'000, 3, 16, 4, 200'000, false, 1.0, 125.0};
	}

	// Scheduler hot paths ask for this per step: hand out the constant, not a copy
	constexpr const TransportPolicy& transportPolicy(const MachineModel _model)
	{
		return _model == MachineModel::Monomachine ? detail::g_monomachinePolicy : detail::g_machinedrumPolicy;
	}
}
