// MD Drums' Device driven by MIDI bytes alone, through synthLib::Plugin as the plug-in drives it: it boots with factory
// Kit 1, plays notes 36-51 and drops the others, takes the Machinedrum's CCs and SysEx from the editor and ignores the
// host's, answers ASSIGN MACHINE with the new machine's SYN1-8 as CCs, plays Mute, Solo and Out, keeps its Kit and mixer
// across a state round trip, and renders with notes and CCs without allocating. It reports the note-to-sound latency
// at 44.1 and 48 kHz, and fills the Telemetry the editor reads (peaks, Hits) for at most 1 % of one core.

#include "mdDrumsDevice.h"

#include "mdProtocol/mdautomation.h"

#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	thread_local bool g_countAllocations = false;
	thread_local size_t g_allocations = 0;
}

void* operator new(const std::size_t _size)
{
	if(g_countAllocations)
		++g_allocations;
	if(void* const result = std::malloc(_size ? _size : 1))
		return result;
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size) { return ::operator new(_size); }

void* operator new(const std::size_t _size, const std::align_val_t _alignment)
{
	if(g_countAllocations)
		++g_allocations;
#if defined(_MSC_VER)
	if(void* const result = _aligned_malloc(_size ? _size : 1, static_cast<std::size_t>(_alignment)))
		return result;
#else
	void* result = nullptr;
	if(posix_memalign(&result, static_cast<std::size_t>(_alignment), _size ? _size : 1) == 0)
		return result;
#endif
	throw std::bad_alloc();
}

void* operator new[](const std::size_t _size, const std::align_val_t _alignment) { return ::operator new(_size, _alignment); }
void operator delete(void* const _pointer) noexcept { std::free(_pointer); }
void operator delete[](void* const _pointer) noexcept { std::free(_pointer); }
void operator delete(void* const _pointer, std::size_t) noexcept { std::free(_pointer); }
void operator delete[](void* const _pointer, std::size_t) noexcept { std::free(_pointer); }
#if defined(_MSC_VER)
void operator delete(void* const _pointer, std::align_val_t) noexcept { _aligned_free(_pointer); }
#else
void operator delete(void* const _pointer, std::align_val_t) noexcept { std::free(_pointer); }
#endif
void operator delete[](void* const _pointer, const std::align_val_t _alignment) noexcept { ::operator delete(_pointer, _alignment); }
void operator delete(void* const _pointer, std::size_t, const std::align_val_t _alignment) noexcept { ::operator delete(_pointer, _alignment); }
void operator delete[](void* const _pointer, std::size_t, const std::align_val_t _alignment) noexcept { ::operator delete(_pointer, _alignment); }

namespace
{
	namespace sysex = md::automation::sysex;
	namespace machinedrum = md::automation::machinedrum;
	using synthLib::MidiEventSource;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	std::vector<uint8_t> readFlash()
	{
		return mdDrums::Engine::findFlashImage({});
	}

	// The Device behind synthLib::Plugin at a host rate, with 18 output buffers of a block each
	struct Rig
	{
		static constexpr uint32_t Block = 512;

		Rig(const std::vector<uint8_t>& _flash, const float _rate, const bool _telemetry = true) : hostRate(_rate)
		{
			synthLib::DeviceCreateParams params;
			params.romData = _flash;
			if(_telemetry)
				telemetry = std::make_shared<mdDrums::Telemetry>();
			device = std::make_unique<mdDrums::Device>(params, telemetry);
			plugin = std::make_unique<synthLib::Plugin>(device.get(), [](synthLib::Device*) {});
			plugin->reserveMidiEventCapacity();
			plugin->setHostSamplerate(_rate, 44100.0f);
			plugin->setBlockSize(Block);
			for(size_t c = 0; c < buffers.size(); ++c)
			{
				buffers[c].resize(Block);
				outputs[c] = buffers[c].data();
			}
		}

		void send(const MidiEventSource _source, std::initializer_list<uint8_t> _bytes, const uint32_t _offset = 0)
		{
			const std::vector<uint8_t> bytes(_bytes);
			send(_source, bytes, _offset);
		}

		void send(const MidiEventSource _source, const std::vector<uint8_t>& _bytes, const uint32_t _offset = 0)
		{
			synthLib::SMidiEvent event(_source);
			require(event.assignRawData(_bytes.data(), _bytes.size(), _source, _offset), "no MIDI event");
			plugin->addMidiEvent(event);
		}

