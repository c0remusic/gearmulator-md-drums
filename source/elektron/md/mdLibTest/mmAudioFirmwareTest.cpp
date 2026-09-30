#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	void require(bool condition, const char* message)
	{
		if(!condition)
			throw std::runtime_error(message);
	}

	// Failure evidence: the last render's samples (interleaved stereo), so an
	// intermittent failure shows where its waveform breaks.
	std::vector<float> g_lastRender;

	void reportExpiredWaits(std::ostream& out, const md::Hardware& hardware)
	{
		using Site = md::Hardware::TransportWaitSite;
		out << "expiredWaits dsp=" << hardware.transportWaitClamps(Site::DspTime)
			<< " link=" << hardware.transportWaitClamps(Site::LinkProducer)
			<< " room=" << hardware.transportWaitClamps(Site::HostToDspRoom)
			<< " parked=" << hardware.transportWaitClamps(Site::ProducerParked)
			<< " mixerGate=" << hardware.transportWaitClamps(Site::MixerGate)
			<< " hostAudio unfilled=" << hardware.hostAudioUnderrunCount()
			<< " dropped=" << hardware.hostAudioOverflowCount() << '\n';
		// MD_TRANSPORT_DIAGNOSTICS builds: what became of the frames DSP2 sent
		// DSP1 over the link.
		const auto score = const_cast<md::Hardware&>(hardware).getTransportScorecard();
		if(score.enabled)
		{
			const auto& l = score.link[1];
			out << "link DSP2->DSP1 tx=" << l.transmitFrames << " accepted=" << l.acceptedFrames
				<< " mixerDmaOff=" << l.mmMixerDmaInactiveDrops << " producerDmaOff=" << l.mmProducerDmaInactiveDrops
				<< " retainedPrefix=" << l.mmRetainedPrefixDrops << " oldEpoch=" << l.mmStrobeChangedDuringCatchUpDrops
				<< " ringFull=" << l.ringFullDrops << " rxDisabled=" << l.receiverDisabledDrops
				<< " popped=" << l.poppedFrames << " empty=" << l.emptyReads << " stallPurged=" << l.stallPurgedFrames
				<< " strobePurged=" << l.mmStrobePurgedFrames << " maxDepth=" << l.maximumRingDepth
				<< " strobeEpoch=" << score.mmStrobeEpoch << '\n';
		}
	}

	void reportLastRender(std::ostream& out)
	{
		const size_t frames = g_lastRender.size() / 2;
		const auto at = [&](size_t frame, size_t channel) { return double(g_lastRender[frame * 2 + channel]); };
		for(size_t channel = 0; channel < 2 && frames > 4; ++channel)
		{
			// The largest second differences of the measured half, with the
			// samples around the largest one.
			std::vector<std::pair<double, size_t>> peaks;
			for(size_t frame = std::max<size_t>(frames / 2, 2); frame < frames; ++frame)
				peaks.emplace_back(std::abs(at(frame, channel) - 2 * at(frame - 1, channel) + at(frame - 2, channel)), frame);
			const auto count = std::min<size_t>(6, peaks.size());
			std::partial_sort(peaks.begin(), peaks.begin() + count, peaks.end(), std::greater<>());
			out << "channel " << channel << " largest second differences:";
			for(size_t i = 0; i < count; ++i)
				out << ' ' << peaks[i].first << "@" << peaks[i].second << "(block " << peaks[i].second / 256
					<< '+' << peaks[i].second % 256 << ')';
			out << "\n  samples around frame " << peaks[0].second << ':';
			const auto first = peaks[0].second > 8 ? peaks[0].second - 8 : 0;
			for(size_t frame = first; frame < std::min(frames, peaks[0].second + 8); ++frame)
				out << ' ' << at(frame, channel);
			out << '\n';
		}
	}

	void advance(md::Hardware& hardware, uint32_t frames)
	{
		while(frames)
		{
			const auto chunk = std::min<uint32_t>(256, frames);
			hardware.advance(chunk);
			frames -= chunk;
		}
	}

	struct DifferenceEnergy
	{
		double previous = 0, beforePrevious = 0, power = 0, difference = 0;
		void add(double sample, bool measure)
		{
			if(measure)
			{
				const auto delta = sample - 2 * previous + beforePrevious;
				difference += delta * delta;
				power += sample * sample;
			}
			beforePrevious = previous;
			previous = sample;
		}
	};

	double render(md::Hardware& hardware, double* roughness = nullptr, unsigned blocks = 64,
		bool reportIdle = false)
	{
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		double sum = 0;
		std::array<DifferenceEnergy, 2> energy{};
		std::array<double, 2> windowSum{}, windowPower{}, windowPeak{};
		unsigned windowFrames = 0;
		g_lastRender.clear();
		for(unsigned block = 0; block < blocks; ++block)
		{
			hardware.processAudio(outputs, 256, 0);
			for(size_t frame = 0; frame < samples[0].size(); ++frame)
			{
				g_lastRender.push_back(samples[0][frame]);
				g_lastRender.push_back(samples[1][frame]);
			}
			for(size_t channel = 0; channel < samples.size(); ++channel)
				for(const auto sample : samples[channel])
				{
					require(std::isfinite(sample), "non-finite MM audio");
					sum += double(sample) * sample;
					energy[channel].add(sample, block >= blocks / 2);
					if(reportIdle)
					{
						windowSum[channel] += sample;
						windowPower[channel] += double(sample) * sample;
						windowPeak[channel] = std::max(windowPeak[channel], std::abs(double(sample)));
					}
				}
			windowFrames += 256;
			if(reportIdle && ((block + 1) % 16 == 0 || block + 1 == blocks))
			{
				for(size_t channel = 0; channel < samples.size(); ++channel)
				{
					const auto mean = windowSum[channel] / windowFrames;
					const auto power = windowPower[channel] / windowFrames;
					std::cout << "Idle window ending frame " << (block + 1) * 256
						<< " channel " << channel << " mean " << mean
						<< " rms " << std::sqrt(power)
						<< " ac-rms " << std::sqrt(std::max(0.0, power - mean * mean))
						<< " peak " << windowPeak[channel] << '\n';
				}
				windowSum.fill(0);
				windowPower.fill(0);
				windowPeak.fill(0);
				windowFrames = 0;
			}
		}
		if(roughness)
		{
			const auto power = energy[0].power + energy[1].power;
			*roughness = power > 0 ? (energy[0].difference + energy[1].difference) / power : 0;
		}
		return std::sqrt(sum / (blocks * 256 * 2));
	}

	void tap(md::Hardware& hardware, md::PanelControl control)
	{
		const auto packet = md::panelPacket(md::MachineModel::Monomachine, control);
		require(packet.has_value(), "missing MM panel control");
		require(hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
		advance(hardware, 2048);
		require(hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
		advance(hardware, 4096);
	}

	void requireSmoothSine(double rms, double roughness, uint8_t note)
	{
		// For x[n] = sin(w*n), normalized second-difference energy is
		// (2 - 2*cos(w))^2. Allow envelope/filter settling, but reject silence,
		// DC and large inter-sample discontinuities. This is not a full spectral oracle.
		const auto frequency = 440.0 * std::exp2((double(note) - 69.0) / 12.0);
		const auto difference = 2.0 - 2.0 * std::cos(6.283185307179586 * frequency / md::g_samplerate);
		const auto expected = difference * difference;
		require(std::isfinite(rms) && rms > 1e-5, "GND SIN produced silence/non-finite audio");
		require(std::isfinite(roughness) && roughness > expected * 0.25
			&& roughness < expected * 4, "GND SIN waveform failed smoothness/pitch-scale check");
	}

	void testSineOracle()
	{
		for(unsigned mode = 0; mode < 6; ++mode)
		{
			DifferenceEnergy energy;
			for(unsigned frame = 0; frame < 8192; ++frame)
			{
				double sample = std::sin(6.283185307179586 * 261.6255653006 * frame / md::g_samplerate);
				if(mode == 1) sample = 0;
				if(mode == 2) sample = 0.5;
				if(mode == 3 && (frame & 15) == 0) sample = 0;
				if(mode == 4) sample = std::numeric_limits<double>::quiet_NaN();
				if(mode == 5) sample = std::numeric_limits<double>::infinity();
				energy.add(sample, frame >= 4096);
			}
			bool accepted = true;
			try
			{
				requireSmoothSine(std::sqrt(energy.power / 4096),
					energy.power > 0 ? energy.difference / energy.power : 0, 60);
			}
			catch(const std::runtime_error&) { accepted = false; }
			require(accepted == (mode == 0), "sine oracle positive/negative control failed");
		}
	}

	void loadEmptyKit(md::Hardware& hardware)
	{
		// Manual: KIT > LOAD, FUNCTION+PLAY clears the selected kit, ENTER loads
		// it. An empty kit initializes all six tracks to GND>SIN. Only this fresh
		// in-memory test machine is changed; no user project is loaded or saved.
		tap(hardware, md::PanelControl::Kit);
		tap(hardware, md::PanelControl::Enter);
		const auto function = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Function);
		const auto play = md::panelPacket(md::MachineModel::Monomachine, md::PanelControl::Play);
		require(function && play, "missing clear-kit controls");
		md::PanelRowState rows;
		const std::array packets{rows.press(*function), rows.press(*play),
			rows.release(*play), rows.release(*function)};
		for(const auto packet : packets)
		{
			require(hardware.trySendPanelEvent(packet.row, packet.mask), "clear-kit control rejected");
			advance(hardware, 2048);
		}
		advance(hardware, md::g_samplerate * 2);
		tap(hardware, md::PanelControl::Enter);
		tap(hardware, md::PanelControl::Exit);
	}

	// Long listening run (MM_LISTEN_SECONDS, default 600): the six GND SIN
	// tracks retriggered like a sequenced clip, with automation-rate CC
	// traffic, rendered block by block as a DAW does. Counts dropouts: runs
	// of exact zeros on both channels with sound on both sides, the trace of
	// a missed link burst (16 frames).
	int listen(md::Hardware& hardware)
	{
		const char* const secondsEnv = std::getenv("MM_LISTEN_SECONDS");
		const uint64_t totalFrames = (secondsEnv ? std::strtoull(secondsEnv, nullptr, 10) : 600) * md::g_samplerate;
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		const auto send = [&](const uint8_t _status, const uint8_t _a, const uint8_t _b)
		{
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, _status, _a, _b)),
				"listen MIDI rejected");
		};
		// Peak of both channels per frame, from frame 'base' on.
		std::vector<float> level;
		uint64_t base = 0, scanned = 0, dropouts = 0;
		std::array<uint64_t, 5> lengths{};	// 8-15, 16, 17-31, 32, 33-64 frames
		const auto rms = [&](const uint64_t _from, const uint64_t _to)
		{
			double sum = 0;
			for(uint64_t f = _from; f < _to; ++f)
				sum += double(level[f - base]) * level[f - base];
			return std::sqrt(sum / double(_to - _from));
		};
		// A 32-step loop of 16th notes at 120 BPM (4 s): 16 dense steps, each
		// retriggering one track, then all notes off for a second of silence
		// (the link idles), then a lone note for the last second. A CC burst
		// on all tracks starts each part, as a DAW sends when a clip starts.
		constexpr uint64_t stepFrames = 5512;
		constexpr uint64_t ccFrames = 1024;		// automation rate
		// MM_LISTEN_LONE_MIXER: instead, a 1 s loop of half a second of
		// silence and a lone note on a mixer DSP track (4-6) while DSP2 plays
		// nothing, the state of the one residual GND SIN failure (track 6
		// alone after silence). It drops link bursts constantly once the DSPs
		// lead the UC by 60 us.
		const bool loneMixer = std::getenv("MM_LISTEN_LONE_MIXER") != nullptr;
		std::array<uint8_t, 6> held{};
		uint64_t nextStep = 0, nextCc = 0, step = 0, cc = 0;
		const auto noteOff = [&](const uint8_t _track)
		{
			if(held[_track])
				send(static_cast<uint8_t>(0x80 | _track), held[_track], 0);
			held[_track] = 0;
		};
		const auto noteOn = [&](const uint8_t _track)
		{
			noteOff(_track);
			held[_track] = static_cast<uint8_t>(48 + (step * 7) % 25);
			send(static_cast<uint8_t>(0x90 | _track), held[_track], 100);
		};
		const auto burst = [&]
		{
			for(uint8_t track = 0; track < 6; ++track)
				send(static_cast<uint8_t>(0xb0 | track), 7, static_cast<uint8_t>(100 + (step + track) % 28));
		};
		for(uint64_t frame = 0; frame < totalFrames; frame += 256)
		{
			if(loneMixer && frame >= nextStep)
			{
				const uint64_t inLoop = step % 8;
				const auto track = static_cast<uint8_t>(3 + (step / 8) % 3);
				if(inLoop == 4)
				{
					burst();
					noteOn(track);
				}
				else if(inLoop == 0 && step)
					noteOff(static_cast<uint8_t>(3 + (step / 8 - 1) % 3));
				++step;
				nextStep += stepFrames;
			}
			else if(frame >= nextStep)
			{
				const uint64_t inLoop = step % 32;
				if(inLoop == 0 || inLoop == 16 || inLoop == 24)
					burst();
				const auto lone = static_cast<uint8_t>((step / 32) % 6);
				if(inLoop < 16)
					noteOn(static_cast<uint8_t>(step % 6));
				else if(inLoop == 16)
					for(uint8_t track = 0; track < 6; ++track)
						noteOff(track);
				else if(inLoop == 24)
					noteOn(lone);
				else if(inLoop == 31)
					noteOff(lone);
				++step;
				nextStep += stepFrames;
			}
			if(frame >= nextCc)
			{
				send(static_cast<uint8_t>(0xb0 | (cc % 6)), 7, static_cast<uint8_t>(90 + (cc * 5) % 38));
				++cc;
				nextCc += ccFrames;
			}
			hardware.processAudio(outputs, 256, 0);
			for(size_t i = 0; i < 256; ++i)
			{
				require(std::isfinite(samples[0][i]) && std::isfinite(samples[1][i]), "non-finite MM audio");
				level.push_back(std::max(std::abs(samples[0][i]), std::abs(samples[1][i])));
			}
			const uint64_t end = base + level.size();
			while(scanned + 128 <= end)
			{
				if(level[scanned - base] != 0.0f)
				{
					++scanned;
					continue;
				}
				uint64_t stop = scanned;
				while(stop < end && level[stop - base] == 0.0f)
					++stop;
				const uint64_t length = stop - scanned;
				if(stop + 64 > end && length <= 64)
					break;	// judge once the sound after it is rendered
				if(length >= 8 && length <= 64 && scanned >= base + 64
					&& rms(scanned - 64, scanned) > 1e-4 && rms(stop, stop + 64) > 1e-4)
				{
					++dropouts;
					++lengths[length < 16 ? 0 : length == 16 ? 1 : length < 32 ? 2 : length == 32 ? 3 : 4];
					std::cout << "dropout at frame " << scanned << " (" << double(scanned) / md::g_samplerate
						<< " s, block " << scanned / 256 << '+' << scanned % 256 << ") length " << length << '\n';
				}
				scanned = stop;
			}
			if(scanned > base + 16384)
			{
				level.erase(level.begin(), level.begin() + 8192);
				base += 8192;
			}
			if(frame % (md::g_samplerate * 60) < 256)
				std::cout << "listen " << frame / md::g_samplerate << " s, dropouts " << dropouts << '\n' << std::flush;
		}
		std::cout << "listen seconds=" << totalFrames / md::g_samplerate << " dropouts=" << dropouts
			<< " lengths 8-15=" << lengths[0] << " 16=" << lengths[1] << " 17-31=" << lengths[2]
			<< " 32=" << lengths[3] << " 33-64=" << lengths[4] << '\n';
		reportExpiredWaits(std::cout, hardware);
		return dropouts ? 1 : 0;
	}

	void testAudioInput(md::Hardware& hardware)
	{
		const auto settle = [&](uint32_t frames) {
			while(frames) { const auto n=std::min(256u,frames); hardware.processAudio(n,0); frames-=n; }
		};
		const auto sysex = [&](std::initializer_list<uint8_t> bytes) {
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(bytes.begin(),bytes.end());
			require(hardware.sendMidi(event), "input setup SysEx rejected");
		};
		std::array<std::array<float,256>,2> input{}, output{};
		synthLib::TAudioInputs ins{}; synthLib::TAudioOutputs outs{};
		for(unsigned c=0;c<2;++c) { ins[c]=input[c].data(); outs[c]=output[c].data(); }
		for(uint8_t track=0;track<6;++track)
		{
			// Manufacturer Appendix C: FX THRU=12, output AB mask=1,
			// input A+B=3. Configure through MIDI; no private DSP RAM edits.
			sysex({0xf0,0,0x20,0x3c,3,0,0x5b,track,12,1,0xf7}); settle(44100);
			sysex({0xf0,0,0x20,0x3c,3,0,0x5c,track,1,3,0xf7});
			for(auto cc : {std::pair<uint8_t,uint8_t>{55,127},{56,0},{57,0},{58,127},
				{59,127},{7,100},{84,64},{85,0}})
				require(hardware.sendMidi({synthLib::MidiEventSource::Host,
					static_cast<uint8_t>(0xb0|track),cc.first,cc.second}), "input setup CC rejected");
			settle(44100);
			require(hardware.sendMidi({synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0x90|track),60,100}), "THRU gate rejected");
			settle(4096);
			hardware.processAudio(outs,256,0);
			for(const auto& channel:output) for(float sample:channel)
				require(std::abs(sample)<1e-5f, "THRU emitted audio with silent input");
			hardware.resetHostAudioInputQueueTelemetry();
			double power=0, sine=0, cosine=0;
			constexpr unsigned total=8192, discard=2048, measured=total-discard;
			const double omega=6.283185307179586*(310+37*track)/44100;
			for(unsigned at=0;at<total;at+=256)
			{
				for(unsigned i=0;i<256;++i) for(unsigned c=0;c<2;++c)
					input[c][i]=static_cast<float>(.05*std::sin(omega*(at+i)));
				hardware.processAudio(ins,outs,256,0);
				for(unsigned i=0;i<256;++i) if(at+i>=discard) {
					const auto sample=output[0][i]; require(std::isfinite(sample), "non-finite input output");
					power+=sample*sample; sine+=sample*std::sin(omega*(at+i)); cosine+=sample*std::cos(omega*(at+i));
				}
			}
			const auto rms=std::sqrt(power/measured);
			const auto fraction=power>0 ? 2*(sine*sine+cosine*cosine)/(measured*power) : 0;
			std::cout<<"MM input track "<<unsigned(track+1)<<" RMS="<<rms<<" fundamental fraction="<<fraction<<'\n';
			require(rms>1e-4 && fraction>.8, "THRU input missing or failed frequency oracle");
			require(hardware.hostAudioInputUnderflowCount()==0 && hardware.hostAudioInputOverflowCount()==0,
				"host input queue lost continuity");
			hardware.sendMidi({synthLib::MidiEventSource::Host,static_cast<uint8_t>(0xb0|track),7,0}); settle(44100);
		}
	}
}

