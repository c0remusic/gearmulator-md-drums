// The Hit screen's preview (ticket 28 of the editor map), mdDrums::HitPreview:
// - a Track's Hit rendered for a Kit is, column for column, the capture a Device just loaded with that Kit makes of its
//   first Hit, for Tracks of several machines, and for a machine just chosen, its SYN1-8 at the machine's defaults as
//   the editor takes them before the Device reports them;
// - the same request renders the same Hit twice;
// - through a Client: only the latest request waiting is rendered, the result comes back with its number, the next
//   engine is built ahead, and the engine and the OS go after the idle time;
// - what a render costs (building, rendering, the process's memory for an engine).
// usage: mdDrumsHitPreviewTest

#include "mdDrumsDevice.h"
#include "mdDrumsEngine.h"
#include "mdDrumsHitPreview.h"
#include "mdDrumsMessages.h"

#include "Firmware.h"

#include "synthLib/midiTypes.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
	using Capture = mdDrums::Telemetry::Capture;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// The process's private memory, in MB
	double privateMb()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS_EX counters{};
		GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
		return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
#else
		return 0.0;
#endif
	}

	double msSince(const std::chrono::steady_clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start).count();
	}

	// Columns that differ between two captures of a full Hit
	int differing(const Capture& _a, const Capture& _b)
	{
		require(_a.count == mdDrums::Telemetry::CaptureColumns && _b.count == mdDrums::Telemetry::CaptureColumns,
			"a capture is not full: " + std::to_string(_a.count) + " and " + std::to_string(_b.count) + " columns");
		int differ = 0;
		for(int c = 0; c < _a.count; ++c)
			differ += _a.columns[static_cast<size_t>(c)] != _b.columns[static_cast<size_t>(c)];
		return differ;
	}

	bool audible(const Capture& _capture)
	{
		for(int c = 0; c < _capture.count; ++c)
			if(_capture.columns[static_cast<size_t>(c)][1] > 100)
				return true;
		return false;
	}

	synthLib::SMidiEvent sysex(std::vector<uint8_t> _bytes)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex.assign(_bytes.begin(), _bytes.end());
		return event;
	}

	// A Device just loaded (factory Kit 1); _events at its first sample, then 0.85 s rendered. Its Telemetry's capture
	// of _track's first Hit, and the Kit it then holds.
	struct Live
	{
		Capture capture;
		md::automation::sysex::MdKit kit;
	};

	Live live(const std::vector<uint8_t>& _flash, const std::vector<synthLib::SMidiEvent>& _events, const int _track)
	{
		synthLib::DeviceCreateParams params;
		params.romData = _flash;
		auto telemetry = std::make_shared<mdDrums::Telemetry>();
		mdDrums::Device device(params, telemetry);
		constexpr size_t block = 512;
		std::vector<std::vector<float>> buffers(mdDrums::Device::OutputCount, std::vector<float>(block));
		synthLib::TAudioOutputs outputs{};
		for(size_t c = 0; c < buffers.size(); ++c)
			outputs[c] = buffers[c].data();
		std::vector<synthLib::SMidiEvent> out;
		auto events = _events;
		for(size_t done = 0; done < 37500; done += block)
		{
			device.process({}, outputs, block, events, out);
			events.clear();
		}
		Live result;
		require(telemetry->readCapture(_track, false, result.capture), "the Device captured no Hit");
		result.kit = device.kit();
		return result;
	}

	synthLib::SMidiEvent note(const int _track, const uint8_t _velocity)
	{
		return {synthLib::MidiEventSource::Host, static_cast<uint8_t>(synthLib::M_NOTEON),
			static_cast<uint8_t>(mdDrums::Device::FirstNote + _track), _velocity};
	}

	Capture preview(const md::fw::FlashOs& _os, const md::fw::RomBank& _bank, const mdDrums::HitPreview::Request& _request)
	{
		mdDrums::Engine engine(_os, _bank, false);
		Capture capture;
		mdDrums::HitPreview::render(engine, _request, capture);
		return capture;
	}

	// Waits for a Client's result, at most _seconds
	bool waitResult(mdDrums::HitPreview::Client& _client, mdDrums::HitPreview::Result& _result, const double _seconds)
	{
		const auto start = std::chrono::steady_clock::now();
		while(msSince(start) < _seconds * 1000.0)
		{
			if(_client.take(_result))
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return false;
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	const auto flash = mdDrums::Engine::findFlashImage({});
	if(flash.empty())
	{
		std::cout << "mdDrumsHitPreviewTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
		return 77;
	}
	try
	{
		const auto os = md::fw::loadFirmwareFromFlash(flash);
		const auto bank = md::fw::loadRomBankFromFlash(flash);
		const auto factory = mdDrums::messages::factoryKit(flash);
		require(factory.has_value(), "no factory Kit in the flash image");

		// The Device's first Hit, Track by Track of factory Kit 1, at two velocities
		for(const auto& [track, velocity] : std::vector<std::pair<int, uint8_t>>{{0, 100}, {4, 127}, {9, 60}, {15, 110}})
		{
			const auto device = live(flash, {note(track, velocity)}, track);
			mdDrums::HitPreview::Request request;
			request.kit = device.kit;
			request.track = static_cast<uint8_t>(track);
			request.velocity = velocity;
			const auto rendered = preview(os, bank, request);
			const auto differ = differing(device.capture, rendered);
			std::printf("Track %d (machine %d) at velocity %d: %d of %d columns differ from the Device's first Hit\n",
				track + 1, device.kit.machine(static_cast<uint8_t>(track)), velocity, differ, rendered.count);
			require(audible(rendered), "the preview is silent");
			require(differ == 0, "the preview is not the Device's first Hit");
			require(rendered.velocity == velocity && rendered.id == 1, "the preview's velocity or id");
		}

		// A machine just chosen: the request still holds the previous machine's SYN1-8
		{
			mdDrums::Engine probe(os, bank, false);
			const auto& machines = probe.machines();
			const auto find = [&](const char* _name)
			{
				for(const auto& machine : machines)
					if(machine.name == _name)
						return machine;
				throw std::runtime_error(std::string("no machine ") + _name);
			};
			for(const auto* name : {"EFMSD", "ROM01", "P-IMT"})
			{
				const auto machine = find(name);
				constexpr int track = 2;
				const auto device = live(flash, {sysex({0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x5b, track,
					static_cast<uint8_t>(machine.id & 0x7f), static_cast<uint8_t>(machine.id >> 7), 0xf7}), note(track, 100)}, track);
				require(device.kit.machine(track) == machine.id, std::string("the Device did not take ") + name);
				mdDrums::HitPreview::Request request;
				request.kit = device.kit;
				request.track = track;
				request.velocity = 100;
				// The machine's defaults, as the editor takes them before the Device reports them (HitView)
				for(int p = 0; p < 8; ++p)
					request.kit.parameters[track][p] = machine.defaults[p];
				const auto withDefaults = differing(device.capture, preview(os, bank, request));
				// Factory Kit 1's SYN1-8 under the new machine, as the editor still has them meanwhile
				for(int p = 0; p < 8; ++p)
					request.kit.parameters[track][p] = factory->parameters[track][p];
				const auto withOld = differing(device.capture, preview(os, bank, request));
				std::printf("%s chosen on Track 3: %d columns differ with its defaults, %d with the SYN1-8 before it\n", name,
					withDefaults, withOld);
				require(withDefaults == 0, "a machine just chosen does not play its defaults");
			}
		}

		// The same request twice, on two fresh engines
		mdDrums::HitPreview::Request request;
		request.kit = *factory;
		request.track = 0;
		request.velocity = 100;
		{
			const auto first = preview(os, bank, request);
			const auto second = preview(os, bank, request);
			require(differing(first, second) == 0, "the same request renders two different Hits");
			std::printf("the same request renders the same Hit twice\n");
		}

		// What a render costs, on this thread
		{
			const auto before = privateMb();
			auto start = std::chrono::steady_clock::now();
			auto engine = std::make_unique<mdDrums::Engine>(os, bank, false);
			const auto built = msSince(start);
			const auto memory = privateMb() - before;
			Capture capture;
			start = std::chrono::steady_clock::now();
			mdDrums::HitPreview::render(*engine, request, capture);
			const auto rendered = msSince(start);
			std::printf("an engine without master effects: %.1f MB, built in %.1f ms; a Hit rendered in %.1f ms\n", memory,
				built, rendered);
		}

		// Through a Client: the latest request only, the engine built ahead, then freed after the idle time
		{
			constexpr double idle = 0.5;
			mdDrums::HitPreview::Client client({}, idle);
			std::vector<uint32_t> ids;
			for(uint8_t velocity : {40, 80, 120})
			{
				auto r = request;
				r.velocity = velocity;
				ids.push_back(client.request(std::move(r)));
			}
			mdDrums::HitPreview::Result result;
			std::vector<uint32_t> served;
			while(waitResult(client, result, 5.0))
			{
				served.push_back(result.id);
				std::printf("  result %u: velocity %d, %.1f ms\n", result.id, result.capture.velocity, result.renderMs);
				if(result.id == ids.back())
					break;
			}
			require(!served.empty() && served.back() == ids.back(), "the latest request was not rendered");
			require(served.size() < ids.size(), "every request waiting was rendered, not the latest only");
			require(result.capture.velocity == 120 && result.track == 0, "the result is not the latest request's");
			auto expected = request;
			expected.velocity = 120;
			require(differing(result.capture, preview(os, bank, expected)) == 0, "the Client's Hit is not the render's");

			// The next engine is built while nothing waits (each render built its own until then): the next render does not
			// build one
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			const auto builtAhead = client.enginesBuilt();
			require(builtAhead == served.size() + 1, "no engine built ahead: " + std::to_string(builtAhead) + " built for "
				+ std::to_string(served.size()) + " renders");
			client.request(request);
			require(waitResult(client, result, 5.0), "no result for a request");
			std::printf("rendered on an engine built ahead: %.1f ms\n", result.renderMs);

			// Idle: the engine and the OS go
			const auto idleStart = std::chrono::steady_clock::now();
			while(client.holdsEngine() && msSince(idleStart) < 5000.0)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			std::printf("engine and OS freed %.0f ms after the last request (idle time %.0f ms)\n", msSince(idleStart),
				idle * 1000.0);
			require(!client.holdsEngine(), "the engine stays after the idle time");

			// And come back with the next request
			client.request(request);
			require(waitResult(client, result, 5.0) && result.capture.id, "no result after the idle time");
			require(differing(result.capture, preview(os, bank, request)) == 0, "the Hit after the idle time differs");
			std::printf("after the idle time: OS read again, engine built, Hit rendered in %.1f ms\n", result.renderMs);
		}

		std::cout << "mdDrumsHitPreviewTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsHitPreviewTest: FAIL: " << _error.what() << '\n';
		return 1;
	}
}
