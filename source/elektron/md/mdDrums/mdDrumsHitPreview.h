#pragma once

#include "mdDrumsTelemetry.h"

#include "mdProtocol/mdkit.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace md::fw
{
	struct FlashOs;
	struct RomBank;
}

namespace mdDrums
{
	class Engine;

	// The Hit screen's and the machine browser's preview (tickets 12 and 28 of the editor map): a Track's Hit rendered on
	// an engine of its own, without the master effects, and captured as the Device captures a Hit (Telemetry::Capture: the
	// Track's own output from where its Hit sounds, min/max columns of 8 samples, 0.8 s). Every render starts on an engine
	// nothing has rendered yet, so that the same Kit, Track and velocity always give the same Hit: the one a Device just
	// loaded with that Kit plays first.
	// One per process, shared by the editors open (Client): a thread of its own renders the latest request of each,
	// builds the next engine while none waits, and frees it, with the OS it was built from, after an idle time without a
	// request (IdleSeconds). Nothing here runs on the audio thread.
	class HitPreview
	{
	public:
		static constexpr double IdleSeconds = 30.0;

		struct Request
		{
			md::automation::sysex::MdKit kit;
			uint8_t track = 0;
			uint8_t velocity = 100;
		};

		struct Result
		{
			uint32_t id = 0;			// the request's, from Client::request
			uint8_t track = 0;
			Telemetry::Capture capture;	// its id 0 when no engine could be built (no flash image, a fault)
			double renderMs = 0.0;		// building the engine unless it was built ahead, then rendering
		};

		// One render: _request's Kit into _engine, which nothing has rendered yet, its Track struck at once, the Hit
		// captured into _capture (id 1)
		static void render(Engine& _engine, const Request& _request, Telemetry::Capture& _capture);

		// An editor's connection to the process's preview, used on its message thread
		class Client
		{
		public:
			// _folders: where else to look for the flash image (Engine::findFlashImage). The first client's folders and
			// _idleSeconds serve until the last client goes.
			explicit Client(std::vector<std::filesystem::path> _folders, double _idleSeconds = IdleSeconds);
			~Client();

			Client(const Client&) = delete;
			Client& operator=(const Client&) = delete;

			// Replaces this client's request still waiting, if any; returns its number, from 1
			uint32_t request(Request _request);
			// The latest result since the last take, swapped into _result; false when none came
			bool take(Result& _result);

			// For the tests: whether the preview holds an engine or the OS now, and how many engines it built
			bool holdsEngine() const;
			uint32_t enginesBuilt() const;

		private:
			std::shared_ptr<HitPreview> m_preview;
			uint64_t m_id = 0;
		};

		~HitPreview();

		HitPreview(const HitPreview&) = delete;
		HitPreview& operator=(const HitPreview&) = delete;

	private:
		using Clock = std::chrono::steady_clock;

		struct Slot
		{
			std::optional<Request> pending;
			uint32_t pendingId = 0;
			uint32_t ids = 0;
			std::optional<Result> result;
		};

		HitPreview(std::vector<std::filesystem::path> _folders, double _idleSeconds);
		static std::shared_ptr<HitPreview> shared(std::vector<std::filesystem::path> _folders, double _idleSeconds);

		void run();
		// The next client with a request waiting, after the last one served; m_slots.end() when none (under the lock)
		std::map<uint64_t, Slot>::iterator nextPending();
		// The thread's: a fresh engine from the OS, read from the flash image the first time; null without one
		std::unique_ptr<Engine> build();
		void release();

		const std::vector<std::filesystem::path> m_folders;
		const Clock::duration m_idle;

		std::mutex m_mutex;
		std::condition_variable m_wake;
		std::map<uint64_t, Slot> m_slots;
		uint64_t m_clients = 0;
		uint64_t m_lastServed = 0;
		bool m_quit = false;

		// The thread's own
		std::unique_ptr<md::fw::FlashOs> m_os;
		std::unique_ptr<md::fw::RomBank> m_bank;
		std::unique_ptr<Engine> m_next;
		std::atomic<bool> m_holding{false};
		std::atomic<uint32_t> m_built{0};

		std::thread m_thread;
	};
}