		// One host block; the peak of Main and of each Out
		void process()
		{
			plugin->process(inputs, outputs, Block, 120.0f, 0.0f, false);
			std::vector<synthLib::SMidiEvent> out;
			plugin->getMidiOut(out);
			midiOut.insert(midiOut.end(), out.begin(), out.end());
		}

		// A second of host blocks; the peak of output _channel (0-1 Main, 2-17 Out 01-16)
		float listen(const size_t _channel, const uint32_t _blocks = 0)
		{
			float peak = 0;
			const auto blocks = _blocks ? _blocks : static_cast<uint32_t>(rate() / Block);
			for(uint32_t b = 0; b < blocks; ++b)
			{
				process();
				for(const auto value : buffers[_channel])
					peak = std::max(peak, std::abs(value));
				if(_channel == 0)
					for(const auto value : buffers[1])
						peak = std::max(peak, std::abs(value));
			}
			return peak;
		}

		void settle() { listen(0, static_cast<uint32_t>(rate() * 2 / Block)); }
		float rate() const { return hostRate; }

		std::shared_ptr<mdDrums::Telemetry> telemetry;
		std::unique_ptr<mdDrums::Device> device;
		std::unique_ptr<synthLib::Plugin> plugin;
		std::array<std::vector<float>, 18> buffers;
		synthLib::TAudioOutputs outputs{};
		synthLib::TAudioInputs inputs{};
		std::vector<synthLib::SMidiEvent> midiOut;
		float hostRate = 44100.0f;
	};

	std::vector<uint8_t> cc(const uint8_t _page, const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		const auto message = md::automation::encodeParameterChange(md::MachineModel::Machinedrum,
			{_page, _track, _index, _value}, 0);
		require(message.has_value(), "no CC");
		return {(*message)[0], (*message)[1], (*message)[2]};
	}

