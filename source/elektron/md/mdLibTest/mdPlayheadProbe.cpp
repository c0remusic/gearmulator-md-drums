// Probe, not a test: what shows the playing step while the sequencer plays. It boots the firmware,
// presses PLAY and prints, every time it changes, the front panel's step LEDs, and counts what the
// machine sends on its MIDI output (clock, start, stop, song position). For the editor's JOUER page,
// which has no playhead yet. Firmware from GEARMULATOR_MD_FIRMWARE_BIN and GEARMULATOR_MM_FIRMWARE_BIN.
// --dump <prefix> writes the RAM of both booted machines to <prefix>-md.bin and <prefix>-mm.bin, where the
// OS keeps what its flash holds compressed, such as the machine table (md::machines::parameterNames).
// --livekit prints the RAM bytes that follow ASSIGN MACHINE and parameter CCs: the live Kit
// (md::Hardware::readLiveKit).
// --tempo feeds each machine following the host a perfect MIDI clock and prints what its firmware makes of it:
// the RAM words that follow the tempo, how steady they are, the sequencer tick's lag and the TEMPO screen.

#include "mmFirmwareMachine.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mdhardware.h"
#include "mdLib/mdlivepattern.h"
#include "mdLib/mdpanel.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdsysexautomation.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" unsigned int m68k_disassemble(char* _buffer, unsigned int _pc, unsigned int _cpuType);

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void advance(md::Hardware& _hardware, uint32_t _frames)
	{
		while(_frames)
		{
			const auto count = std::min<uint32_t>(_frames, 64);
			_hardware.advance(count);
			_frames -= count;
		}
	}

	void panelTap(md::Hardware& _hardware, const md::PanelControl _control)
	{
		const auto packet = md::panelPacket(_hardware.getModel(), _control);
		require(packet.has_value(), "unknown panel control");
		require(_hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
		advance(_hardware, 2048);
		require(_hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
		advance(_hardware, 6400);
	}

	std::string stepLeds(const md::Hardware& _hardware)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		std::string leds;
		for(uint32_t step = 0; step < 16; ++step)
		{
			if(_hardware.isMonomachine())
			{
				const auto color = panel.getMonomachineStepLedColor(step);
				leds += color == md::FrontPanel::LedColor::Off ? '.' : color == md::FrontPanel::LedColor::Green ? 'g'
					: color == md::FrontPanel::LedColor::Red ? 'r' : 'y';
			}
			else
				leds += panel.getStepLed(step) ? '#' : '.';
		}
		return leds;
	}

	// The RAM bytes that count steps while the sequencer plays: one more at each step (the LEDs give the
	// step rate), back to 0 at the end of the pattern. Main RAM, patch RAM and the ColdFire's SRAM are
	// sampled every 256 frames for _seconds of play.
	void scanRam(md::Hardware& _hardware, const char* _name, const double _seconds)
	{
		struct Region { uint32_t begin, size; };
		const Region regions[] = {{0x00200000, 0x00100000}, {0x00100000, 0x00100000}, {0x01000000, 0x00002000}};
		auto& uc = _hardware.getUC();
		struct Stats { uint8_t last = 0; uint16_t changes = 0, increments = 0, wraps = 0, other = 0; };
		std::vector<std::vector<Stats>> stats;
		for(const auto& region : regions)
		{
			stats.emplace_back(region.size);
			for(uint32_t i = 0; i < region.size; ++i)
				stats.back()[i].last = uc.read8(region.begin + i);
		}
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		const auto windows = static_cast<uint32_t>(_seconds * md::g_samplerate / 256);
		std::vector<synthLib::SMidiEvent> events;
		for(uint32_t window = 0; window < windows; ++window)
		{
			_hardware.processAudio(outputs, 256, 0);
			events.clear();
			_hardware.readMidiOut(events);
			for(size_t r = 0; r < std::size(regions); ++r)
			{
				for(uint32_t i = 0; i < regions[r].size; ++i)
				{
					auto& s = stats[r][i];
					const auto value = uc.read8(regions[r].begin + i);
					if(value == s.last)
						continue;
					++s.changes;
					if(value == static_cast<uint8_t>(s.last + 1))
						++s.increments;
					else if(value == 0)
						++s.wraps;
					else
						++s.other;
					s.last = value;
				}
			}
		}
		std::printf("%s RAM step counter candidates (%.1f s of play):\n", _name, _seconds);
		for(size_t r = 0; r < std::size(regions); ++r)
		{
			for(uint32_t i = 0; i < regions[r].size; ++i)
			{
				const auto& s = stats[r][i];
				if(s.changes < 8 || s.changes > 400 || s.other > 1 || s.increments < s.changes * 3 / 4)
					continue;
				std::printf("  $%08x: %u changes, %u +1, %u to 0, %u other, now %u\n", regions[r].begin + i,
					s.changes, s.increments, s.wraps, s.other, s.last);
			}
		}
	}

	void probe(const char* _variable, const md::MachineModel _model)
	{
		const auto* path = std::getenv(_variable);
		if(!path)
			return;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, _model);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
		{
			advance(*hardware, 64);
			require(std::chrono::steady_clock::now() < deadline, "firmware boot timed out");
		}
		advance(*hardware, md::g_samplerate * 20);
		const char* name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		std::printf("%s stopped: step LEDs %s\n", name, stepLeds(*hardware).c_str());

		std::vector<synthLib::SMidiEvent> events;
		hardware->readMidiOut(events);
		panelTap(*hardware, md::PanelControl::Play);
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::string shown;
		uint32_t clocks = 0, starts = 0, stops = 0, positions = 0, other = 0;
		constexpr uint32_t windows = 4 * md::g_samplerate / 256;
		for(uint32_t window = 0; window < windows; ++window)
		{
			hardware->processAudio(outputs, 256, 0);
			events.clear();
			hardware->readMidiOut(events);
			for(const auto& event : events)
			{
				if(!event.sysex.empty())
					++other;
				else if(event.a == 0xf8)
					++clocks;
				else if(event.a == 0xfa || event.a == 0xfb)
					++starts;
				else if(event.a == 0xfc)
					++stops;
				else if(event.a == 0xf2)
					++positions;
				else
					++other;
			}
			const auto leds = stepLeds(*hardware);
			if(leds != shown)
			{
				shown = leds;
				std::printf("%s %7.1f ms  %s  clocks %u\n", name, 1000.0 * window * 256 / md::g_samplerate, leds.c_str(), clocks);
			}
		}
		std::printf("%s MIDI out in 4 s of play: %u clocks, %u starts, %u stops, %u song positions, %u other\n",
			name, clocks, starts, stops, positions, other);
		scanRam(*hardware, name, 4.0);
		panelTap(*hardware, md::PanelControl::Stop);
	}

	// The candidates' values while a pattern of 12 steps plays from PLAY: the step counter goes 0 to 11
	void trace(md::Hardware& _hardware, const char* _name, const std::vector<uint32_t>& _addresses, const double _seconds)
	{
		auto& uc = _hardware.getUC();
		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::vector<int> shown(_addresses.size(), -1);
		std::vector<synthLib::SMidiEvent> events;
		std::printf("%s 12 steps from PLAY:", _name);
		for(const auto address : _addresses)
			std::printf(" $%08x", address);
		std::printf("\n");
		const auto windows = static_cast<uint32_t>(_seconds * md::g_samplerate / 256);
		for(uint32_t window = 0; window < windows; ++window)
		{
			bool changed = false;
			std::vector<int> values;
			for(const auto address : _addresses)
				values.push_back(uc.read8(address));
			changed = values != shown;
			if(changed)
			{
				shown = values;
				std::printf("%s %7.1f ms %s ", _name, 1000.0 * window * 256 / md::g_samplerate, stepLeds(_hardware).c_str());
				for(const auto value : values)
					std::printf(" %3d", value);
				std::printf("\n");
			}
			_hardware.processAudio(outputs, 256, 0);
			events.clear();
			_hardware.readMidiOut(events);
		}
	}

	// Bytes holding one value whenever the sequencer is stopped and another whenever it plays
	void scanRunFlag(md::Hardware& _hardware, const char* _name)
	{
		auto& uc = _hardware.getUC();
		const uint32_t begin = 0x00200000, size = 0x00100000;
		const uint32_t sramBegin = 0x01000000, sramSize = 0x2000;
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(size + sramSize);
			for(uint32_t i = 0; i < size; ++i)
				bytes[i] = uc.read8(begin + i);
			for(uint32_t i = 0; i < sramSize; ++i)
				bytes[size + i] = uc.read8(sramBegin + i);
			return bytes;
		};
		const auto address = [&](const uint32_t _index) { return _index < size ? begin + _index : sramBegin + _index - size; };
		std::vector<std::vector<uint8_t>> stopped, playing;
		stopped.push_back(snapshot());
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Play);
		for(int i = 0; i < 4; ++i)
		{
			advance(_hardware, md::g_samplerate / 3);
			playing.push_back(snapshot());
		}
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Play);
		advance(_hardware, md::g_samplerate / 2);
		playing.push_back(snapshot());
		panelTap(_hardware, md::PanelControl::Stop);
		advance(_hardware, md::g_samplerate / 2);
		stopped.push_back(snapshot());
		std::printf("%s run flag candidates (stopped value -> playing value):\n", _name);
		int shownCount = 0;
		for(uint32_t i = 0; i < size + sramSize && shownCount < 60; ++i)
		{
			const auto s = stopped.front()[i];
			const auto p = playing.front()[i];
			if(s == p)
				continue;
			bool consistent = true;
			for(const auto& snap : stopped)
				consistent &= snap[i] == s;
			for(const auto& snap : playing)
				consistent &= snap[i] == p;
			if(!consistent)
				continue;
			std::printf("  $%08x: %u -> %u\n", address(i), s, p);
			++shownCount;
		}
	}

	// The same, the sequencer started and stopped by MIDI START and STOP with MIDI clock (120 BPM), as when
	// the machine follows the host
	void scanRunFlagMidi(md::Hardware& _hardware, const std::function<void(uint8_t)>& _send, const char* _name)
	{
		auto& uc = _hardware.getUC();
		const uint32_t begin = 0x00200000, size = 0x00100000;
		const uint32_t sramBegin = 0x01000000, sramSize = 0x2000;
		constexpr uint32_t framesPerTick = md::g_samplerate * 60 / (120 * 24);
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(size + sramSize);
			for(uint32_t i = 0; i < size; ++i)
				bytes[i] = uc.read8(begin + i);
			for(uint32_t i = 0; i < sramSize; ++i)
				bytes[size + i] = uc.read8(sramBegin + i);
			return bytes;
		};
		const auto address = [&](const uint32_t _index) { return _index < size ? begin + _index : sramBegin + _index - size; };
		const auto clocks = [&](const int _ticks)
		{
			for(int tick = 0; tick < _ticks; ++tick)
			{
				_send(0xf8);
				advance(_hardware, framesPerTick);
			}
		};
		std::vector<std::vector<uint8_t>> stopped, playing;
		for(int round = 0; round < 2; ++round)
		{
			advance(_hardware, md::g_samplerate / 2);
			stopped.push_back(snapshot());
			_send(0xfa);
			clocks(24);
			playing.push_back(snapshot());
			clocks(24);
			playing.push_back(snapshot());
			_send(0xfc);
			advance(_hardware, md::g_samplerate / 2);
			stopped.push_back(snapshot());
		}
		std::printf("%s run flag candidates with MIDI transport (stopped value -> playing value):\n", _name);
		int shownCount = 0;
		for(uint32_t i = 0; i < size + sramSize && shownCount < 80; ++i)
		{
			const auto s = stopped.front()[i];
			const auto p = playing.front()[i];
			if(s == p)
				continue;
			bool consistent = true;
			for(const auto& snap : stopped)
				consistent &= snap[i] == s;
			for(const auto& snap : playing)
				consistent &= snap[i] == p;
			if(!consistent)
				continue;
			std::printf("  $%08x: %u -> %u\n", address(i), s, p);
			++shownCount;
		}
	}

	void syncMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		const auto sendBytes = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_bytes.front() == 0xf0)
				event.sysex.assign(_bytes.begin(), _bytes.end());
			else
				event.a = _bytes[0];
			require(hardware->sendMidi(event), "MIDI rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			sendBytes(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto slot = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Global), 0x72));
		require(slot.has_value(), "no Global status");
		const auto patched = sysex::withGlobalSync(model, exchange(sysex::globalRequest(model, slot->value), 0x50), {true, true});
		require(patched.has_value(), "Global not patched");
		sendBytes(*patched);
		advance(*hardware, md::g_samplerate);
		scanRunFlagMidi(*hardware, [&](const uint8_t _byte) { sendBytes({_byte}); }, "MD");

		// The step and the sequencer's tick as Hardware::readSequencerPosition reads them, from MIDI START
		constexpr uint32_t framesPerTick = md::g_samplerate * 60 / (120 * 24);
		sendBytes({0xfa});
		std::string line;
		for(int tick = 0; tick < 36; ++tick)
		{
			sendBytes({0xf8});
			advance(*hardware, framesPerTick);
			const auto position = hardware->readSequencerPosition();
			line += " " + std::to_string(hardware->getUC().read8(0x00261aa7)) + "/" + std::to_string(hardware->getUC().read8(0x01001f33))
				+ (position && position->playing ? "p" : "s");
		}
		sendBytes({0xfc});
		for(int i = 0; i < 12; ++i)
		{
			advance(*hardware, md::g_samplerate / 24);
			const auto position = hardware->readSequencerPosition();
			line += " |" + std::to_string(hardware->getUC().read8(0x00261aa7)) + "/" + std::to_string(hardware->getUC().read8(0x01001f33))
				+ (position && position->playing ? "p" : "s");
		}
		std::printf("MD MIDI START, 36 ticks, STOP (step/tick, p playing, s stopped):%s\n", line.c_str());
	}

	void syncMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path)
			return;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		md::test::Monomachine machine(rom, path);
		machine.follow();
		scanRunFlagMidi(machine.hardware(), [&](const uint8_t _byte) { machine.send({_byte}); }, "MM");
	}

	// MIDI clock as synthLib::MidiClock sends it from a host playing at a steady tempo: each tick on the first
	// native frame at or after its time, scheduled at its offset in the chunk, as md::Device does
	class ClockedRun
	{
	public:
		static constexpr uint32_t Chunk = 16;

		explicit ClockedRun(md::Hardware& _hardware) : m_hardware(_hardware)
		{
			m_outputs[0] = m_samples[0].data();
			m_outputs[1] = m_samples[1].data();
		}

		void start(const double _bpm)
		{
			m_bpm = _bpm;
			m_next = static_cast<double>(m_frame);
			require(m_hardware.scheduleMidi({synthLib::MidiEventSource::Internal, synthLib::M_START}, 0), "START rejected");
		}

		void setBpm(const double _bpm) { m_bpm = _bpm; }
		uint64_t frame() const { return m_frame; }
		const std::vector<uint64_t>& ticks() const { return m_ticks; }

		void run(const uint32_t _frames, const std::function<void()>& _afterChunk = {}, const uint32_t _chunk = Chunk)
		{
			require(_chunk > 0 && _chunk <= Chunk, "chunk out of range");
			for(uint32_t done = 0; done < _frames; done += _chunk)
			{
				if(m_bpm > 0)
				{
					const auto perTick = md::g_samplerate * 60.0 / (m_bpm * 24.0);
					for(;;)
					{
						const auto at = static_cast<uint64_t>(std::ceil(m_next - 1e-7));
						if(at >= m_frame + _chunk)
							break;
						m_ticks.push_back(at);
						require(m_hardware.scheduleMidi({synthLib::MidiEventSource::Internal, synthLib::M_TIMINGCLOCK, 0, 0,
							static_cast<uint32_t>(at - m_frame)}, 0), "clock rejected");
						m_next += perTick;
					}
				}
				m_hardware.processAudio(m_outputs, _chunk, 0);
				m_frame += _chunk;
				m_events.clear();
				m_hardware.readMidiOut(m_events);
				if(_afterChunk)
					_afterChunk();
			}
		}

		// A panel key pressed and released while the clock runs
		void tap(const md::PanelControl _control)
		{
			const auto packet = md::panelPacket(m_hardware.getModel(), _control);
			require(packet.has_value(), "unknown panel control");
			require(m_hardware.trySendPanelEvent(packet->row, packet->mask), "panel press rejected");
			run(2048);
			require(m_hardware.trySendPanelEvent(packet->row, 0), "panel release rejected");
			run(6400);
		}

	private:
		md::Hardware& m_hardware;
		std::array<std::array<float, Chunk>, 2> m_samples{};
		synthLib::TAudioOutputs m_outputs{};
		std::vector<synthLib::SMidiEvent> m_events;
		double m_bpm = 0;
		double m_next = 0;
		uint64_t m_frame = 0;
		std::vector<uint64_t> m_ticks;
	};

	void printLcd(const md::Hardware& _hardware, const std::string& _title)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		std::printf("%s\n", _title.c_str());
		for(uint32_t y = 0; y < md::FrontPanel::g_lcdHeight; ++y)
		{
			std::string row;
			for(uint32_t x = 0; x < md::FrontPanel::g_lcdWidth; ++x)
				row += panel.getLcdPixel(x, y) ? '#' : '.';
			std::printf("  %s\n", row.c_str());
		}
	}

	// Main RAM, patch RAM and the ColdFire's SRAM, as one byte array
	std::vector<uint8_t> tempoRam(md::Hardware& _hardware)
	{
		auto& uc = _hardware.getUC();
		std::vector<uint8_t> bytes;
		bytes.reserve(0x00200000 + 0x2000);
		for(uint32_t i = 0; i < 0x00200000; ++i)
			bytes.push_back(uc.read8(0x00100000 + i));
		for(uint32_t i = 0; i < 0x2000; ++i)
			bytes.push_back(uc.read8(0x01000000 + i));
		return bytes;
	}

	uint32_t tempoRamAddress(const size_t _index)
	{
		return _index < 0x00200000 ? 0x00100000 + static_cast<uint32_t>(_index) : 0x01000000 + static_cast<uint32_t>(_index - 0x00200000);
	}

	uint16_t wordAt(const std::vector<uint8_t>& _bytes, const size_t _index)
	{
		return static_cast<uint16_t>(_bytes[_index] << 8 | _bytes[_index + 1]);
	}

	// What the firmware makes of a perfect clock: the 16-bit words that follow the tempo (x1.25 from 120 to 150
	// BPM) or its period (x0.8), how they move at a steady 120 BPM, the LCD before and after TEMPO, and when the
	// sequencer's tick moves after the clock tick that drives it
	void probeTempo(md::Hardware& _hardware, const char* _name, const uint32_t _tickAddress, const uint32_t _stepAddress,
		const uint32_t _ring, const uint32_t _ringCount)
	{
		ClockedRun run(_hardware);
		run.start(120);
		run.run(md::g_samplerate * 6);

		const auto snapshots = [&](const double _bpm)
		{
			run.setBpm(_bpm);
			run.run(md::g_samplerate * 6);
			std::vector<std::vector<uint8_t>> taken;
			for(int i = 0; i < 4; ++i)
			{
				run.run(md::g_samplerate);
				taken.push_back(tempoRam(_hardware));
			}
			return taken;
		};
		const auto at120 = snapshots(120);
		const auto at150 = snapshots(150);

		struct Candidate { size_t index; double mean120, mean150; };
		std::vector<Candidate> candidates;
		const auto steady = [](const std::vector<std::vector<uint8_t>>& _set, const size_t _index, double& _mean)
		{
			uint16_t lo = 0xffff, hi = 0;
			double sum = 0;
			for(const auto& bytes : _set)
			{
				const auto word = wordAt(bytes, _index);
				lo = std::min(lo, word);
				hi = std::max(hi, word);
				sum += word;
			}
			_mean = sum / static_cast<double>(_set.size());
			return lo > 0 && (hi - lo) <= _mean * 0.03;
		};
		for(size_t i = 0; i + 1 < at120.front().size(); ++i)
		{
			double mean120 = 0, mean150 = 0;
			if(!steady(at120, i, mean120) || !steady(at150, i, mean150) || mean120 < 64)
				continue;
			const auto ratio = mean150 / mean120;
			if((ratio > 1.22 && ratio < 1.28) || (ratio > 0.78 && ratio < 0.82))
				candidates.push_back({i, mean120, mean150});
		}
		std::printf("%s tempo word candidates (address: mean at 120 -> at 150 BPM), %zu in all:\n", _name, candidates.size());
		for(size_t c = 0; c < std::min<size_t>(candidates.size(), 40); ++c)
		{
			std::printf("  $%08x: %.1f -> %.1f\n", tempoRamAddress(candidates[c].index), candidates[c].mean120,
				candidates[c].mean150);
		}

		// The tempo as the firmware shows it, in 1/24 BPM: about 2880 at 120 BPM and 3600 at 150
		std::vector<size_t> tempo24;
		for(const auto& candidate : candidates)
		{
			if(std::abs(candidate.mean120 - 2880.0) < 40.0 && std::abs(candidate.mean150 - 3600.0) < 50.0)
				tempo24.push_back(candidate.index);
		}

		// At a steady 120 BPM for 20 s: every clock interval the firmware measured (its ring of the last 25, in
		// 2 us units), and the values its tempo took
		run.setBpm(120);
		run.run(md::g_samplerate * 4);
		auto& uc = _hardware.getUC();
		{
			const auto read16 = [&](const uint32_t _address)
			{
				return static_cast<uint16_t>(uc.read8(_address) << 8 | uc.read8(_address + 1));
			};
			// The whole ring once every _ringCount clock ticks: each read finds only intervals measured since
			// the last one
			const double ringFrames = _ringCount * md::g_samplerate * 60.0 / (120.0 * 24.0);
			double nextRing = static_cast<double>(run.frame()) + ringFrames;
			std::vector<double> intervals;
			std::vector<std::map<uint16_t, uint32_t>> tempoValues(tempo24.size());
			uint32_t chunks = 0;
			run.run(md::g_samplerate * 20, [&]
			{
				if(static_cast<double>(run.frame()) >= nextRing)
				{
					nextRing += ringFrames;
					for(uint32_t s = 0; s < _ringCount; ++s)
						intervals.push_back(2.0 * read16(_ring + 4 * s));
				}
				if((++chunks % (256 / ClockedRun::Chunk)) != 0)
					return;
				for(size_t t = 0; t < tempo24.size(); ++t)
					++tempoValues[t][read16(tempoRamAddress(tempo24[t]))];
			});
			const double expected = 1e6 * 60.0 / (120.0 * 24.0);
			if(!intervals.empty())
			{
				double sum = 0, squares = 0;
				uint32_t exact = 0;
				for(const auto interval : intervals)
				{
					sum += interval;
					squares += (interval - expected) * (interval - expected);
					exact += std::abs(interval - expected) <= 2.0 ? 1 : 0;
				}
				const auto [lo, hi] = std::minmax_element(intervals.begin(), intervals.end());
				std::printf("%s clock intervals measured by the firmware at 120 BPM (%.1f us sent): %zu, mean %.1f us, "
					"rms error %.1f us, min %.0f us, max %.0f us, within 2 us %.0f%%\n", _name, expected, intervals.size(),
					sum / static_cast<double>(intervals.size()), std::sqrt(squares / static_cast<double>(intervals.size())),
					*lo, *hi, 100.0 * exact / static_cast<double>(intervals.size()));
			}
			for(size_t t = 0; t < tempo24.size(); ++t)
			{
				std::string shown;
				for(const auto& [value, count] : tempoValues[t])
				{
					char text[32];
					std::snprintf(text, sizeof(text), " %.2f:%u", value / 24.0, count);
					shown += text;
				}
				std::printf("%s tempo at $%08x over 20 s at 120 BPM (BPM:samples):%s\n", _name,
					tempoRamAddress(tempo24[t]), shown.c_str());
			}
		}
		// Each clock interval as the firmware wrote it, tick by tick: its ring read 3 ms after every tick (one
		// slot changes, or none when the new interval equals the old one), with the sequencer's step then
		const auto series = [&](const char* _state)
		{
			const auto read16 = [&](const uint32_t _address)
			{
				return static_cast<uint16_t>(uc.read8(_address) << 8 | uc.read8(_address + 1));
			};
			std::vector<uint16_t> ring(_ringCount);
			for(uint32_t s = 0; s < _ringCount; ++s)
				ring[s] = read16(_ring + 4 * s);
			struct Sample { int slot; int changes; uint16_t value; uint8_t step; };
			std::vector<Sample> samples;
			size_t seenTicks = run.ticks().size();
			int lastSlot = -1;
			uint64_t readAt = 0;
			bool pending = false;
			run.run(md::g_samplerate * 10, [&]
			{
				const auto& ticks = run.ticks();
				if(ticks.size() != seenTicks)
				{
					seenTicks = ticks.size();
					readAt = ticks.back() + md::g_samplerate * 3 / 1000;
					pending = true;
				}
				if(!pending || run.frame() < readAt)
					return;
				pending = false;
				int changed = -1, changes = 0;
				for(uint32_t s = 0; s < _ringCount; ++s)
				{
					const auto value = read16(_ring + 4 * s);
					if(value == ring[s])
						continue;
					ring[s] = value;
					changed = static_cast<int>(s);
					++changes;
				}
				const int slot = changed >= 0 ? changed : lastSlot >= 0 ? (lastSlot + 1) % static_cast<int>(_ringCount) : -1;
				lastSlot = slot;
				samples.push_back({slot, changes, slot >= 0 ? ring[static_cast<size_t>(slot)] : uint16_t{0}, uc.read8(_stepAddress)});
			});
			double squares = 0;
			for(const auto& s : samples)
			{
				const auto deviation = 2.0 * s.value - 1e6 * 60.0 / (120.0 * 24.0);
				squares += deviation * deviation;
			}
			std::printf("%s %s, each clock interval at 120 BPM, rms %.1f us; us from 20833 (step:deviation, ! when not "
				"one slot changed):\n", _name, _state, samples.empty() ? 0.0 : std::sqrt(squares / static_cast<double>(samples.size())));
			std::string line;
			for(size_t i = 0; i < samples.size(); ++i)
			{
				const auto& s = samples[i];
				char text[32];
				std::snprintf(text, sizeof(text), " %2u:%+5.0f%s", s.step, 2.0 * s.value - 1e6 * 60.0 / (120.0 * 24.0),
					s.changes == 1 ? "" : "!");
				line += text;
				if((i + 1) % 12 == 0 || i + 1 == samples.size())
				{
					std::printf("  %s\n", line.c_str());
					line.clear();
				}
			}
		};
		series("playing");
		// The same with the sequencer stopped, the clock still running
		require(_hardware.scheduleMidi({synthLib::MidiEventSource::Internal, synthLib::M_STOP}, 0), "STOP rejected");
		run.run(md::g_samplerate * 4);
		series("stopped");
		require(_hardware.scheduleMidi({synthLib::MidiEventSource::Internal, synthLib::M_START}, 0), "START rejected");
		run.run(md::g_samplerate * 2);
		const auto traced = std::min<size_t>(candidates.size(), 6);
		std::vector<std::vector<uint16_t>> traces(traced);
		uint32_t chunks = 0;
		run.run(md::g_samplerate * 8, [&]
		{
			if((++chunks % (1024 / ClockedRun::Chunk)) != 0)
				return;
			for(size_t c = 0; c < traced; ++c)
			{
				const auto address = tempoRamAddress(candidates[c].index);
				traces[c].push_back(static_cast<uint16_t>(uc.read8(address) << 8 | uc.read8(address + 1)));
			}
		});
		for(size_t c = 0; c < traced; ++c)
		{
			const auto& values = traces[c];
			const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
			std::string head;
			for(size_t v = 0; v < std::min<size_t>(values.size(), 24); ++v)
				head += " " + std::to_string(values[v]);
			std::printf("%s $%08x at 120 BPM: min %u max %u over %zu samples:%s\n", _name,
				tempoRamAddress(candidates[c].index), *lo, *hi, values.size(), head.c_str());
		}

		// The sequencer's tick after each clock tick: frames from the tick's scheduled frame to the end of the
		// chunk that shows the change
		const auto firstTick = run.ticks().size();
		auto lastTick = uc.read8(_tickAddress);
		std::vector<int64_t> lags;
		uint32_t changes = 0;
		run.run(md::g_samplerate * 10, [&]
		{
			const auto tick = uc.read8(_tickAddress);
			if(tick == lastTick)
				return;
			lastTick = tick;
			++changes;
			const auto& ticks = run.ticks();
			const auto now = run.frame();
			// The latest clock tick scheduled before this chunk's end
			for(size_t t = ticks.size(); t > firstTick; --t)
			{
				if(ticks[t - 1] < now)
				{
					lags.push_back(static_cast<int64_t>(now - ticks[t - 1]));
					break;
				}
			}
		});
		const auto clocks = run.ticks().size() - firstTick;
		if(!lags.empty())
		{
			std::sort(lags.begin(), lags.end());
			double sum = 0;
			for(const auto lag : lags)
				sum += static_cast<double>(lag);
			const auto ms = [](const double _frames) { return 1000.0 * _frames / md::g_samplerate; };
			std::printf("%s sequencer tick at 120 BPM: %u changes for %zu clocks; lag after the clock min %.2f ms, "
				"median %.2f ms, max %.2f ms, mean %.2f ms (chunks of %u frames)\n", _name, changes, clocks,
				ms(static_cast<double>(lags.front())), ms(static_cast<double>(lags[lags.size() / 2])),
				ms(static_cast<double>(lags.back())), ms(sum / static_cast<double>(lags.size())), ClockedRun::Chunk);
		}

		// When the firmware reads each clock byte from its UART: frames from the tick's scheduled frame to the
		// end of the one-frame chunk whose emulation consumed it. Started between two ticks, the last one read.
		while(run.ticks().empty() || run.frame() < run.ticks().back() + 300)
			run.run(1, {}, 1);
		const auto firstRead = run.ticks().size();
		auto consumed = uc.midiRxConsumedCount();
		size_t reads = 0;
		std::vector<int64_t> readLags;
		run.run(md::g_samplerate * 4, [&]
		{
			const auto now = uc.midiRxConsumedCount();
			const auto& ticks = run.ticks();
			for(; consumed < now; ++consumed, ++reads)
			{
				if(firstRead + reads < ticks.size())
					readLags.push_back(static_cast<int64_t>(run.frame()) - static_cast<int64_t>(ticks[firstRead + reads]));
			}
		}, 1);
		if(!readLags.empty())
		{
			std::sort(readLags.begin(), readLags.end());
			const auto us = [](const int64_t _frames) { return 1e6 * static_cast<double>(_frames) / md::g_samplerate; };
			std::map<int64_t, uint32_t> histogram;	// 100 us buckets
			for(const auto lag : readLags)
				++histogram[static_cast<int64_t>(us(lag) / 100.0)];
			std::string buckets;
			for(const auto& [bucket, count] : histogram)
				buckets += " " + std::to_string(bucket * 100) + ":" + std::to_string(count);
			std::printf("%s UART read of each clock byte: %zu reads for %zu clocks; lag min %.0f us, median %.0f us, "
				"p90 %.0f us, max %.0f us; per 100 us from (us:count):%s\n", _name, reads,
				run.ticks().size() - firstRead, us(readLags.front()), us(readLags[readLags.size() / 2]),
				us(readLags[readLags.size() * 9 / 10]), us(readLags.back()), buckets.c_str());
		}

		printLcd(_hardware, std::string(_name) + " LCD at 120 BPM");
		run.tap(md::PanelControl::Tempo);
		printLcd(_hardware, std::string(_name) + " LCD after TEMPO");
		for(int i = 0; i < 3; ++i)
		{
			run.run(md::g_samplerate);
			printLcd(_hardware, std::string(_name) + " LCD after TEMPO, " + std::to_string(i + 1) + " s later");
		}
	}

	// The ColdFire stepped alone, one instruction at a time, from the UART read of a clock byte until the firmware
	// writes the interval into its ring: the instructions that led there, with how often each ran (the DSPs stand
	// still meanwhile), and the interrupt setup they ran under
	void traceTimestamp(md::Hardware& _hardware, const char* _name, const uint32_t _ring, const uint32_t _ringCount)
	{
		auto& uc = _hardware.getUC();
		auto& sim = uc.getSim();
		std::printf("%s TMR1 %04x TRR1 %04x TMR2 %04x TRR2 %04x; ICR timer1 %02x timer2 %02x uart1 %02x uart2 %02x irq4 %02x; "
			"IMR %04x\n", _name, sim.read16(md::Sim::g_timer1Base + md::Sim::g_timerTmr),
			sim.read16(md::Sim::g_timer1Base + md::Sim::g_timerTrr), sim.read16(md::Sim::g_timer2Base + md::Sim::g_timerTmr),
			sim.read16(md::Sim::g_timer2Base + md::Sim::g_timerTrr), sim.read8(md::Sim::g_icrTimer1),
			sim.read8(md::Sim::g_icrTimer2), sim.read8(md::Sim::g_icrUart1), sim.read8(md::Sim::g_icrUart2),
			sim.read8(md::Sim::g_icrExtIrq4), sim.read16(md::Sim::g_imr));

		ClockedRun run(_hardware);
		run.start(120);
		run.run(md::g_samplerate * 6);
		for(int round = 0; round < 3; ++round)
		{
			// Between two ticks, then one-frame chunks until the firmware reads the next clock byte
			while(run.ticks().empty() || run.frame() < run.ticks().back() + 300)
				run.run(1, {}, 1);
			const auto consumed = uc.midiRxConsumedCount();
			while(uc.midiRxConsumedCount() == consumed)
				run.run(1, {}, 1);
			const auto readRing = [&]
			{
				std::vector<uint8_t> bytes(_ringCount * 4);
				for(uint32_t i = 0; i < bytes.size(); ++i)
					bytes[i] = uc.read8(_ring + i);
				return bytes;
			};
			const auto before = readRing();
			std::vector<uint32_t> trail;
			bool written = false;
			for(uint32_t step = 0; step < 2000000 && !written; ++step)
			{
				trail.push_back(uc.getPC());
				if(trail.size() > 8000)
					trail.erase(trail.begin(), trail.begin() + 4000);
				uc.exec();
				const auto now = readRing();
				if(now == before)
					continue;
				written = true;
				std::printf("%s round %d: interval written %u instructions after the clock byte was read; the last "
					"2000 instructions, each address once (runs, disassembly):\n", _name, round, step + 1);
				const auto first = trail.size() > 2000 ? trail.size() - 2000 : 0;
				std::vector<uint32_t> order;
				std::map<uint32_t, uint32_t> hits;
				for(size_t i = first; i < trail.size(); ++i)
				{
					if(!hits[trail[i]]++)
						order.push_back(trail[i]);
				}
				for(const auto pc : order)
				{
					// Musashi's disassembler has no ColdFire type: the 68020 one decodes ISA_A
					char text[256] = {};
					m68k_disassemble(text, pc, 4);
					std::printf("  %08x %5u  %s\n", pc, hits[pc], text);
				}
				for(uint32_t i = 0; i < now.size(); ++i)
				{
					if(now[i] != before[i])
						std::printf("  ring byte $%08x: %02x -> %02x\n", _ring + i, before[i], now[i]);
				}
			}
			if(!written)
				std::printf("%s round %d: no interval written within 2M instructions\n", _name, round);
			// Let the scheduler run again before the next round
			run.run(md::g_samplerate / 2);
		}
	}

	// --write-cost: what the editor's pattern write costs the emulation, block by block (256 frames, wall clock,
	// serial transport), the sequencer playing: before, after SAVE KIT, after the pattern dump, after the pattern
	// request, and after the three as Controller::sendPattern sends them. Each phase prints its block times and,
	// per 100 ms, the mean block time, so the stretch the machine works through the message shows.
	void writeCostMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		const auto send = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_bytes.begin(), _bytes.end());
			require(hardware->sendMidi(event), "SysEx rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			send(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto status = [&](const sysex::StatusParameter _parameter)
		{
			const auto response = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, _parameter), 0x72));
			require(response.has_value(), "no status");
			return response->value;
		};
		const auto pattern = status(sysex::StatusParameter::Pattern);
		const auto kit = status(sysex::StatusParameter::Kit);
		auto editor = sysex::MdPatternEditor::fromDump(exchange(sysex::patternRequest(model, pattern), 0x67));
		require(editor.has_value(), "pattern dump unreadable");
		panelTap(*hardware, md::PanelControl::Play);

		std::array<std::array<float, 256>, 2> samples{};
		synthLib::TAudioOutputs outputs{};
		outputs[0] = samples[0].data();
		outputs[1] = samples[1].data();
		std::vector<synthLib::SMidiEvent> events;
		double baseline = 0;
		uint8_t step = 2;
		// Bytes handed over at MIDI wire speed (31250 baud, 320 us a byte), as a cable delivers them: each
		// block schedules the bytes that arrive within it, at their offset
		std::vector<uint8_t> wire;
		size_t wireSent = 0;
		double nextByteFrame = 0;
		uint64_t frame = 0;
		const auto sendPaced = [&](const sysex::Message& _bytes)
		{
			wire.insert(wire.end(), _bytes.begin(), _bytes.end());
			nextByteFrame = std::max(nextByteFrame, static_cast<double>(frame));
		};
		const auto scheduleWire = [&]
		{
			constexpr double framesPerByte = md::g_samplerate * 10.0 / 31250.0;
			while(wireSent < wire.size())
			{
				const auto at = static_cast<uint64_t>(nextByteFrame);
				if(at >= frame + 256)
					break;
				synthLib::SMidiEvent chunk(synthLib::MidiEventSource::Host);
				chunk.sysex.push_back(wire[wireSent]);
				chunk.offset = static_cast<uint32_t>(at > frame ? at - frame : 0);
				require(hardware->scheduleMidi(chunk, 0), "byte not scheduled");
				++wireSent;
				nextByteFrame += framesPerByte;
			}
		};
		const auto measure = [&](const char* _label, const std::function<void()>& _action)
		{
			if(_action)
				_action();
			constexpr uint32_t blocks = 3 * md::g_samplerate / 256;
			std::vector<double> ms;
			size_t midiOut = 0;
			for(uint32_t b = 0; b < blocks; ++b)
			{
				scheduleWire();
				frame += 256;
				const auto start = std::chrono::steady_clock::now();
				hardware->processAudio(outputs, 256, 0);
				ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					midiOut += e.sysex.empty() ? 1 : e.sysex.size();
			}
			auto sorted = ms;
			std::sort(sorted.begin(), sorted.end());
			double sum = 0;
			for(const auto value : ms)
				sum += value;
			const auto mean = sum / static_cast<double>(ms.size());
			if(!_action)
				baseline = mean;
			// Mean block time per 100 ms (17 blocks of 256 frames)
			std::string bins;
			constexpr size_t perBin = md::g_samplerate / 10 / 256;
			for(size_t i = 0; i + perBin <= ms.size() && i < perBin * 20; i += perBin)
			{
				double binSum = 0;
				for(size_t j = i; j < i + perBin; ++j)
					binSum += ms[j];
				char text[16];
				std::snprintf(text, sizeof(text), " %.2f", binSum / perBin);
				bins += text;
			}
			std::printf("MD %-16s block (5.80 ms of audio) mean %.2f p99 %.2f max %.2f ms; %+.0f ms over 3 s against the baseline; "
				"%zu MIDI bytes out\n  per 100 ms:%s\n", _label, mean, sorted[sorted.size() * 99 / 100], sorted.back(),
				(mean - baseline) * static_cast<double>(ms.size()), midiOut, bins.c_str());
			std::fflush(stdout);
		};
		const auto toggled = [&]
		{
			step = static_cast<uint8_t>((step + 4) % 16);
			require(editor->setTrig(0, step, true), "trig not set");
			return editor->toDump();
		};
		measure("baseline", {});
		measure("SAVE KIT", [&] { send(sysex::kitSave(model, kit)); });
		measure("pattern dump", [&] { send(toggled()); });
		measure("pattern request", [&] { send(sysex::patternRequest(model, pattern)); });
		measure("all three", [&]
		{
			send(sysex::kitSave(model, kit));
			send(toggled());
			send(sysex::patternRequest(model, pattern));
		});
		measure("baseline again", {});

		// Controlled trials: the same trig set then cleared (the pattern plays the same notes), each sent when
		// the sequencer enters step 1, four times per case. Per trial: CPU over the 3 s baseline, the worst 100 ms
		// and the blocks slower than real time (5.80 ms)
		const auto waitStep = [&](const uint8_t _step)
		{
			for(uint32_t guard = 0; guard < md::g_samplerate * 4 / 64; ++guard)
			{
				if(hardware->getUC().read8(0x00261aa7) == _step)
					return;
				advance(*hardware, 64);
				frame += 64;
				events.clear();
				hardware->readMidiOut(events);
			}
		};
		bool on = false;
		const auto flip = [&]
		{
			on = !on;
			require(editor->setTrig(0, 9, on), "trig not set");
			return editor->toDump();
		};
		const auto trial = [&](const std::function<void()>& _action)
		{
			waitStep(0);
			_action();
			constexpr uint32_t blocks = 3 * md::g_samplerate / 256;
			constexpr size_t perBin = md::g_samplerate / 10 / 256;
			std::vector<double> ms;
			for(uint32_t b = 0; b < blocks; ++b)
			{
				scheduleWire();
				frame += 256;
				const auto start = std::chrono::steady_clock::now();
				hardware->processAudio(outputs, 256, 0);
				ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
				events.clear();
				hardware->readMidiOut(events);
			}
			double extra = 0, worst = 0;
			uint32_t slow = 0;
			for(size_t i = 0; i < ms.size(); ++i)
			{
				extra += ms[i];
				slow += ms[i] > 5.80 ? 1 : 0;
			}
			for(size_t i = 0; i + perBin <= ms.size(); i += perBin)
			{
				double bin = 0;
				for(size_t j = i; j < i + perBin; ++j)
					bin += ms[j];
				worst = std::max(worst, bin / perBin);
			}
			return std::array<double, 3>{extra, worst, static_cast<double>(slow)};
		};
		const auto cases = std::vector<std::pair<const char*, std::function<void()>>>{
			{"nothing", [] {}},
			{"dump, current", [&] { send(flip()); }},
			{"dump, other slot", [&]
			{
				auto other = sysex::MdPatternEditor::fromDump(flip());
				require(other && other->setSlot(static_cast<uint8_t>((pattern + 1) % 128)), "slot not set");
				send(other->toDump());
			}},
			{"dump at 31250", [&] { sendPaced(flip()); }},
			{"SAVE KIT", [&] { send(sysex::kitSave(model, kit)); }},
			{"pattern request", [&] { send(sysex::patternRequest(model, pattern)); }},
			{"write as sent", [&]
			{
				send(sysex::kitSave(model, kit));
				send(flip());
				send(sysex::patternRequest(model, pattern));
			}},
		};
		double nothing = 0;
		for(const auto& [label, action] : cases)
		{
			std::string line;
			double extraSum = 0;
			for(int t = 0; t < 4; ++t)
			{
				const auto [total, worst, slow] = trial(action);
				extraSum += total;
				char text[64];
				std::snprintf(text, sizeof(text), "  worst 100 ms %.2f, %2.0f slow", worst, slow);
				line += text;
			}
			const auto mean = extraSum / 4.0;
			if(std::string(label) == "nothing")
				nothing = mean;
			std::printf("MD trial %-16s CPU over 3 s %+6.0f ms against nothing;%s\n", label, mean - nothing, line.c_str());
			std::fflush(stdout);
		}
	}

	void tempoMachines(const bool _code)
	{
		if(const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN"))
		{
			namespace sysex = md::automation::sysex;
			constexpr auto model = md::MachineModel::Machinedrum;
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			auto hardware = std::make_unique<md::Hardware>(rom, path, model);
			while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
				advance(*hardware, 64);
			advance(*hardware, md::g_samplerate * 20);
			const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
			{
				std::vector<synthLib::SMidiEvent> events;
				hardware->readMidiOut(events);
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				event.sysex.assign(_request.begin(), _request.end());
				require(hardware->sendMidi(event), "SysEx rejected");
				for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
				{
					advance(*hardware, 64);
					events.clear();
					hardware->readMidiOut(events);
					for(const auto& e : events)
						if(e.sysex.size() > 9 && e.sysex[6] == _command)
							return sysex::Message(e.sysex.begin(), e.sysex.end());
				}
				throw std::runtime_error("no answer");
			};
			const auto slot = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Global), 0x72));
			require(slot.has_value(), "no Global status");
			const auto global = exchange(sysex::globalRequest(model, slot->value), 0x50);
			const auto patched = sysex::withGlobalSync(model, global, {true, true});
			require(patched.has_value(), "Global not patched");
			// As md::HostSync does: the patched Global, then the slot reloaded, without which it is stored only
			for(const auto& message : {*patched, sysex::globalReload(model, global[9])})
			{
				synthLib::SMidiEvent write(synthLib::MidiEventSource::Host);
				write.sysex.assign(message.begin(), message.end());
				require(hardware->sendMidi(write), "Global rejected");
				advance(*hardware, md::g_samplerate / 2);
			}
			const auto check = sysex::parseGlobalSync(model, exchange(sysex::globalRequest(model, slot->value), 0x50));
			require(check && *check == sysex::GlobalSync{true, true}, "the Machinedrum does not follow the host");
			advance(*hardware, md::g_samplerate);
			if(_code)
				traceTimestamp(*hardware, "MD", 0x002654fc, 25);
			else
				probeTempo(*hardware, "MD", 0x01001f33, 0x00261aa7, 0x002654fc, 25);
		}
		if(const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN"))
		{
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			md::test::Monomachine machine(rom, path);
			machine.follow();
			if(_code)
				traceTimestamp(machine.hardware(), "MM", 0x0029cd86, 25);
			else
				probeTempo(machine.hardware(), "MM", 0x002bc29f, 0x002bc287, 0x0029cd86, 25);
		}
	}

	void confirmMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_request.begin(), _request.end());
			require(hardware->sendMidi(event), "SysEx rejected");
			for(uint32_t block = 0; block < md::g_samplerate * 3 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto status = sysex::parseStatusResponse(md::MachineModel::Machinedrum,
			exchange(sysex::statusRequest(md::MachineModel::Machinedrum, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		auto editor = sysex::MdPatternEditor::fromDump(exchange(sysex::patternRequest(md::MachineModel::Machinedrum, status->value), 0x67));
		require(editor && editor->setLength(12), "pattern length refused");
		synthLib::SMidiEvent write(synthLib::MidiEventSource::Host);
		const auto dump = editor->toDump();
		write.sysex.assign(dump.begin(), dump.end());
		require(hardware->sendMidi(write), "pattern write rejected");
		advance(*hardware, md::g_samplerate);
		trace(*hardware, "MD stopped", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 1.0);
		panelTap(*hardware, md::PanelControl::Play);
		trace(*hardware, "MD", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 0.5);
		panelTap(*hardware, md::PanelControl::Stop);
		trace(*hardware, "MD after STOP", {0x00261aa7, 0x0028d151, 0x00284c65, 0x01001f33}, 1.0);
		advance(*hardware, md::g_samplerate);
		scanRunFlag(*hardware, "MD");
	}

	// Writes the RAM from $100000 to $400000 of a booted machine to _file, the SIM's registers left as
	// zeros: the decompressed OS, for strings such as the machines' parameter names.
	void dumpRam(md::Hardware& _hardware, const std::string& _file)
	{
		auto& uc = _hardware.getUC();
		std::vector<uint8_t> bytes(0x00300000);
		for(uint32_t i = 0; i < bytes.size(); ++i)
		{
			const auto address = 0x00100000 + i;
			if(address < 0x00300000 || address >= 0x00310000)
				bytes[i] = uc.read8(address);
		}
		require(baseLib::filesystem::writeFile(_file, bytes), "cannot write " + _file);
		std::printf("%s written\n", _file.c_str());
	}

	void dumpMachines(const std::string& _prefix)
	{
		if(const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN"))
		{
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
			while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
				advance(*hardware, 64);
			advance(*hardware, md::g_samplerate * 20);
			dumpRam(*hardware, _prefix + "-md.bin");
		}
		if(const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN"))
		{
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			md::test::Monomachine machine(rom, path);
			dumpRam(machine.hardware(), _prefix + "-mm.bin");
		}
	}

	// The RAM bytes that hold the live Kit, the one the machine plays: what ASSIGN MACHINE ($5B) and a
	// parameter's CC change at once, where a Kit request answers with the stored Kit. Addresses whose byte
	// follows two assignments (or two values) in turn are printed.
	std::vector<uint8_t> snapshotRam(md::Hardware& _hardware)
	{
		auto& uc = _hardware.getUC();
		std::vector<uint8_t> bytes(0x00300000);
		for(uint32_t i = 0; i < bytes.size(); ++i)
		{
			const auto address = 0x00100000 + i;
			if(address < 0x00300000 || address >= 0x00310000)
				bytes[i] = uc.read8(address);
		}
		return bytes;
	}

	void printFollowers(const char* _what, const std::vector<uint8_t>& _before, const std::vector<uint8_t>& _first,
		const std::vector<uint8_t>& _second, const uint8_t _a, const uint8_t _b)
	{
		std::string line;
		size_t count = 0;
		for(size_t i = 0; i < _first.size(); ++i)
		{
			if(_before[i] == _a || _first[i] != _a || _second[i] != _b)
				continue;
			if(++count <= 24)
			{
				char text[16];
				std::snprintf(text, sizeof(text), " $%06x", static_cast<unsigned>(0x00100000 + i));
				line += text;
			}
		}
		std::printf("%s: %zu bytes follow %u then %u:%s\n", _what, count, _a, _b, line.c_str());
	}

	void probeLiveKit(md::Hardware& _hardware, const std::function<void(const md::automation::sysex::Message&)>& _send,
		const md::MachineModel _model, const uint16_t _machineA, const uint16_t _machineB, const char* _name)
	{
		namespace sysex = md::automation::sysex;
		const auto settle = [&] { advance(_hardware, md::g_samplerate / 2); };
		const auto assign = [&](const uint8_t _track, const uint16_t _machine)
		{
			const auto message = sysex::assignMachine(_model, _track, _machine);
			require(message.has_value(), "machine not assignable");
			_send(*message);
			settle();
		};
		const auto cc = [&](const uint8_t _page, const uint8_t _track, const uint8_t _index, const uint8_t _value)
		{
			const auto message = md::automation::encodeParameterChange(_model, {_page, _track, _index, _value}, 0);
			require(message.has_value(), "parameter not encodable");
			_send(sysex::Message(message->begin(), message->end()));
			settle();
		};

		// Machines: track 1 takes A then B, track 2 then takes A
		auto before = snapshotRam(_hardware);
		assign(0, _machineA);
		const auto first = snapshotRam(_hardware);
		assign(0, _machineB);
		const auto second = snapshotRam(_hardware);
		printFollowers((std::string(_name) + " track 1 machine").c_str(), before, first, second,
			static_cast<uint8_t>(_machineA), static_cast<uint8_t>(_machineB));
		assign(1, _machineA);
		const auto third = snapshotRam(_hardware);
		printFollowers((std::string(_name) + " track 2 machine").c_str(), second, third, third,
			static_cast<uint8_t>(_machineA), static_cast<uint8_t>(_machineA));

		// Parameters: track 1's first synthesis parameter takes 11 then 99; its second 33 then 77; track 2's
		// first 22 then 88
		before = snapshotRam(_hardware);
		cc(0, 0, 0, 11);
		auto p1 = snapshotRam(_hardware);
		cc(0, 0, 0, 99);
		auto p2 = snapshotRam(_hardware);
		printFollowers((std::string(_name) + " track 1 param 1").c_str(), before, p1, p2, 11, 99);
		cc(0, 0, 1, 33);
		p1 = snapshotRam(_hardware);
		cc(0, 0, 1, 77);
		p2 = snapshotRam(_hardware);
		printFollowers((std::string(_name) + " track 1 param 2").c_str(), before, p1, p2, 33, 77);
		cc(0, 1, 0, 22);
		p1 = snapshotRam(_hardware);
		cc(0, 1, 0, 88);
		p2 = snapshotRam(_hardware);
		printFollowers((std::string(_name) + " track 2 param 1").c_str(), before, p1, p2, 22, 88);
	}

	void liveKitMachines()
	{
		if(const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN"))
		{
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
			while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
				advance(*hardware, 64);
			advance(*hardware, md::g_samplerate * 20);
			const auto send = [&](const md::automation::sysex::Message& _bytes)
			{
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				if(_bytes.front() == 0xf0)
					event.sysex.assign(_bytes.begin(), _bytes.end());
				else
				{
					event.a = _bytes[0];
					event.b = _bytes.size() > 1 ? _bytes[1] : 0;
					event.c = _bytes.size() > 2 ? _bytes[2] : 0;
				}
				require(hardware->sendMidi(event), "MIDI rejected");
			};
			// TRX-SD then EFM-BD
			probeLiveKit(*hardware, send, md::MachineModel::Machinedrum, 17, 32, "MD");
		}
		if(const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN"))
		{
			std::vector<uint8_t> rom;
			require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
			md::test::Monomachine machine(rom, path);
			// SWAVE-SAW then FM+-STAT
			probeLiveKit(machine.hardware(), [&](const md::automation::sysex::Message& _bytes) { machine.send(_bytes); },
				md::MachineModel::Monomachine, 4, 8, "MM");
		}
	}

	// SET LFO PARAM ($62, MCL's MD::setLFOParam): what the Machinedrum keeps of each value, read back from
	// the live Kit's LFO block of track 1 ($1001ea: destination track, parameter, shape 1, shape 2, update)
	void probeLfo()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		auto& uc = hardware->getUC();
		const auto block = [&]
		{
			std::string text;
			for(uint32_t i = 0; i < 5; ++i)
				text += " " + std::to_string(uc.read8(0x001001ea + i));
			return text;
		};
		std::printf("MD LFO 1 block at boot:%s\n", block().c_str());
		for(const uint8_t field : {0, 1, 2, 3, 4})
		{
			std::string line;
			for(const uint8_t value : {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 15, 16, 23, 24, 31, 127})
			{
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				event.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x62, field, value, 0xf7};
				require(hardware->sendMidi(event), "SysEx rejected");
				advance(*hardware, md::g_samplerate / 5);
				line += " " + std::to_string(value) + ">" + std::to_string(uc.read8(0x001001ea + field));
			}
			std::printf("MD LFO 1 field %u (sent>kept):%s\n", field, line.c_str());
		}
		std::printf("MD LFO 1 block at end:%s\n", block().c_str());

		// Each shape running (free, fast, full depth, shape 1 only): the state byte that moves most, traced
		const auto lfoParam = [&](const uint8_t _field, const uint8_t _value)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x62, _field, _value, 0xf7};
			require(hardware->sendMidi(event), "SysEx rejected");
		};
		const auto cc = [&](const uint8_t _index, const uint8_t _value)
		{
			const auto message = md::automation::encodeParameterChange(md::MachineModel::Machinedrum,
				{md::automation::machinedrum::Routing, 0, _index, _value}, 0);
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.a = (*message)[0];
			event.b = (*message)[1];
			event.c = (*message)[2];
			require(hardware->sendMidi(event), "CC rejected");
		};
		lfoParam(0, 0);		// track 1
		lfoParam(1, 17);	// VOL
		lfoParam(4, 0);		// FREE
		lfoParam(2, 0);
		lfoParam(3, 0);
		cc(5, 96);			// LFOS
		cc(7, 0);			// LFOM: shape 1

		// Per RAM byte ($200000-$2fffff, then the SRAM), how many values it takes over 128 samples
		const auto distinct = [&]
		{
			constexpr uint32_t ramSize = 0x00100000, sramSize = 0x2000;
			std::vector<std::array<uint8_t, 128>> samples(ramSize + sramSize);
			for(size_t s = 0; s < 128; ++s)
			{
				advance(*hardware, 256);
				for(uint32_t i = 0; i < ramSize; ++i)
					samples[i][s] = uc.read8(0x00200000 + i);
				for(uint32_t i = 0; i < sramSize; ++i)
					samples[ramSize + i][s] = uc.read8(0x01000000 + i);
			}
			std::vector<uint16_t> counts(samples.size());
			for(size_t i = 0; i < samples.size(); ++i)
			{
				std::array<bool, 256> seen{};
				for(const auto value : samples[i])
					counts[i] += seen[value] ? 0 : (seen[value] = true, 1);
			}
			return counts;
		};
		const auto addressOf = [](const size_t _index)
		{
			return _index < 0x00100000 ? 0x00200000 + static_cast<uint32_t>(_index) : 0x01000000 + static_cast<uint32_t>(_index - 0x00100000);
		};
		cc(6, 0);			// LFOD 0: what moves anyway
		advance(*hardware, md::g_samplerate / 2);
		const auto still = distinct();
		cc(6, 127);			// LFOD 127
		advance(*hardware, md::g_samplerate / 2);
		const auto moving = distinct();
		std::vector<std::pair<uint16_t, size_t>> candidates;
		for(size_t i = 0; i < moving.size(); ++i)
		{
			if(still[i] <= 1 && moving[i] >= 8)
				candidates.push_back({moving[i], i});
		}
		std::sort(candidates.rbegin(), candidates.rend());
		std::string list;
		for(size_t c = 0; c < std::min<size_t>(candidates.size(), 16); ++c)
		{
			char text[32];
			std::snprintf(text, sizeof(text), " $%06x:%u", addressOf(candidates[c].second), candidates[c].first);
			list += text;
		}
		std::printf("MD LFO output candidates (address:values), %zu in all:%s\n", candidates.size(), list.c_str());
		if(candidates.empty())
			return;

		// Each shape traced on the best candidate, the LFO restarted by the trigs of the pattern playing (TRIG),
		// at a depth that does not clip: the one-shot shapes show after each trig
		const auto address = addressOf(candidates.front().second);
		lfoParam(4, 1);
		cc(6, 40);
		cc(5, 64);
		panelTap(*hardware, md::PanelControl::Play);
		for(uint8_t shape = 0; shape < 6; ++shape)
		{
			lfoParam(2, shape);
			lfoParam(3, shape);
			advance(*hardware, md::g_samplerate / 4);
			std::string line;
			for(size_t s = 0; s < 128; ++s)
			{
				advance(*hardware, 128);
				line += " " + std::to_string(uc.read8(address));
			}
			std::printf("TRIG shape %u $%06x every 128 frames:%s\n", shape, address, line.c_str());
		}
		panelTap(*hardware, md::PanelControl::Stop);
	}

	// Where the Machinedrum keeps the pattern it plays (--pattern-ram): the RAM bytes that follow one trig written
	// on and off by pattern dumps ($67), for a few tracks and steps; then whether a trig written there directly is
	// what the next pattern request answers with, and whether it plays.
	void patternRamMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		auto& uc = hardware->getUC();
		const auto send = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_bytes.begin(), _bytes.end());
			require(hardware->sendMidi(event), "SysEx rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			send(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 4 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto status = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		const auto slot = status->value;

		// A pattern of 16 steps, no trig: what each toggle starts from
		auto blank = sysex::MdPatternEditor::fromDump(exchange(sysex::patternRequest(model, slot), 0x67));
		require(blank.has_value() && blank->setLength(16), "pattern unreadable");
		blank->clear();
		const auto blankDump = blank->toDump();

		constexpr uint32_t dramBegin = 0x00100000, dramSize = 0x00300000, sramBegin = 0x01000000, sramSize = 0x2000;
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(dramSize + sramSize);
			for(uint32_t i = 0; i < dramSize; ++i)
			{
				const auto address = dramBegin + i;
				if(address < 0x00300000 || address >= 0x00310000)
					bytes[i] = uc.read8(address);
			}
			for(uint32_t i = 0; i < sramSize; ++i)
				bytes[dramSize + i] = uc.read8(sramBegin + i);
			return bytes;
		};
		const auto addressOf = [&](const size_t _index)
		{
			return _index < dramSize ? dramBegin + static_cast<uint32_t>(_index) : sramBegin + static_cast<uint32_t>(_index - dramSize);
		};
		const auto written = [&](const std::optional<std::pair<uint8_t, uint8_t>> _trig)
		{
			auto editor = sysex::MdPatternEditor::fromDump(blankDump);
			require(editor.has_value() && (!_trig || editor->setTrig(_trig->first, _trig->second, true)), "trig not set");
			send(editor->toDump());
			advance(*hardware, md::g_samplerate);
			return snapshot();
		};

		// Bytes equal in both snapshots without the trig and in both with it, and different between the two
		struct Spot { uint32_t address; uint8_t off; uint8_t on; };
		const auto locate = [&](const uint8_t _track, const uint8_t _step)
		{
			const auto off1 = written(std::nullopt);
			const auto on1 = written(std::make_pair(_track, _step));
			const auto off2 = written(std::nullopt);
			const auto on2 = written(std::make_pair(_track, _step));
			std::vector<Spot> spots;
			for(size_t i = 0; i < off1.size(); ++i)
			{
				if(off1[i] == off2[i] && on1[i] == on2[i] && off1[i] != on1[i])
					spots.push_back({addressOf(i), off1[i], on1[i]});
			}
			std::string line;
			for(size_t s = 0; s < std::min<size_t>(spots.size(), 16); ++s)
			{
				char text[32];
				std::snprintf(text, sizeof(text), " $%06x:%02x>%02x", spots[s].address, spots[s].off, spots[s].on);
				line += text;
			}
			std::printf("MD trig track %u step %u: %zu bytes follow it:%s\n", _track + 1, _step + 1, spots.size(), line.c_str());
			std::fflush(stdout);
			return spots;
		};
		for(const auto& [track, step] : std::vector<std::pair<uint8_t, uint8_t>>{{0, 1}, {0, 2}, {0, 9}, {1, 2}, {15, 2}})
			(void)locate(track, step);
		const auto spots = locate(0, 4);
		if(spots.empty())
			return;

		// Written directly: the blank pattern taken, then track 1 step 5's bytes given their value with the trig
		const auto trigInDump = [&]
		{
			const auto pattern = sysex::parseMdPatternDump(exchange(sysex::patternRequest(model, slot), 0x67));
			require(pattern.has_value(), "pattern unreadable");
			return pattern->hasTrig(0, 4);
		};
		(void)written(std::nullopt);
		std::printf("MD before the direct write: the dump has the trig %d\n", trigInDump() ? 1 : 0);
		for(const auto& spot : spots)
			uc.write8(spot.address, spot.on);
		advance(*hardware, md::g_samplerate / 4);
		std::printf("MD after the direct write: the dump has the trig %d\n", trigInDump() ? 1 : 0);

		// Heard: the output's energy over two bars playing, without the trig and with it written directly
		const auto energy = [&]
		{
			std::array<std::array<float, 256>, 2> samples{};
			synthLib::TAudioOutputs outputs{};
			outputs[0] = samples[0].data();
			outputs[1] = samples[1].data();
			double sum = 0;
			for(uint32_t block = 0; block < md::g_samplerate * 4 / 256; ++block)
			{
				hardware->processAudio(outputs, 256, 0);
				for(const auto& channel : samples)
					for(const auto value : channel)
						sum += static_cast<double>(value) * value;
			}
			return sum;
		};
		(void)written(std::nullopt);
		panelTap(*hardware, md::PanelControl::Play);
		const auto silent = energy();
		for(const auto& spot : spots)
			uc.write8(spot.address, spot.on);
		const auto withTrig = energy();
		for(const auto& spot : spots)
			uc.write8(spot.address, spot.off);
		const auto cleared = energy();
		panelTap(*hardware, md::PanelControl::Stop);
		std::printf("MD playing, energy over 4 s: blank %.3g, trig written directly %.3g, cleared again %.3g\n",
			silent, withTrig, cleared);

		// The whole pattern: a dump's payload (trigs, lock masks, accent/slide/swing, the plain bytes, lock rows,
		// tail, then the 64-step extension), unpacked, against the RAM from the trigs found above
		const auto unpack = [](const sysex::Message& _dump)
		{
			std::vector<uint8_t> payload;
			size_t position = 0x0a;
			const auto read7 = [&](const size_t _count)
			{
				for(size_t done = 0; done < _count;)
				{
					const auto high = _dump[position++];
					for(uint8_t bit = 0; bit < 7 && done < _count; ++bit, ++done)
						payload.push_back(static_cast<uint8_t>(_dump[position++] | ((high >> (6 - bit)) & 1u) << 7));
				}
			};
			read7(64);
			read7(64);
			read7(16);
			payload.insert(payload.end(), _dump.begin() + static_cast<std::ptrdiff_t>(position), _dump.begin() + static_cast<std::ptrdiff_t>(position) + 6);
			position += 6;
			read7(64 * 32);
			read7(204);
			if(_dump.size() == 0x1522)
				read7(64 + 12 + 64 * 32 + 192);
			return payload;
		};
		auto rich = sysex::MdPatternEditor::fromDump(blankDump);
		require(rich.has_value() && rich->setLength(64), "pattern not made 64 steps");
		for(uint8_t step = 0; step < 64; step += 3)
			require(rich->setTrig(static_cast<uint8_t>(step % 16), step, true), "trig not set");
		require(rich->setTrig(3, 9, true) && rich->setTrig(6, 18, true) && rich->setTrig(5, 40, true), "trig not set");
		require(rich->setLock(0, 3, 0, 55) && rich->setLock(3, 7, 9, 77) && rich->setLock(6, 1, 18, 99)
			&& rich->setLock(5, 2, 40, 33), "lock not set");
		require(rich->setFlag(sysex::StepFlag::Accent, std::nullopt, 6, true), "accent not set");
		const auto richDump = rich->toDump();
		send(richDump);
		advance(*hardware, md::g_samplerate);
		const auto payload = unpack(exchange(sysex::patternRequest(model, slot), 0x67));
		const auto trigs = spots.front().address & ~0x3fu;	// track 1's word ends with steps 1-8 at +3
		std::string mismatches;
		size_t equal = 0, count = 0;
		for(size_t i = 0; i < payload.size(); ++i)
		{
			if(uc.read8(trigs + static_cast<uint32_t>(i)) == payload[i])
			{
				++equal;
				continue;
			}
			if(++count <= 24)
			{
				char text[32];
				std::snprintf(text, sizeof(text), " +%zu:%02x/%02x", i, uc.read8(trigs + static_cast<uint32_t>(i)), payload[i]);
				mismatches += text;
			}
		}
		std::printf("MD pattern of %zu payload bytes against RAM from $%06x: %zu equal, %zu differ (offset:RAM/dump):%s\n",
			payload.size(), trigs, equal, count, mismatches.c_str());

		// Only track 1 step 5's trig byte written, the energy every half second: when it plays, when it stops
		const auto halves = [&](const int _count)
		{
			std::string line;
			for(int h = 0; h < _count; ++h)
			{
				std::array<std::array<float, 256>, 2> samples{};
				synthLib::TAudioOutputs outputs{};
				outputs[0] = samples[0].data();
				outputs[1] = samples[1].data();
				double sum = 0;
				for(uint32_t block = 0; block < md::g_samplerate / 2 / 256; ++block)
				{
					hardware->processAudio(outputs, 256, 0);
					for(const auto& channel : samples)
						for(const auto value : channel)
							sum += static_cast<double>(value) * value;
				}
				char text[24];
				std::snprintf(text, sizeof(text), " %.0f", sum);
				line += text;
			}
			return line;
		};
		(void)written(std::nullopt);
		const uint32_t trigByte = trigs + 3;
		panelTap(*hardware, md::PanelControl::Play);
		std::printf("MD energy per half second, blank:%s\n", halves(4).c_str());
		uc.write8(trigByte, static_cast<uint8_t>(uc.read8(trigByte) | 0x10));
		std::printf("MD trig byte $%06x set:%s\n", trigByte, halves(8).c_str());
		uc.write8(trigByte, static_cast<uint8_t>(uc.read8(trigByte) & ~0x10));
		std::printf("MD trig byte cleared:%s\n", halves(8).c_str());
		panelTap(*hardware, md::PanelControl::Stop);

		// Kept as the hardware keeps a grid edit? The trig written in RAM, another pattern selected, then this one
		uc.write8(trigByte, static_cast<uint8_t>(uc.read8(trigByte) | 0x10));
		advance(*hardware, md::g_samplerate / 4);
		const auto other = static_cast<uint8_t>((slot + 1) % 128);
		send(sysex::patternSelect(model, other));
		advance(*hardware, md::g_samplerate);
		const auto otherStatus = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Pattern), 0x72));
		send(sysex::patternSelect(model, slot));
		advance(*hardware, md::g_samplerate);
		const auto backStatus = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Pattern), 0x72));
		std::printf("MD trig written in RAM, pattern %u selected (status %d), then %u again (status %d): RAM byte %02x, dump has the trig %d\n",
			other, otherStatus ? otherStatus->value : -1, slot, backStatus ? backStatus->value : -1, uc.read8(trigByte),
			trigInDump() ? 1 : 0);

		// What lives outside the first 2198 bytes: the tail (EDIT ALL words, the tracks' own accent, slide and
		// swing) and the 64-step extension, each located as the trigs were, from a 64-step blank pattern
		auto longBlank = sysex::MdPatternEditor::fromDump(blankDump);
		require(longBlank.has_value() && longBlank->setLength(64), "pattern not made 64 steps");
		const auto longBlankDump = longBlank->toDump();
		const auto editedFrom = [&](const sysex::Message& _base, const std::function<bool(sysex::MdPatternEditor&)>& _edit, const bool _on)
		{
			auto editor = sysex::MdPatternEditor::fromDump(_base);
			require(editor.has_value() && (!_on || _edit(*editor)), "edit refused");
			send(editor->toDump());
			advance(*hardware, md::g_samplerate);
			return snapshot();
		};
		const auto locateEdit = [&](const char* _what, const sysex::Message& _base, const std::function<bool(sysex::MdPatternEditor&)>& _edit)
		{
			const auto off1 = editedFrom(_base, _edit, false);
			const auto on1 = editedFrom(_base, _edit, true);
			const auto off2 = editedFrom(_base, _edit, false);
			const auto on2 = editedFrom(_base, _edit, true);
			std::string line;
			size_t count = 0;
			for(size_t i = 0; i < off1.size(); ++i)
			{
				if(off1[i] != off2[i] || on1[i] != on2[i] || off1[i] == on1[i])
					continue;
				if(++count <= 12)
				{
					char text[32];
					std::snprintf(text, sizeof(text), " $%06x:%02x>%02x", addressOf(i), off1[i], on1[i]);
					line += text;
				}
			}
			std::printf("MD %s: %zu bytes follow it:%s\n", _what, count, line.c_str());
			std::fflush(stdout);
		};
		locateEdit("track 1 own accent step 2", blankDump, [](sysex::MdPatternEditor& _e)
		{
			return _e.setFlagPerTrack(sysex::StepFlag::Accent, true) && _e.setTrig(0, 1, true)
				&& _e.setFlag(sysex::StepFlag::Accent, uint8_t{0}, 1, true);
		});
		locateEdit("track 1 own slide step 2", blankDump, [](sysex::MdPatternEditor& _e)
		{
			return _e.setFlagPerTrack(sysex::StepFlag::Slide, true) && _e.setTrig(0, 1, true)
				&& _e.setFlag(sysex::StepFlag::Slide, uint8_t{0}, 1, true);
		});
		locateEdit("EDIT ALL off for accents", blankDump, [](sysex::MdPatternEditor& _e)
		{
			return _e.setFlagPerTrack(sysex::StepFlag::Accent, true);
		});
		locateEdit("64 steps: track 1 step 34", longBlankDump, [](sysex::MdPatternEditor& _e) { return _e.setTrig(0, 33, true); });
		locateEdit("64 steps: track 2 step 34", longBlankDump, [](sysex::MdPatternEditor& _e) { return _e.setTrig(1, 33, true); });
		locateEdit("64 steps: track 1 step 34 lock param 4", longBlankDump, [](sysex::MdPatternEditor& _e)
		{
			return _e.setTrig(0, 33, true) && _e.setLock(0, 3, 33, 66);
		});
		locateEdit("64 steps: accent step 34", longBlankDump, [](sysex::MdPatternEditor& _e)
		{
			return _e.setTrig(0, 33, true) && _e.setFlag(sysex::StepFlag::Accent, std::nullopt, 33, true);
		});
		locateEdit("length 16 to 64", blankDump, [](sysex::MdPatternEditor& _e) { return _e.setLength(64); });
	}

	// Which copy of the 64-step extension plays (--pattern-ext): a 64-step pattern without trigs, then track 1
	// step 34's trig written in one copy only, the energy every half second
	void patternExtensionMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		auto& uc = hardware->getUC();
		const auto send = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.sysex.assign(_bytes.begin(), _bytes.end());
			require(hardware->sendMidi(event), "SysEx rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			send(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 4 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		const auto status = sysex::parseStatusResponse(model, exchange(sysex::statusRequest(model, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		auto blank = sysex::MdPatternEditor::fromDump(exchange(sysex::patternRequest(model, status->value), 0x67));
		require(blank.has_value() && blank->setLength(64), "pattern unreadable");
		blank->clear();
		const auto halves = [&](const int _count)
		{
			std::string line;
			for(int h = 0; h < _count; ++h)
			{
				std::array<std::array<float, 256>, 2> samples{};
				synthLib::TAudioOutputs outputs{};
				outputs[0] = samples[0].data();
				outputs[1] = samples[1].data();
				double sum = 0;
				for(uint32_t block = 0; block < md::g_samplerate / 2 / 256; ++block)
				{
					hardware->processAudio(outputs, 256, 0);
					for(const auto& channel : samples)
						for(const auto value : channel)
							sum += static_cast<double>(value) * value;
				}
				char text[24];
				std::snprintf(text, sizeof(text), " %.0f", sum);
				line += text;
			}
			return line;
		};
		for(const uint32_t address : {0x00180003u, 0x00262563u})
		{
			send(blank->toDump());
			advance(*hardware, md::g_samplerate);
			panelTap(*hardware, md::PanelControl::Play);
			// 64 steps at 120 BPM: 8 s a loop
			const auto before = halves(4);
			uc.write8(address, static_cast<uint8_t>(uc.read8(address) | 0x02));
			const auto after = halves(20);
			panelTap(*hardware, md::PanelControl::Stop);
			std::printf("MD 64 steps, track 1 step 34 written at $%06x only; energy per half second before:%s after:%s\n",
				address, before.c_str(), after.c_str());
			std::fflush(stdout);
		}
	}

	// Where the Machinedrum stores its 128 patterns and 64 Kits, for BIBLIO to read them without a request each:
	// two patterns written with marked trigs on 64 steps, two Kits saved with marked parameters, then the marks
	// searched for in the RAM and the flash, and the bytes there compared with the dumps
	void libraryRamMachinedrum()
	{
		const auto* path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Machinedrum;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		auto hardware = std::make_unique<md::Hardware>(rom, path, model);
		while(!hardware->isFirmwareMidiReady() || !hardware->isAudioReady())
			advance(*hardware, 64);
		advance(*hardware, md::g_samplerate * 20);
		auto& uc = hardware->getUC();
		const auto send = [&](const sysex::Message& _bytes)
		{
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			if(_bytes.front() == 0xf0)
				event.sysex.assign(_bytes.begin(), _bytes.end());
			else
			{
				event.a = _bytes[0];
				event.b = _bytes.size() > 1 ? _bytes[1] : 0;
				event.c = _bytes.size() > 2 ? _bytes[2] : 0;
			}
			require(hardware->sendMidi(event), "MIDI rejected");
		};
		const auto exchange = [&](const sysex::Message& _request, const uint8_t _command)
		{
			std::vector<synthLib::SMidiEvent> events;
			hardware->readMidiOut(events);
			send(_request);
			for(uint32_t block = 0; block < md::g_samplerate * 4 / 64; ++block)
			{
				advance(*hardware, 64);
				events.clear();
				hardware->readMidiOut(events);
				for(const auto& e : events)
					if(e.sysex.size() > 9 && e.sysex[6] == _command)
						return sysex::Message(e.sysex.begin(), e.sysex.end());
			}
			throw std::runtime_error("no answer");
		};
		constexpr uint32_t ramBegin = 0x00100000, ramEnd = 0x00400000;
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(ramEnd - ramBegin);
			for(uint32_t i = 0; i < bytes.size(); ++i)
			{
				const auto address = ramBegin + i;
				if(address < 0x00300000 || address >= 0x00310000)
					bytes[i] = uc.read8(address);
			}
			return bytes;
		};
		const auto find = [](const std::vector<uint8_t>& _haystack, const std::vector<uint8_t>& _needle)
		{
			std::vector<size_t> hits;
			auto it = _haystack.begin();
			while((it = std::search(it, _haystack.end(), _needle.begin(), _needle.end())) != _haystack.end())
			{
				hits.push_back(static_cast<size_t>(it - _haystack.begin()));
				++it;
			}
			return hits;
		};
		const auto print = [&](const std::string& _what, const std::vector<size_t>& _ram, const std::vector<size_t>& _flash)
		{
			std::string line;
			for(size_t i = 0; i < std::min<size_t>(_ram.size(), 8); ++i)
			{
				char text[16];
				std::snprintf(text, sizeof(text), " $%06x", static_cast<unsigned>(ramBegin + _ram[i]));
				line += text;
			}
			for(size_t i = 0; i < std::min<size_t>(_flash.size(), 8); ++i)
			{
				char text[24];
				std::snprintf(text, sizeof(text), " flash+$%06x", static_cast<unsigned>(_flash[i]));
				line += text;
			}
			std::printf("%s: %zu in the RAM, %zu in the flash:%s\n", _what.c_str(), _ram.size(), _flash.size(), line.c_str());
			std::fflush(stdout);
		};
		// The bytes at _address against _expected: how many differ, and the first ranges that do
		const auto compare = [&](const std::string& _what, const std::vector<uint8_t>& _ram, const size_t _offset,
			const std::vector<uint8_t>& _expected)
		{
			size_t differ = 0;
			std::string ranges;
			size_t rangeCount = 0;
			for(size_t i = 0; i < _expected.size(); ++i)
			{
				const bool same = _offset + i < _ram.size() && _ram[_offset + i] == _expected[i];
				if(same)
					continue;
				++differ;
				if(i == 0 || (_offset + i - 1 < _ram.size() && _ram[_offset + i - 1] == _expected[i - 1]))
				{
					if(++rangeCount <= 12)
					{
						size_t end = i;
						while(end + 1 < _expected.size() && !(_offset + end + 1 < _ram.size() && _ram[_offset + end + 1] == _expected[end + 1]))
							++end;
						char text[48];
						std::snprintf(text, sizeof(text), " [%zx..%zx]", i, end);
						ranges += text;
					}
				}
			}
			std::printf("%s at $%06x: %zu of %zu bytes differ%s\n", _what.c_str(), static_cast<unsigned>(ramBegin + _offset),
				differ, _expected.size(), ranges.c_str());
			std::fflush(stdout);
		};
		const auto bigEndian = [](const uint32_t _a, const uint32_t _b)
		{
			return std::vector<uint8_t>{static_cast<uint8_t>(_a >> 24), static_cast<uint8_t>(_a >> 16),
				static_cast<uint8_t>(_a >> 8), static_cast<uint8_t>(_a), static_cast<uint8_t>(_b >> 24),
				static_cast<uint8_t>(_b >> 16), static_cast<uint8_t>(_b >> 8), static_cast<uint8_t>(_b)};
		};

		// Patterns: the current one made 64 steps long, its trigs cleared, then tracks 1 and 2 given marked trigs
		const auto status = sysex::parseStatusResponse(model,
			exchange(sysex::statusRequest(model, sysex::StatusParameter::Pattern), 0x72));
		require(status.has_value(), "no pattern status");
		const auto source = exchange(sysex::patternRequest(model, status->value), 0x67);
		struct Marked { uint8_t slot; std::array<uint32_t, 2> low; std::array<uint32_t, 2> high; sysex::Message dump; };
		std::vector<Marked> patterns{
			{40, {0xa5c3f00fu, 0x3c5a0ff0u}, {0x96e1d22du, 0x69871ee1u}, {}},
			{41, {0xc3a50ff0u, 0x5a3cf00fu}, {0xe196d22du, 0x87691ee1u}, {}}};
		for(auto& pattern : patterns)
		{
			auto editor = sysex::MdPatternEditor::fromDump(source);
			require(editor && editor->setSlot(pattern.slot) && editor->setLength(64), "pattern not editable");
			editor->clear();
			for(uint8_t track = 0; track < 2; ++track)
			{
				for(uint8_t step = 0; step < 64; ++step)
				{
					const auto mask = step < 32 ? pattern.low[track] : pattern.high[track];
					if((mask >> (step % 32)) & 1u)
						require(editor->setTrig(track, step, true), "trig refused");
				}
			}
			pattern.dump = editor->toDump();
			send(pattern.dump);
			advance(*hardware, md::g_samplerate);
		}

		// Kits: track 1's first eight synthesis parameters marked, the live Kit saved into slots 20 and 21
		const auto cc = [&](const uint8_t _index, const uint8_t _value)
		{
			const auto message = md::automation::encodeParameterChange(model, {0, 0, _index, _value}, 0);
			require(message.has_value(), "parameter not encodable");
			send(sysex::Message(message->begin(), message->end()));
			advance(*hardware, md::g_samplerate / 8);
		};
		const std::vector<std::pair<uint8_t, std::vector<uint8_t>>> kits{
			{20, {101, 13, 77, 42, 99, 5, 64, 120}}, {21, {102, 14, 78, 43, 100, 6, 65, 121}}};
		for(const auto& [slot, values] : kits)
		{
			for(uint8_t i = 0; i < values.size(); ++i)
				cc(i, values[i]);
			send(sysex::kitSave(model, slot));
			advance(*hardware, md::g_samplerate);
		}

		const auto ram = snapshot();
		const auto flash = hardware->copyFlashData();
		std::printf("MD flash image: %zu bytes\n", flash.size());
		for(const auto& pattern : patterns)
		{
			const auto low = find(ram, bigEndian(pattern.low[0], pattern.low[1]));
			const auto high = find(ram, bigEndian(pattern.high[0], pattern.high[1]));
			print("MD pattern " + std::to_string(pattern.slot) + " steps 1-32", low,
				find(flash, bigEndian(pattern.low[0], pattern.low[1])));
			print("MD pattern " + std::to_string(pattern.slot) + " steps 33-64", high,
				find(flash, bigEndian(pattern.high[0], pattern.high[1])));
			const auto payload = md::unpackMdPatternPayload(pattern.dump);
			require(payload.has_value(), "pattern payload unreadable");
			std::printf("MD pattern %u payload: %zu bytes (main %u, tail %u, extension %u)\n", pattern.slot,
				payload->size(), md::LivePatternLayout::MainSize, md::LivePatternLayout::TailSize,
				md::LivePatternLayout::ExtensionSize);
			const std::vector<uint8_t> main(payload->begin(), payload->begin() + md::LivePatternLayout::MainSize);
			const std::vector<uint8_t> tail(payload->begin() + md::LivePatternLayout::MainSize,
				payload->begin() + md::LivePatternLayout::MainSize + md::LivePatternLayout::TailSize);
			const std::vector<uint8_t> extension(payload->begin() + md::LivePatternLayout::MainSize
				+ md::LivePatternLayout::TailSize, payload->end());
			for(const auto at : low)
			{
				compare("MD pattern " + std::to_string(pattern.slot) + " main", ram, at, main);
				compare("MD pattern " + std::to_string(pattern.slot) + " tail after main", ram,
					at + md::LivePatternLayout::MainSize, tail);
				compare("MD pattern " + std::to_string(pattern.slot) + " whole payload", ram, at, *payload);
			}
			for(const auto at : high)
				compare("MD pattern " + std::to_string(pattern.slot) + " extension", ram, at, extension);
			print("MD pattern " + std::to_string(pattern.slot) + " tail anywhere", find(ram, tail), {});
		}
		for(const auto& [slot, values] : kits)
		{
			const auto hits = find(ram, values);
			print("MD Kit " + std::to_string(slot) + " parameters", hits, find(flash, values));
			const auto dump = exchange(sysex::kitRequest(model, slot), 0x52);
			std::printf("MD Kit %u dump: %zu bytes\n", slot, dump.size());
			for(const auto at : hits)
			{
				if(at >= 0x1a)
					compare("MD Kit " + std::to_string(slot) + " dump from $1a before", ram, at - 0x1a, dump);
			}
		}
	}

	// Where the Monomachine stores its 128 patterns and 128 Kits: two patterns written with marked trigs on track 1
	// and 2, a length and a Kit each, two Kits saved with marked parameters, then the marks searched for in the RAM
	// and the flash, and the length and Kit bytes checked where the decoded payload puts them
	void libraryRamMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		constexpr auto model = md::MachineModel::Monomachine;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		md::test::Monomachine machine(rom, path);
		auto& hardware = machine.hardware();
		auto& uc = hardware.getUC();
		constexpr uint32_t ramBegin = 0x00100000, ramEnd = 0x00400000;
		const auto snapshot = [&]
		{
			std::vector<uint8_t> bytes(ramEnd - ramBegin);
			for(uint32_t i = 0; i < bytes.size(); ++i)
			{
				const auto address = ramBegin + i;
				if(address < 0x00300000 || address >= 0x00310000)
					bytes[i] = uc.read8(address);
			}
			return bytes;
		};
		const auto find = [](const std::vector<uint8_t>& _haystack, const std::vector<uint8_t>& _needle)
		{
			std::vector<size_t> hits;
			auto it = _haystack.begin();
			while((it = std::search(it, _haystack.end(), _needle.begin(), _needle.end())) != _haystack.end())
			{
				hits.push_back(static_cast<size_t>(it - _haystack.begin()));
				++it;
			}
			return hits;
		};
		const auto print = [&](const std::string& _what, const std::vector<size_t>& _ram, const std::vector<size_t>& _flash)
		{
			std::string line;
			for(size_t i = 0; i < std::min<size_t>(_ram.size(), 12); ++i)
			{
				char text[16];
				std::snprintf(text, sizeof(text), " $%06x", static_cast<unsigned>(ramBegin + _ram[i]));
				line += text;
			}
			for(size_t i = 0; i < std::min<size_t>(_flash.size(), 8); ++i)
			{
				char text[24];
				std::snprintf(text, sizeof(text), " flash+$%06x", static_cast<unsigned>(_flash[i]));
				line += text;
			}
			std::printf("%s: %zu in the RAM, %zu in the flash:%s\n", _what.c_str(), _ram.size(), _flash.size(), line.c_str());
			std::fflush(stdout);
		};
		const auto bigEndian = [](const uint64_t _mask)
		{
			std::vector<uint8_t> bytes(8);
			for(size_t i = 0; i < 8; ++i)
				bytes[i] = static_cast<uint8_t>(_mask >> (56 - 8 * i));
			return bytes;
		};

		// Patterns: the current one made 64 steps long, its trigs on tracks 1 and 2 cleared, then marked
		struct Marked { uint8_t slot; uint8_t length; uint8_t kit; std::array<uint64_t, 2> trigs; };
		const std::vector<Marked> patterns{
			{40, 64, 5, {0xa5c3f00f96e1d22dull, 0x3c5a0ff069871ee1ull}},
			{41, 47, 9, {0xc3a50ff0e196d22dull & ((1ull << 47) - 1), 0x5a3cf00f87691ee1ull & ((1ull << 47) - 1)}}};
		const auto current = machine.status(sysex::StatusParameter::Pattern);
		const auto source = machine.readPattern(current);
		for(const auto& pattern : patterns)
		{
			auto editor = sysex::MmPatternEditor::fromDump(source);
			require(editor && editor->setSlot(pattern.slot) && editor->setLength(64) && editor->setKit(pattern.kit),
				"pattern not editable");
			for(uint8_t track = 0; track < 2; ++track)
			{
				for(uint8_t step = 0; step < 64; ++step)
					require(editor->setTrig(track, step, std::nullopt), "trig not cleared");
				for(uint8_t step = 0; step < 64; ++step)
				{
					if((pattern.trigs[track] >> step) & 1u)
						require(editor->setTrig(track, step, uint8_t{60}), "trig refused");
				}
			}
			require(editor->setLength(pattern.length), "length refused");
			machine.writePattern(editor->toDump());
		}

		// Kits: track 1's first eight synthesis parameters marked, the live Kit saved into slots 20 and 21
		const std::vector<std::pair<uint8_t, std::vector<uint8_t>>> kits{
			{20, {101, 13, 77, 42, 99, 5, 64, 120}}, {21, {102, 14, 78, 43, 100, 6, 65, 121}}};
		for(const auto& [slot, values] : kits)
		{
			for(uint8_t i = 0; i < values.size(); ++i)
			{
				const auto message = md::automation::encodeParameterChange(model, {0, 0, i, values[i]}, 0);
				require(message.has_value(), "parameter not encodable");
				machine.send(sysex::Message(message->begin(), message->end()));
				advance(hardware, md::g_samplerate / 8);
			}
			machine.send(sysex::kitSave(model, slot));
			advance(hardware, md::g_samplerate);
		}

		const auto ram = snapshot();
		const auto flash = hardware.copyFlashData();
		std::printf("MM flash image: %zu bytes\n", flash.size());
		for(const auto& pattern : patterns)
		{
			for(uint8_t track = 0; track < 2; ++track)
			{
				const auto hits = find(ram, bigEndian(pattern.trigs[track]));
				print("MM pattern " + std::to_string(pattern.slot) + " track " + std::to_string(track + 1) + " trigs", hits,
					find(flash, bigEndian(pattern.trigs[track])));
				// The decoded payload: amp trigs first (track * 8), the trigs at 6 * 48, the length at 1060, the Kit at 1062
				for(const auto at : hits)
				{
					const auto base = static_cast<int64_t>(at) - static_cast<int64_t>(track * 8);
					if(base < 0 || static_cast<size_t>(base) + 1063 > ram.size())
						continue;
					std::printf("  if $%06x is the amp trigs: length %u, Kit %u (expected %u and %u)\n",
						static_cast<unsigned>(ramBegin + base), ram[base + 1060], ram[base + 1062], pattern.length, pattern.kit);
				}
			}
			const auto dump = machine.readPattern(pattern.slot);
			const auto parsed = sysex::parseMmPatternDump(dump);
			std::printf("MM pattern %u dump: %zu bytes, length %u, Kit %u\n", pattern.slot, dump.size(),
				parsed ? parsed->length : 0, parsed ? parsed->kit : 0);
		}
		for(const auto& [slot, values] : kits)
		{
			const auto hits = find(ram, values);
			print("MM Kit " + std::to_string(slot) + " parameters", hits, find(flash, values));
			const auto dump = machine.exchange(sysex::kitRequest(model, slot), 0x52);
			const auto parsed = sysex::parseKitDump(model, dump);
			std::string machines;
			if(parsed)
			{
				for(const auto id : parsed->machines)
					machines += " " + std::to_string(id);
			}
			std::printf("MM Kit %u dump: %zu bytes, name \"%s\", machines%s\n", slot, dump.size(),
				parsed ? parsed->name.c_str() : "?", machines.c_str());
			for(const auto at : hits)
			{
				if(at < 0x11)
					continue;
				const auto base = at - 0x11;
				std::string name;
				for(size_t i = 0; i < 11; ++i)
					name += ram[base + i] >= 0x20 && ram[base + i] < 0x7f ? static_cast<char>(ram[base + i]) : '.';
				std::string ids;
				for(size_t track = 0; track < 6 && base + 0x1c1 + track < ram.size(); ++track)
					ids += " " + std::to_string(ram[base + 0x1c1 + track]);
				std::printf("  if $%06x is the Kit: name \"%s\", machines%s\n", static_cast<unsigned>(ramBegin + base),
					name.c_str(), ids.c_str());
			}
		}
	}

	void confirmMonomachine()
	{
		const auto* path = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
		if(!path)
			return;
		namespace sysex = md::automation::sysex;
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), std::string("cannot read ") + path);
		md::test::Monomachine machine(rom, path);
		const auto pattern = machine.status(sysex::StatusParameter::Pattern);
		auto editor = sysex::MmPatternEditor::fromDump(machine.readPattern(pattern));
		require(editor && editor->setLength(12), "pattern length refused");
		machine.writePattern(editor->toDump());
		trace(machine.hardware(), "MM stopped", {0x002bc287, 0x002bc29b, 0x002bc29f}, 1.0);
		panelTap(machine.hardware(), md::PanelControl::Play);
		trace(machine.hardware(), "MM", {0x002bc287, 0x002bc29b, 0x002bc29f}, 0.5);
		panelTap(machine.hardware(), md::PanelControl::Stop);
		trace(machine.hardware(), "MM after STOP", {0x002bc287, 0x002bc29b, 0x002bc29f}, 1.0);
		advance(machine.hardware(), md::g_samplerate);
		scanRunFlag(machine.hardware(), "MM");
	}
}

