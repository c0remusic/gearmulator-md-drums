#include "mdDrumsHitPreview.h"

#include "mdDrumsEngine.h"

#include "Firmware.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace mdDrums
{
	void HitPreview::render(Engine& _engine, const Request& _request, Telemetry::Capture& _capture)
	{
		const int track = std::min<int>(_request.track, Engine::TrackCount - 1);
		applyKit(_engine, _request.kit);
		// Struck before the first block, as a Device's first note at its first sample: the Hit sounds SampleAccurateDelay
		// samples later, where the capture starts (Device::capture)
		_engine.trigger(track, _request.velocity);

		_capture.id = 1;
		_capture.velocity = _request.velocity;
		_capture.count = 0;
		_capture.columns.resize(Telemetry::CaptureColumns);

		std::array<float, 1024> samples{};
		std::array<float*, Engine::OutputCount> outputs{};
		outputs[2 + static_cast<size_t>(track)] = samples.data();
		int64_t position = 0;
		int filled = 0;
		float low = 0.0f, high = 0.0f;
		while(_capture.count < Telemetry::CaptureColumns)
		{
			_engine.render(outputs.data(), samples.size());
			for(size_t i = 0; i < samples.size() && _capture.count < Telemetry::CaptureColumns; ++i)
			{
				if(position + static_cast<int64_t>(i) < Engine::SampleAccurateDelay)
					continue;
				const auto value = samples[i];
				low = filled ? std::min(low, value) : value;
				high = filled ? std::max(high, value) : value;
				if(++filled < Telemetry::ColumnSamples)
					continue;
				filled = 0;
				_capture.columns[static_cast<size_t>(_capture.count++)] = {Telemetry::level(low), Telemetry::level(high)};
			}
			position += static_cast<int64_t>(samples.size());
		}
	}

	HitPreview::Client::Client(std::vector<std::filesystem::path> _folders, const double _idleSeconds)
		: m_preview(shared(std::move(_folders), _idleSeconds))
	{
		std::lock_guard lock(m_preview->m_mutex);
		m_id = ++m_preview->m_clients;
		m_preview->m_slots[m_id];
	}

	HitPreview::Client::~Client()
	{
		std::lock_guard lock(m_preview->m_mutex);
		m_preview->m_slots.erase(m_id);
	}

	uint32_t HitPreview::Client::request(Request _request)
	{
		uint32_t id;
		{
			std::lock_guard lock(m_preview->m_mutex);
			auto& slot = m_preview->m_slots[m_id];
			id = ++slot.ids;
			slot.pending = std::move(_request);
			slot.pendingId = id;
		}
		m_preview->m_wake.notify_one();
		return id;
	}

	bool HitPreview::Client::take(Result& _result)
	{
		std::lock_guard lock(m_preview->m_mutex);
		auto& slot = m_preview->m_slots[m_id];
		if(!slot.result)
			return false;
		std::swap(_result, *slot.result);
		slot.result.reset();
		return true;
	}

	bool HitPreview::Client::holdsEngine() const
	{
		return m_preview->m_holding.load();
	}

	uint32_t HitPreview::Client::enginesBuilt() const
	{
		return m_preview->m_built.load();
	}

	std::shared_ptr<HitPreview> HitPreview::shared(std::vector<std::filesystem::path> _folders, const double _idleSeconds)
	{
		static std::mutex mutex;
		static std::weak_ptr<HitPreview> instance;
		std::lock_guard lock(mutex);
		auto preview = instance.lock();
		if(!preview)
		{
			preview = std::shared_ptr<HitPreview>(new HitPreview(std::move(_folders), _idleSeconds));
			instance = preview;
		}
		return preview;
	}

	HitPreview::HitPreview(std::vector<std::filesystem::path> _folders, const double _idleSeconds)
		: m_folders(std::move(_folders))
		, m_idle(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(_idleSeconds)))
	{
		m_thread = std::thread([this] { run(); });
	}

	HitPreview::~HitPreview()
	{
		{
			std::lock_guard lock(m_mutex);
			m_quit = true;
		}
		m_wake.notify_one();
		m_thread.join();
	}

	std::map<uint64_t, HitPreview::Slot>::iterator HitPreview::nextPending()
	{
		const auto first = m_slots.upper_bound(m_lastServed);
		for(auto it = first; it != m_slots.end(); ++it)
			if(it->second.pending)
				return it;
		for(auto it = m_slots.begin(); it != first; ++it)
			if(it->second.pending)
				return it;
		return m_slots.end();
	}

	std::unique_ptr<Engine> HitPreview::build()
	{
		try
		{
			if(!m_os)
			{
				const auto flash = Engine::findFlashImage(m_folders);
				if(flash.empty())
					return {};
				m_os = std::make_unique<md::fw::FlashOs>(md::fw::loadFirmwareFromFlash(flash));
				m_bank = std::make_unique<md::fw::RomBank>(md::fw::loadRomBankFromFlash(flash));
			}
			m_holding = true;
			auto engine = std::make_unique<Engine>(*m_os, *m_bank, false);
			++m_built;
			return engine;
		}
		catch(const std::exception& _error)
		{
			std::fprintf(stderr, "[MD Drums] preview: %s\n", _error.what());
			return {};
		}
	}

	void HitPreview::release()
	{
		m_next.reset();
		m_bank.reset();
		m_os.reset();
		m_holding = false;
	}

	void HitPreview::run()
	{
		auto lastRequest = Clock::now();
		std::unique_lock lock(m_mutex);
		while(!m_quit)
		{
			const auto next = nextPending();
			if(next == m_slots.end())
			{
				if(!m_holding)
				{
					m_wake.wait(lock);
					continue;
				}
				// Idle: the engine built ahead and the OS go once no request came for the idle time
				if(m_wake.wait_until(lock, lastRequest + m_idle) == std::cv_status::timeout && nextPending() == m_slots.end()
					&& !m_quit)
				{
					lock.unlock();
					release();
					lock.lock();
				}
				continue;
			}

			const auto client = next->first;
			auto request = std::move(*next->second.pending);
			next->second.pending.reset();
			Result result;
			result.id = next->second.pendingId;
			result.track = request.track;
			m_lastServed = client;
			lastRequest = Clock::now();
			lock.unlock();

			const auto start = Clock::now();
			{
				auto engine = m_next ? std::move(m_next) : build();
				if(engine)
				{
					try
					{
						render(*engine, request, result.capture);
					}
					catch(const std::exception& _error)
					{
						std::fprintf(stderr, "[MD Drums] preview: %s\n", _error.what());
						result.capture.id = 0;
						result.capture.count = 0;
					}
				}
			}
			result.renderMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

			lock.lock();
			if(const auto slot = m_slots.find(client); slot != m_slots.end())
				slot->second.result = std::move(result);
			// The next engine, while no request waits: a render then starts at once
			if(!m_quit && !m_next && nextPending() == m_slots.end())
			{
				lock.unlock();
				m_next = build();
				lock.lock();
			}
		}
	}
}