	void behaviour(const std::vector<uint8_t>& _flash)
	{
		Rig rig(_flash, 44100.0f);
		const auto& kit = rig.device->kit();
		std::printf("boots with Kit 1 %s, track 1 %u, track 9 chokes track %u\n", kit.displayName().c_str(),
			kit.machine(0), kit.chokes[8] + 1);
		require(kit.displayName() == "TRX UW" && kit.machine(0) == 28 && kit.chokes[8] == 9, "not factory Kit 1");
		rig.settle();

		rig.send(MidiEventSource::Host, {0x90, 36, 100});
		const auto loud = rig.listen(0);
		rig.settle();
		rig.send(MidiEventSource::Host, {0x9f, 60, 100});
		const auto other = rig.listen(0);
		std::printf("note 36: peak %.4f; note 60: peak %.4f\n", loud, other);
		require(loud > 0.01f && other == 0.0f, "notes 36-51 only");

		// The host's CCs count for nothing; the editor's set the Kit
		rig.send(MidiEventSource::Host, cc(machinedrum::Level, 0, 0, 0));
		rig.settle();
		require(rig.device->kit().levels[0] == kit.levels[0], "a CC from the host reached the Kit");
		rig.send(MidiEventSource::Editor, cc(machinedrum::Level, 0, 0, 0));
		rig.settle();
		rig.send(MidiEventSource::Host, {0x90, 36, 100});
		require(rig.device->kit().levels[0] == 0 && rig.listen(0) == 0.0f, "the editor's level CC did not silence track 1");
		rig.send(MidiEventSource::Editor, cc(machinedrum::Level, 0, 0, 100));

		// ASSIGN MACHINE: TRX-BD on track 1, its SYN1-8 defaults said back as CCs
		rig.midiOut.clear();
		const auto assign = sysex::assignMachine(md::MachineModel::Machinedrum, 0, 16);
		rig.send(MidiEventSource::Editor, *assign);
		rig.process();
		size_t synCcs = 0;
		for(const auto& event : rig.midiOut)
		{
			const auto change = md::automation::decodeParameterChange(md::MachineModel::Machinedrum,
				{event.a, event.b, event.c}, 0);
			if(change && change->page == machinedrum::Synthesis && change->track == 0
				&& change->value == rig.device->kit().parameters[0][change->index])
				++synCcs;
		}
		std::printf("$5B TRX-BD on track 1: machine %u, %zu SYN CCs back\n", rig.device->kit().machine(0), synCcs);
		require(rig.device->kit().machine(0) == 16 && synCcs == 8, "ASSIGN MACHINE did not set and report the machine");

		// Master effects, LFO, Link and Choke land in the Kit
		rig.send(MidiEventSource::Editor, *sysex::masterEffectChange(sysex::MasterEffect::Echo, 2, 99));
		rig.send(MidiEventSource::Editor, *sysex::lfoChange(3, 2, md::LfoSettings::Random));
		rig.send(MidiEventSource::Editor, *sysex::linkChange(4, 5));
		rig.send(MidiEventSource::Editor, *sysex::chokeChange(8, sysex::MdKit::Off));
		rig.process();
		const auto& changed = rig.device->kit();
		require(changed.masterEffectValues()[0][2] == 99, "$5D did not reach the Kit");
		require(changed.lfo(3).shape1 == md::LfoSettings::Random, "$62 did not reach the Kit");
		require(changed.links[4] == 5 && changed.chokes[8] == sysex::MdKit::Off, "$65 or $66 did not reach the Kit");

		// Mute drops the Hits, Solo silences the others, Out takes a track off Main onto its own output
		rig.settle();
		rig.send(MidiEventSource::Editor, cc(machinedrum::Mute, 0, 0, 1));
		rig.send(MidiEventSource::Host, {0x90, 36, 100});
		const auto muted = rig.listen(0);
		rig.send(MidiEventSource::Editor, cc(machinedrum::Mute, 0, 0, 0));
		rig.send(MidiEventSource::Editor, mdDrums::messages::solo(1, true).message());
		rig.send(MidiEventSource::Host, {0x90, 36, 100});
		const auto soloElsewhere = rig.listen(0);
		rig.send(MidiEventSource::Editor, mdDrums::messages::solo(1, false).message());
		rig.send(MidiEventSource::Editor, mdDrums::messages::out(0, true).message());
		rig.settle();
		rig.send(MidiEventSource::Host, {0x90, 36, 100});
		float mainPeak = 0, outPeak = 0;
		for(uint32_t b = 0; b < 86; ++b)
		{
			rig.process();
			for(size_t i = 0; i < Rig::Block; ++i)
			{
				mainPeak = std::max({mainPeak, std::abs(rig.buffers[0][i]), std::abs(rig.buffers[1][i])});
				outPeak = std::max(outPeak, std::abs(rig.buffers[2][i]));
			}
		}
		std::printf("muted %.4f, another track soloed %.4f, on Out 01: Main %.4f, Out 01 %.4f\n", muted, soloElsewhere,
			mainPeak, outPeak);
		require(muted == 0.0f && soloElsewhere == 0.0f, "Mute or Solo let a Hit through");
		require(mainPeak == 0.0f && outPeak > 0.01f, "Out did not move track 1 off Main");

		// The state: Kit and mixer, read back by another Device
		std::vector<uint8_t> state;
		require(rig.plugin->getState(state, synthLib::StateTypeGlobal), "no state");
		Rig restored(_flash, 44100.0f);
		require(restored.plugin->setState(state), "the state was refused");
		require(restored.device->kit() == rig.device->kit() && restored.device->mixer() == rig.device->mixer(),
			"the state did not bring back the Kit and the mixer");
		std::printf("state: %zu bytes, Kit and mixer restored\n", state.size());

		// A Kit dump from the editor: the Device plays that Kit
		auto dumped = restored.device->kit();
		dumped.levels[2] = 77;
		restored.send(MidiEventSource::Editor, sysex::mdKitDump(dumped, 12));
		restored.process();
		require(restored.device->kit() == dumped, "a Kit dump did not become the Kit played");
	}

	// Every host parameter, as the Controller sends it, lands in the Device where the state reads it back
	void parameters(const std::vector<uint8_t>& _flash)
	{
		namespace pages = mdDrums::messages::pages;
		Rig rig(_flash, 44100.0f);
		struct Page { uint8_t page; uint8_t indices; std::vector<int> values; };
		const std::vector<Page> table = {
			{pages::Synthesis, 8, {0, 1, 64, 127}}, {pages::Effects, 8, {0, 1, 64, 127}},
			{pages::Routing, 8, {0, 1, 64, 127}}, {pages::Level, 1, {0, 99, 127}}, {pages::Mute, 1, {1, 0}},
			{pages::Machine, 1, {16, 37, 128, 176, 28}}, {pages::Lfo, 5, {0, 1, 2}}, {pages::Mixer, 2, {1, 0}},
			{pages::Master, 32, {0, 63, 127}}};
		size_t checked = 0;
		for(const auto& page : table)
		{
			for(const uint8_t track : {uint8_t{0}, uint8_t{7}, uint8_t{15}})
			{
				if(page.page == pages::Master && track != 0)
					continue;
				for(uint8_t index = 0; index < page.indices; ++index)
				{
					for(const auto value : page.values)
					{
						const auto message = mdDrums::messages::parameterMessage(page.page, track, index, value);
						require(message.has_value(), "no message for a host parameter");
						rig.send(MidiEventSource::Editor, message->message());
						rig.process();
						const mdDrums::messages::State state{rig.device->kit(), rig.device->mixer()};
						const auto read = mdDrums::messages::parameterValue(state, page.page, track, index);
						require(read && *read == value, "page " + std::to_string(page.page) + " track "
							+ std::to_string(track + 1) + " index " + std::to_string(index) + ": sent "
							+ std::to_string(value) + ", read " + (read ? std::to_string(*read) : std::string("nothing")));
						++checked;
					}
				}
			}
		}
		require(!mdDrums::messages::parameterMessage(pages::Machine, 0, 0, 6), "a machine the Machinedrum lacks was sent");
		std::printf("%zu host parameter values sent and read back from the Device\n", checked);
	}