int main(const int _argc, const char* const* _argv)
{
	try
	{
		if(_argc > 1 && std::string(_argv[1]) == "--confirm")
		{
			confirmMachinedrum();
			confirmMonomachine();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--sync")
		{
			syncMachinedrum();
			syncMonomachine();
			return 0;
		}
		if(_argc > 2 && std::string(_argv[1]) == "--dump")
		{
			dumpMachines(_argv[2]);
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--livekit")
		{
			liveKitMachines();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--lfo")
		{
			probeLfo();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--tempo")
		{
			tempoMachines(false);
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--tempo-code")
		{
			tempoMachines(true);
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--write-cost")
		{
			writeCostMachinedrum();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--pattern-ram")
		{
			patternRamMachinedrum();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--pattern-ext")
		{
			patternExtensionMachinedrum();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--library-ram")
		{
			libraryRamMachinedrum();
			return 0;
		}
		if(_argc > 1 && std::string(_argv[1]) == "--library-ram-mm")
		{
			libraryRamMonomachine();
			return 0;
		}
		probe("GEARMULATOR_MD_FIRMWARE_BIN", md::MachineModel::Machinedrum);
		probe("GEARMULATOR_MM_FIRMWARE_BIN", md::MachineModel::Monomachine);
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdPlayheadProbe: FAIL %s\n", _error.what());
		return 1;
	}
}