int main(int argc, char** argv)
{
	if(argc == 2 && std::string_view(argv[1]) == "--sine-oracle")
	{
		try { testSineOracle(); return 0; }
		catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
	}
	const bool sineMidi = argc == 2 && std::string_view(argv[1]) == "--sine-midi";
	const bool input = argc == 2 && std::string_view(argv[1]) == "--input";
	const bool sine = sineMidi || (argc == 2 && std::string_view(argv[1]) == "--sine");
	const bool ensemble = argc == 2 && std::string_view(argv[1]) == "--digipro-ensemble";
	const bool digipro = ensemble || (argc == 2 && std::string_view(argv[1]) == "--digipro");
	const bool listening = argc == 2 && std::string_view(argv[1]) == "--listen";
	if(argc != 1 && !sine && !digipro && !input && !listening)
		return 2;
	const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if(!path || !*path)
	{
		std::cout << "mmAudioFirmwareTest: SKIP (MM firmware not supplied)\n";
		return 77;
	}
	std::unique_ptr<md::Hardware> machine;
	try
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), "could not read MM fixture");
		require(md::RomLoader::isRomForModel(rom, md::MachineModel::Monomachine),
			"MM fixture fingerprint mismatch");
		machine = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Monomachine);
		auto& hardware = *machine;
		advance(hardware, md::g_samplerate * 20);
		require(hardware.isAudioReady() && hardware.isFirmwareMidiReady(), "MM boot incomplete");
		// A pair run left on the serial scheduler would pass without ever
		// exercising the pair gates (mmPairZeroLeadFirmwareTest).
		if(md::parseTransportMode(std::getenv("MDMM_TRANSPORT")) == md::TransportMode::Pair)
			require(hardware.isDspPairThreaded(), "MDMM_TRANSPORT=pair, but the pair worker never started");
		if(sine || digipro || input || listening)
			loadEmptyKit(hardware);
		if(input) { testAudioInput(hardware); return 0; }
		if(listening)
			return listen(hardware);
		if(sineMidi)
		{
			// Manufacturer manual, Appendix C: machine 01 is GND-SIN, and
			// init=1 initializes all data pages. Preserve the original panel-only
			// sine fixture as a separate gate rather than replacing its coverage.
			for(uint8_t track = 0; track < 6; ++track)
			{
				synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
				assign.sysex = {0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, track, 1, 1, 0xf7};
				require(hardware.sendMidi(assign), "GND SIN assignment rejected");
				advance(hardware, md::g_samplerate);
			}
		}
		// Fresh hardware starts without patch RAM supplied by the host. Exercise
		// its firmware-initialized kit through ordinary MIDI, not private memory.
		// Observe the same samples used by the strict gate, without extra
		// settling frames or removing DC from its pass/fail measurement.
		const auto idleRms = render(hardware, nullptr, 64, true);
		std::cout << "Idle MM RMS " << idleRms << '\n';
		require(idleRms < 1e-7, "idle MM unexpectedly produced audio");
		for(uint8_t track = 0; track < 6; ++track)
		{
			if(digipro)
			{
				// Public manual, Appendix C: assign DPRO-DDRW (32) or DPRO-DENS (33)
				// and initialize its data pages. No host writes to private kit RAM.
				synthLib::SMidiEvent assign(synthLib::MidiEventSource::Host);
				assign.sysex = {0xf0, 0, 0x20, 0x3c, 3, 0, 0x5b, track,
					static_cast<uint8_t>(ensemble ? 33 : 32), 1, 0xf7};
				require(hardware.sendMidi(assign), "DigiPRO assignment rejected");
				advance(hardware, md::g_samplerate);
			}
			// CC7 is the documented per-track level control. Verify that the DSP
			// actually reacts to parameter traffic, not just that MIDI was accepted.
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0xb0 | track), 7, 0)), "level change rejected");
			advance(hardware, md::g_samplerate * 2);
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0x90 | track), 60, 100)), "quiet note-on rejected");
			const auto quiet = render(hardware);
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0x80 | track), 60, 0)), "quiet note-off rejected");
			advance(hardware, md::g_samplerate * 2);
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0xb0 | track), 7, 127)), "level restore rejected");
			advance(hardware, md::g_samplerate / 10);
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0x90 | track), 60, 100)), "note-on rejected");
			double roughness = 0;
			const auto rms = render(hardware, &roughness);
			std::cout << "track " << unsigned(track) << " RMS " << rms
				<< ", zero-level RMS " << quiet << ", roughness " << roughness << '\n';
			require(rms > 1e-5, "MM note produced silence");
			require(quiet < rms * 0.1, "MM level control did not attenuate audio");
			if(digipro)
			{
				// Appendix A: WAV1/WAV2 select the 64-wave bank. Appendix B:
				// synthesis parameters 1/3 are CC48/50. Sweep the entire MIDI
				// range without assuming how its 128 values map onto 64 slots.
				// DPRO-DENS instead has WAVE as synthesis parameter 4 (CC51).
				const std::vector<uint8_t> waveControllers = ensemble
					? std::vector<uint8_t>{51} : std::vector<uint8_t>{48, 50};
				for(const uint8_t cc : waveControllers)
				{
					// Sweep each slot independently: changing both together lets one
					// working selector hide a broken one. DDRW's MIX (parameter 2,
					// CC49) makes only the selected slot audible at either endpoint.
					if(!ensemble)
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0xb0 | track), 49, cc == 48 ? 0 : 127)),
							"DigiPRO waveform mix change rejected");
					double minRoughness = std::numeric_limits<double>::infinity();
					double maxRoughness = 0;
					for(unsigned value = 0; value < 128; ++value)
					{
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0x80 | track), 60, 0)), "DigiPRO sweep note-off rejected");
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0xb0 | track), cc, static_cast<uint8_t>(value))),
							"DigiPRO waveform change rejected");
						advance(hardware, md::g_samplerate / 10);
						// Retrigger each observation: the default amplitude envelope
						// decays even while a MIDI key remains held.
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0x90 | track), 60, 100)), "DigiPRO sweep note-on rejected");
						double waveRoughness = 0;
						const auto waveRms = render(hardware, &waveRoughness, 16);
						std::cout << "DigiPRO track " << unsigned(track) << " CC " << unsigned(cc)
							<< " value " << value << " RMS " << waveRms
							<< " roughness " << waveRoughness << '\n';
						require(waveRms > 1e-5, "DigiPRO waveform produced silence");
						minRoughness = std::min(minRoughness, waveRoughness);
						maxRoughness = std::max(maxRoughness, waveRoughness);
					}
					std::cout << "DigiPRO track " << unsigned(track) << " CC " << unsigned(cc)
						<< " roughness range " << minRoughness << ".." << maxRoughness << '\n';
					require(maxRoughness > minRoughness * 2, "DigiPRO waveform sweep did not change timbre");
				}
			}
			if(sine)
			{
				requireSmoothSine(rms, roughness, 60);
				require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
					static_cast<uint8_t>(0xb0 | track), 82, 64)), "SRR change rejected");
				double reducedRoughness = 0;
				const auto reduced = render(hardware, &reducedRoughness);
				std::cout << "reduced-rate RMS " << reduced << ", roughness " << reducedRoughness << '\n';
				require(reduced > 1e-5 && reducedRoughness > roughness * 2,
					"GND SIN sample-rate reduction had no observable effect");
				require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
					static_cast<uint8_t>(0xb0 | track), 82, 0)), "SRR restore rejected");
				// Burst parameter traffic across both DSPs while this track sounds.
				// Finish at nominal SRR on every voice, then require the audible DSP
				// state to agree. A MIDI/kit-status response alone would not prove it.
				for(unsigned pass = 0; pass < 16; ++pass)
					for(uint8_t voice = 0; voice < 6; ++voice)
						require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
							static_cast<uint8_t>(0xb0 | voice), 82, (pass & 1) ? 0 : 64)),
							"SRR burst rejected");
				advance(hardware, md::g_samplerate / 4);
				double restoredRoughness = 0;
				const auto restored = render(hardware, &restoredRoughness);
				std::cout << "restored-rate RMS " << restored << ", roughness " << restoredRoughness << '\n';
				requireSmoothSine(restored, restoredRoughness, 60);
			}
			require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(0x80 | track), 60, 0)), "note-off rejected");
			advance(hardware, md::g_samplerate);
			if(sine)
				for(const uint8_t note : {36, 48, 72, 84})
				{
					require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
						static_cast<uint8_t>(0x90 | track), note, 100)), "sine sweep note rejected");
					double sweepRoughness = 0;
					const auto sweepRms = render(hardware, &sweepRoughness);
					std::cout << "sine note " << unsigned(note) << " RMS " << sweepRms
						<< ", roughness " << sweepRoughness << '\n';
					requireSmoothSine(sweepRms, sweepRoughness, note);
					require(hardware.sendMidi(synthLib::SMidiEvent(synthLib::MidiEventSource::Host,
						static_cast<uint8_t>(0x80 | track), note, 0)), "sine sweep note-off rejected");
					advance(hardware, md::g_samplerate / 2);
				}
		}
		reportExpiredWaits(std::cout, hardware);
		std::cout << "mmAudioFirmwareTest: PASS\n";
		return 0;
	}
	catch(const std::exception& error)
	{
		std::cerr << "mmAudioFirmwareTest: " << error.what() << '\n';
		if(machine)
		{
			reportExpiredWaits(std::cerr, *machine);
			reportLastRender(std::cerr);
		}
		return 1;
	}
}