	// Notes and CCs, inline SysEx included, on a prepared Plugin: no heap allocation
	void allocations(const std::vector<uint8_t>& _flash)
	{
		Rig rig(_flash, 48000.0f);
		rig.settle();
		const auto lfo = sysex::lfoChange(2, 0, 5);
		synthLib::SMidiEvent note(MidiEventSource::Host, 0x90, 36, 100, 17);
		synthLib::SMidiEvent level(MidiEventSource::Editor, 0xb0, 8, 90, 100);
		synthLib::SMidiEvent lfoEvent(MidiEventSource::Editor);
		lfoEvent.assignRawData(lfo->data(), lfo->size(), MidiEventSource::Editor, 300);
		const auto count = [&](const bool _note, const bool _level, const bool _lfo)
		{
			g_allocations = 0;
			g_countAllocations = true;
			if(_note)
				rig.plugin->addMidiEvent(note);
			if(_level)
				rig.plugin->addMidiEvent(level);
			if(_lfo)
				rig.plugin->addMidiEvent(lfoEvent);
			rig.plugin->process(rig.inputs, rig.outputs, Rig::Block, 120.0f, 0.0f, false);
			g_countAllocations = false;
			return g_allocations;
		};
		const auto plain = count(false, false, false), withNote = count(true, false, false);
		const auto withLevel = count(false, true, false), withLfo = count(false, false, true);
		std::printf("allocations: plain %zu, note %zu, CC %zu, LFO message %zu\n", plain, withNote, withLevel, withLfo);
		for(uint32_t b = 0; b < 32; ++b)
		{
			const auto allocations = count(true, true, true);
			require(allocations == 0, "rendering allocated " + std::to_string(allocations) + " times");
		}
		std::printf("32 blocks with a note, a CC and an LFO message each: no allocation\n");
	}

