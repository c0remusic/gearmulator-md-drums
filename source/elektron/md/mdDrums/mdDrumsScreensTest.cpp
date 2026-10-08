// What the editor's Filter and EQ and LFO screens compute (ticket 22 of the editor map). The LFO's port (mdDrumsLfo.*)
// against the OS's own routines running in the engine, tick by tick and Hit by Hit, over every pair of shapes, every
// mode and both of the speed's formulas: the phase, both shapes, what the apply reads and what it writes, bit for bit.
// The filter and EQ response (mdDrumsFilterResponse.*): where its edges, its peak and its EQ fall, and what a measure
// costs.

#include "mdDrumsEngine.h"
#include "mdDrumsFilterResponse.h"
#include "mdDrumsLfo.h"

#include "Firmware.h"
#include "MdEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// OS 1.63's addresses (md::engine::HostModel)
	constexpr uint32_t g_lfo = 0x1000f8c;			// 16 LFO structs of $24 bytes
	constexpr uint32_t g_work = 0x1000a4c;			// the smoothed parameters: 16 Tracks of $30 bytes, a word each
	constexpr uint32_t g_voice = 0x10011cc;		// the same after the LFOs: what the voices get
	constexpr uint32_t g_tempo = 0x100150c;		// BPM x 24
	constexpr uint32_t g_trackStride = 0x30;
	constexpr int g_blocksPerTick = 11;

	uint16_t word(const md::engine::MachineRunner& _os, const uint32_t _array, const int _track, const int _param)
	{
		return _os.peek16(_array + g_trackStride * static_cast<uint32_t>(_track) + 2 * static_cast<uint32_t>(_param));
	}

	mdDrums::Lfo::State lfoState(const md::engine::MachineRunner& _os, const int _k)
	{
		const auto base = g_lfo + 0x24 * static_cast<uint32_t>(_k);
		mdDrums::Lfo::State s;
		s.trigger = _os.peek8(base + 5) != 0;
		s.eighth = {static_cast<int8_t>(_os.peek8(base + 6)), static_cast<int8_t>(_os.peek8(base + 7))};
		s.current = {static_cast<int32_t>(_os.peek32(base + 8)), static_cast<int32_t>(_os.peek32(base + 0xc))};
		s.held = {static_cast<int32_t>(_os.peek32(base + 0x10)), static_cast<int32_t>(_os.peek32(base + 0x14))};
		s.random = {static_cast<int32_t>(_os.peek32(base + 0x18)), static_cast<int32_t>(_os.peek32(base + 0x1c))};
		s.phase = static_cast<int32_t>(_os.peek32(base + 0x20));
		return s;
	}

	bool same(const mdDrums::Lfo::State& _a, const mdDrums::Lfo::State& _b)
	{
		return _a.phase == _b.phase && _a.current == _b.current && _a.held == _b.held && _a.random == _b.random
			&& _a.eighth == _b.eighth && _a.trigger == _b.trigger;
	}

	// Track 1's LFO on Track 2's FLTF in the engine, every pair of shapes in every mode, the speed on both sides of
	// $1fff, the depth, the mix and the destination moving, the tempo changing, Hits now and then (between ticks and on
	// them): after each block, the port from the block's start against the engine's memory.
	void lfoBitExact(const std::vector<uint8_t>& _flash)
	{
		auto os = md::fw::loadFirmwareFromFlash(_flash);
		md::engine::Engine engine(os.firmware, std::move(os.osImage));
		auto& host = engine.host();
		const auto& memory = engine.os();
		// The RND generator seeded, as a Kit's LFO block does
		std::array<uint8_t, md::engine::HostModel<>::kLfoBytes> block{};
		block[0x18] = 0x12; block[0x19] = 0x34; block[0x1a] = 0x56; block[0x1b] = 0x78;
		block[0x1c] = 0x0f; block[0x1d] = 0xed; block[0x1e] = 0xcb; block[0x1f] = 0xa9;
		host.loadLfo(0, block.data());

		constexpr int destination = 12;	// FLTF
		constexpr std::array<int, 8> speeds{1, 30, 63, 64, 65, 96, 120, 127};
		constexpr std::array<int, 4> depths{127, 64, 30, 100};
		constexpr std::array<int, 5> mixes{0, 40, 64, 100, 127};
		constexpr std::array<int, 3> values{64, 0, 127};
		constexpr std::array<double, 3> tempi{125.0, 90.0, 174.0};

		int blocks = 0, ticks = 0, hits = 0, differ = 0;
		int scenario = 0;
		for(uint8_t shape1 = 0; shape1 < mdDrums::Lfo::ShapeCount; ++shape1)
			for(uint8_t shape2 = 0; shape2 < mdDrums::Lfo::ShapeCount; ++shape2)
				for(uint8_t type = 0; type < 3; ++type, ++scenario)
				{
					const mdDrums::Lfo lfo{shape1, shape2, type};
					host.setLfo(0, 1, destination, shape1, shape2, type);
					host.setParam(0, 21, speeds[static_cast<size_t>(scenario) % speeds.size()]);
					host.setParam(0, 22, depths[static_cast<size_t>(scenario) % depths.size()]);
					host.setParam(0, 23, mixes[static_cast<size_t>(scenario) % mixes.size()]);
					host.setParam(1, destination, values[static_cast<size_t>(scenario) % values.size()]);
					host.setTempo(tempi[static_cast<size_t>(scenario) % tempi.size()]);
					for(int b = 0; b < 230; ++b, ++blocks)
					{
						const bool hit = (b + scenario) % 37 == 5;
						if(hit)
						{
							host.trigger(0, 100);
							++hits;
						}
						const bool ticking = blocks % g_blocksPerTick == 0;
						auto expected = lfoState(memory, 0);
						const auto speed = word(memory, g_voice, 0, 21);
						const auto tempo = static_cast<int32_t>(memory.peek32(g_tempo));
						const auto before = std::array<uint16_t, 3>{word(memory, g_work, 1, destination),
							word(memory, g_work, 0, 23), word(memory, g_work, 0, 22)};

						md::engine::Engine::Output out;
						require(engine.render(out), "the engine faulted");

						// The Hit first (the tick's voice loop, or the Hit's own update between ticks): the waveform routine,
						// then this LFO's apply alone, into the voices' array
						std::optional<uint16_t> atHit;
						if(expected.trigger)
						{
							lfo.wave(expected);
							atHit = mdDrums::Lfo::apply(before[0], mdDrums::Lfo::mix(expected, before[1]), before[2]);
						}
						uint16_t written;
						if(ticking)
						{
							++ticks;
							lfo.tick(expected, mdDrums::Lfo::increment(speed, tempo));
							// The apply reads what the smoothing left, which the engine's memory holds again after it
							written = mdDrums::Lfo::apply(word(memory, g_work, 1, destination),
								mdDrums::Lfo::mix(expected, word(memory, g_work, 0, 23)), word(memory, g_work, 0, 22));
						}
						else
						{
							written = atHit ? *atHit : word(memory, g_voice, 1, destination);
						}
						const auto actual = lfoState(memory, 0);
						if(!same(expected, actual) || written != word(memory, g_voice, 1, destination))
						{
							if(differ < 5)
								std::printf("  differs: shapes %d/%d type %d, block %d%s%s: phase %d/%d, current %d %d/%d %d, held %d %d/%d %d,"
									" destination %d/%d\n", shape1, shape2, type, b, ticking ? ", a tick" : "", hit ? ", a Hit" : "",
									expected.phase, actual.phase, expected.current[0], expected.current[1], actual.current[0],
									actual.current[1], expected.held[0], expected.held[1], actual.held[0], actual.held[1], written,
									word(memory, g_voice, 1, destination));
							++differ;
						}
					}
				}
		std::printf("LFO port against the OS's routines: %d shape pairs and modes, %d blocks, %d ticks, %d Hits: %d differ\n",
			scenario, blocks, ticks, hits, differ);
		require(differ == 0, "the LFO's port is not the OS's");
	}

	// What the screen draws: the wave's span and window for a few settings
	void lfoTraces()
	{
		const auto show = [](const char* _name, const mdDrums::Lfo& _lfo, const int _speed, const int _depth, const int _mix,
			const int _value)
		{
			const auto ticks = _lfo.window(_speed, _mix, 120.0);
			const auto trace = _lfo.trace(_speed, _depth, _mix, _value, 120.0, ticks);
			std::printf("  %-28s LFOS %3d LFOD %3d LFOM %3d at %3d: %6d ticks (%.2f s), step %4d, from %6.2f to %6.2f\n", _name,
				_speed, _depth, _mix, _value, ticks, ticks / mdDrums::Lfo::TickRate, trace.increment, trace.lowest / 128.0,
				trace.highest / 128.0);
			return trace;
		};
		std::printf("LFO screen traces at 120 BPM:\n");
		const mdDrums::Lfo triangle{mdDrums::Lfo::Triangle, mdDrums::Lfo::Triangle, 0};
		const auto full = show("TRI", triangle, 64, 127, 0, 64);
		require(full.lowest == 0 && full.highest == mdDrums::Lfo::MaxWord, "LFOD 127 does not swing 64 to both ends");
		const auto half = show("TRI", triangle, 64, 64, 0, 64);
		// +-$3f80 x LFOD's word >> 14: about 63 steps at LFOD 64
		require(std::abs((half.highest - half.lowest) / 128.0 - 126.0) < 3.0, "LFOD 64 does not swing about 63 steps each way");
		const auto none = show("TRI", triangle, 64, 0, 0, 64);
		require(none.lowest == none.highest, "LFOD 0 moves the destination");
		const auto cancel = show("TRI and TRI at LFOM 64", triangle, 64, 127, 64, 64);
		require(cancel.highest - cancel.lowest < 3 * 128, "LFOM 64 does not cancel two equal shapes");
		const mdDrums::Lfo random{mdDrums::Lfo::Random, mdDrums::Lfo::Random, 0};
		const auto rnd = show("RND", random, 64, 127, 0, 64);
		require(rnd.highest - rnd.lowest <= 128 * 128 && rnd.highest > rnd.lowest, "RND does not swing half as far");
		const mdDrums::Lfo ramp{mdDrums::Lfo::Ramp, mdDrums::Lfo::Ramp, 0};
		const auto rmp = show("RMP", ramp, 64, 127, 0, 0);
		require(rmp.words.front() > rmp.words.back() && rmp.words.back() == mdDrums::Lfo::settled(0), "RMP does not fall once to rest");
		const mdDrums::Lfo exponential{mdDrums::Lfo::Exponential, mdDrums::Lfo::Exponential, 0};
		show("EXP", exponential, 64, 127, 0, 0);
		const mdDrums::Lfo saw{mdDrums::Lfo::Saw, mdDrums::Lfo::Saw, 0};
		show("SAW", saw, 1, 127, 0, 64);
		show("SQR", mdDrums::Lfo{mdDrums::Lfo::Square, mdDrums::Lfo::Square, 0}, 127, 127, 0, 64);
	}

	// The frequency where the response crosses _level dB, searching from _from to _to
	double crossing(const mdDrums::FilterResponse& _response, const double _level, const double _from, const double _to)
	{
		const auto steps = 2000;
		const auto ratio = std::pow(_to / _from, 1.0 / steps);
		auto previous = _from;
		const bool above = _response.gain(_from) > _level;
		for(int i = 1; i <= steps; ++i)
		{
			const auto hz = _from * std::pow(ratio, i);
			if((_response.gain(hz) > _level) != above)
				return std::sqrt(previous * hz);
			previous = hz;
		}
		return 0.0;
	}

	double peakAt(const mdDrums::FilterResponse& _response, double& _gain)
	{
		_gain = -1000.0;
		double where = 0.0;
		for(double hz = 20.0; hz < 20000.0; hz *= 1.002)
			if(const auto g = _response.gain(hz); g > _gain)
			{
				_gain = g;
				where = hz;
			}
		return where;
	}

	// The chain itself, EQ and filter (TrackFx stopped after the filter, whose output lies in X:$2-$21), playing a sine at
	// _hz until it has settled (1.5 s), then the sine's level in its output, in dB of its input: an impulse response
	// would hold the fixed-point recursion's own offset, which a sine's correlation leaves out
	double played(const std::shared_ptr<const md::engine::TrackFx::Tables>& _tables,
		const mdDrums::FilterResponse::Settings& _settings, const double _hz)
	{
		constexpr double amplitude = 1 << 20;
		constexpr int settle = 1 << 16;
		md::engine::TrackFx fx(*_tables);
		fx.stopAfter = md::engine::TrackFx::Stop::AfterFilter2;
		md::engine::TrackFx::State state;
		md::engine::TrackFx::init(state);
		const auto word = [](const int _value) { return static_cast<uint32_t>(_value << 7); };
		const std::array<uint32_t, 9> words{0, 0, word(_settings.eqf), word(_settings.eqg), word(_settings.fltf),
			word(_settings.fltw), word(_settings.fltq), 0, 0};
		md::engine::TrackFx::setParams(state, words.data());

		const auto step = 2.0 * 3.14159265358979323846 * _hz / mdDrums::FilterResponse::SampleRate;
		const auto periods = std::ceil(8192.0 * _hz / mdDrums::FilterResponse::SampleRate);
		const auto measure = static_cast<int>(std::lround(periods * mdDrums::FilterResponse::SampleRate / _hz));
		std::array<int32_t, md::engine::TrackFx::kBlock> in{}, out{};
		double i = 0.0, q = 0.0;
		int n = 0;
		while(n < settle + measure)
		{
			for(auto& sample : in)
				sample = static_cast<int32_t>(std::lround(amplitude * std::sin(step * (n++))));
			fx.process(state, in.data(), out.data());
			for(int k = 0; k < md::engine::TrackFx::kBlock; ++k)
			{
				const auto at = n - md::engine::TrackFx::kBlock + k - settle;
				if(at < 0 || at >= measure)
					continue;
				const auto y = static_cast<double>(fx.scratch()[static_cast<size_t>(2 + k)]);
				i += y * std::cos(step * at);
				q += y * std::sin(step * at);
			}
		}
		return 20.0 * std::log10(2.0 * std::sqrt(i * i + q * q) / measure / amplitude);
	}

	void filterResponse(const std::vector<uint8_t>& _flash)
	{
		mdDrums::Engine engine(_flash);
		const auto tables = engine.fxTables();
		mdDrums::FilterResponse response(tables);

		// The response against the chain playing sines, both from an untouched Track's at 1 kHz, from 20 Hz to 20 kHz
		// wherever the response is above -40 dB (the screen shows down to -29 dB; lower, the fixed-point recursions' own
		// rounding parts from the linear response)
		const auto reference = played(tables, {}, 1000.0);
		const std::vector<mdDrums::FilterResponse::Settings> cases{{}, {64, 64, 0, 32, 0}, {64, 64, 64, 127, 0},
			{64, 64, 64, 127, 127}, {64, 64, 32, 40, 64}, {64, 127, 0, 127, 0}, {0, 0, 0, 127, 0}, {127, 127, 0, 127, 0},
			{100, 20, 20, 60, 100}, {10, 120, 2, 125, 30}, {64, 64, 127, 0, 127}};
		double worst = 0.0;
		int points = 0;
		for(const auto& settings : cases)
		{
			response.set(settings);
			double caseWorst = 0.0;
			for(double hz = 20.0; hz <= 20000.0; hz *= 1.33)
			{
				if(response.gain(hz) < -40.0)
					continue;
				const auto chain = played(tables, settings, hz) - reference;
				if(std::abs(response.gain(hz) - chain) > 0.1)
					std::printf("    %.0f Hz: computed %.3f, the chain %.3f\n", hz, response.gain(hz), chain);
				caseWorst = std::max(caseWorst, std::abs(response.gain(hz) - chain));
				++points;
			}
			std::printf("  EQF %3d EQG %3d FLTF %3d FLTW %3d FLTQ %3d: within %.4f dB of the chain\n", settings.eqf,
				settings.eqg, settings.fltf, settings.fltw, settings.fltq, caseWorst);
			worst = std::max(worst, caseWorst);
		}
		std::printf("Filter and EQ response against the chain playing sines: %d points, within %.4f dB\n", points, worst);
		// 0.1 dB is a quarter of the screen's pixel (2.5 px a dB)
		require(worst < 0.1, "the response is not the chain's");

		response.set({});
		std::printf("Untouched: %.2f dB at 20 Hz, %.2f at 100 Hz, %.2f at 1 kHz, %.2f at 10 kHz, %.2f at 20 kHz; EQ centre %.0f Hz\n",
			response.gain(20), response.gain(100), response.gain(1000), response.gain(10000), response.gain(20000),
			response.eqCentre());

		double peak = 0.0;
		std::printf("High-pass, 3 dB under its passband's top (FLTW 127):");
		for(const int fltf : {0, 32, 64, 96, 112, 127})
		{
			response.set({64, 64, fltf, 127, 0});
			peakAt(response, peak);
			const auto below = response.gain(20.0) > peak - 3.0;
			std::printf(" FLTF %d %.0f Hz;", fltf, below ? 0.0 : crossing(response, peak - 3.0, 20.0, 20000.0));
		}
		response.set({64, 64, 64, 127, 0});
		require(response.gain(20) < -40.0, "FLTF 64 does not cut 20 Hz");
		std::printf("\nLow-pass, 3 dB under 0 dB (FLTF 0; 0: below 20 Hz):");
		for(const int fltw : {32, 64, 96, 112, 127})
		{
			response.set({64, 64, 0, fltw, 0});
			const auto below = response.gain(20.0) < -3.0;
			std::printf(" FLTW %d %.0f Hz;", fltw, below ? 0.0 : crossing(response, -3.0, 20.0, 20000.0));
		}
		response.set({64, 64, 0, 64, 0});
		require(response.gain(10000) < -40.0, "FLTW 64 does not cut 10 kHz");

		response.set({64, 64, 64, 127, 127});
		const auto resonance = peakAt(response, peak);
		std::printf("\nFLTQ 127 at FLTF 64: %.1f dB at %.0f Hz;", peak, resonance);
		require(peak > 10.0, "FLTQ 127 raises no peak");
		response.set({64, 64, 64, 127, 64});
		peakAt(response, peak);
		std::printf(" FLTQ 64: %.1f dB\n", peak);

		std::printf("EQ at EQG 127, its centre and its peak:");
		for(const int eqf : {0, 32, 64, 96, 112, 127})
		{
			response.set({eqf, 127, 0, 127, 0});
			const auto at = peakAt(response, peak);
			std::printf(" EQF %d %.0f Hz, %.0f Hz %.1f dB;", eqf, response.eqCentre(), at, peak);
		}
		response.set({64, 127, 0, 127, 0});
		const auto boost = response.gain(response.eqCentre());
		response.set({64, 0, 0, 127, 0});
		const auto cut = response.gain(response.eqCentre());
		std::printf("\nEQF 64: EQG 127 %.1f dB, EQG 0 %.1f dB at its centre\n", boost, cut);
		require(boost > 10.0 && cut < -10.0, "EQG does not boost and cut at the EQ's centre");

		// What a screen's frame costs the editor: the coefficients, then the 369 pixels of the plot
		const auto start = std::chrono::steady_clock::now();
		constexpr int runs = 200;
		double sink = 0.0;
		for(int i = 0; i < runs; ++i)
		{
			response.set({i % 128, 127 - i % 128, (i * 2) % 128, 127 - i % 64, (i * 3) % 128});
			for(int x = 0; x <= 368; ++x)
				sink += response.gain(20.0 * std::pow(1000.0, x / 368.0));
		}
		const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / runs;
		std::printf("a curve costs %.1f us (%.0f)\n", us, sink);
		require(us < 2000.0, "a curve costs more than 2 ms");
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsScreensTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		lfoBitExact(flash);
		lfoTraces();
		filterResponse(flash);
	}
	catch(const std::exception& _error)
	{
		std::cout << "mdDrumsScreensTest: FAIL: " << _error.what() << "\n";
		return 1;
	}
	std::cout << "mdDrumsScreensTest: PASS\n";
	return 0;
}