	// The Telemetry the editor reads (ticket 16): a note's Track and Main peak while the other Tracks stay silent, and
	// its Hit is counted; a muted Track's Hit is not. Then what writing it costs, 16 Tracks struck every 64th of a
	// second: the Device's own peak pass over 18 outputs, timed alone (at most 1 % of one core), and the whole Device
	// with and without it, side by side, for information.
	void telemetry(const std::vector<uint8_t>& _flash)
	{
		{
			Rig rig(_flash, 44100.0f);
			rig.settle();
			auto& t = *rig.telemetry;
			for(int output = 0; output < mdDrums::Telemetry::OutputCount; ++output)
				t.takePeak(output);
			const auto before = t.hits(0);
			rig.send(MidiEventSource::Host, {0x90, 36, 100});
			rig.listen(0, 20);
			const auto main = std::max(t.takePeak(0), t.takePeak(1)), own = t.takePeak(2);
			float others = 0;
			for(int output = 3; output < mdDrums::Telemetry::OutputCount; ++output)
				others = std::max(others, t.takePeak(output));
			std::printf("telemetry: note 36, Main peak %.4f, Out 01 %.4f, the other Outs %.4f, Hits of track 1 %u -> %u\n",
				main, own, others, before, t.hits(0));
			require(main > 0.01f && own > 0.01f && others == 0.0f, "the peaks do not follow the note");
			require(t.hits(0) == before + 1, "the Hit was not counted");
			rig.send(MidiEventSource::Editor, cc(machinedrum::Mute, 0, 0, 1));
			rig.send(MidiEventSource::Host, {0x90, 36, 100});
			rig.listen(0, 4);
			require(t.hits(0) == before + 1, "a muted Track's Hit was counted");
		}

		// The peak pass alone: what Device::render adds after the engine, over 18 outputs of 512 samples
		{
			std::vector<std::vector<float>> outputs(mdDrums::Telemetry::OutputCount, std::vector<float>(Rig::Block));
			uint32_t random = 0x2468aceu;
			for(auto& output : outputs)
				for(auto& value : output)
				{
					random ^= random << 13; random ^= random >> 17; random ^= random << 5;
					value = static_cast<float>(static_cast<int32_t>(random)) / 2147483648.0f;
				}
			mdDrums::Telemetry t;
			constexpr int blocks = 20000;
			const auto start = std::chrono::steady_clock::now();
			for(int b = 0; b < blocks; ++b)
				for(int output = 0; output < mdDrums::Telemetry::OutputCount; ++output)
				{
					float peak = 0.0f;
					for(const auto value : outputs[static_cast<size_t>(output)])
						peak = std::max(peak, std::abs(value));
					t.raisePeak(output, peak * (1.0f + static_cast<float>(b & 1)));
					if(output == 0 && (b & 63) == 0)
						t.takePeak(0);
				}
			const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			const auto audio = blocks * static_cast<double>(Rig::Block) / 44100.0;
			std::printf("telemetry: the peak pass costs %.3f %% of one core\n", seconds / audio * 100.0);
			require(seconds / audio <= 0.01, "the telemetry costs more than 1 % of one core");
		}

		// The whole Device with and without it, block by block side by side
		{
			Rig with(_flash, 44100.0f, true), without(_flash, 44100.0f, false);
			with.settle();
			without.settle();
			double withSeconds = 0, withoutSeconds = 0;
			constexpr uint32_t blocks = 700;
			for(uint32_t b = 0; b < blocks; ++b)
			{
				for(auto* rig : {&with, &without})
				{
					rig->send(MidiEventSource::Host, {0x90, static_cast<uint8_t>(36 + b % 16), 100});
					const auto start = std::chrono::steady_clock::now();
					rig->process();
					const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
					(rig == &with ? withSeconds : withoutSeconds) += seconds;
				}
			}
			const auto audio = blocks * static_cast<double>(Rig::Block) / 44100.0;
			std::printf("telemetry: the Device, 16 tracks struck, %.2f %% of one core with it, %.2f %% without\n",
				withSeconds / audio * 100.0, withoutSeconds / audio * 100.0);
		}
	}

	// Note to sound through the Plugin: TRX-BD on track 1, 48 notes at pseudo-random samples of 512-sample host blocks,
	// to the first nonzero sample
	void latency(const std::vector<uint8_t>& _flash, const float _rate)
	{
		Rig rig(_flash, _rate);
		rig.send(MidiEventSource::Editor, *sysex::assignMachine(md::MachineModel::Machinedrum, 0, 16));
		rig.send(MidiEventSource::Editor, cc(machinedrum::Level, 0, 0, 100));
		uint32_t random = 0x16305eedu;
		int minimum = 1 << 30, maximum = 0;
		double sum = 0;
		for(int note = 0; note < 48; ++note)
		{
			rig.listen(0, static_cast<uint32_t>(_rate / Rig::Block));
			random ^= random << 13; random ^= random >> 17; random ^= random << 5;
			const auto offset = random % Rig::Block;
			rig.send(MidiEventSource::Host, {0x90, 36, 100}, offset);
			int first = -1;
			for(uint32_t b = 0, position = 0; b < 8 && first < 0; ++b, position += Rig::Block)
			{
				rig.process();
				for(uint32_t s = 0; s < Rig::Block && first < 0; ++s)
					if(rig.buffers[0][s] != 0.0f || rig.buffers[1][s] != 0.0f)
						first = static_cast<int>(position + s) - static_cast<int>(offset);
			}
			require(first >= 0, "a note stayed silent");
			minimum = std::min(minimum, first);
			maximum = std::max(maximum, first);
			sum += first;
		}
		std::printf("note to sound at %.0f Hz (TRXBD, 48 notes): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms),"
			" reported %u\n", _rate, minimum, maximum, sum / 48, sum / 48 * 1000.0 / _rate, maximum - minimum,
			(maximum - minimum) * 1000.0 / _rate, rig.plugin->getLatencyMidiToOutput());
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = readFlash();
	if(flash.empty())
	{
		std::printf("mdDrumsDeviceTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n");
		return 77;
	}
	try
	{
		behaviour(flash);
		parameters(flash);
		allocations(flash);
		latency(flash, 44100.0f);
		latency(flash, 48000.0f);
		telemetry(flash);
		std::printf("mdDrumsDeviceTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::printf("mdDrumsDeviceTest: FAIL: %s\n", _error.what());
		return 1;
	}
}
