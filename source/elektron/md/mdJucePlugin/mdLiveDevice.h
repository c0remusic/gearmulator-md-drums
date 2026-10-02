#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "mdLib/mdlivekit.h"
#include "mdLib/mdmachinestatus.h"

namespace md
{
	class FrontPanelPublisher;
	class PanelInputQueue;
}

namespace pluginLib
{
	class Processor;
}

namespace mdJucePlugin
{
	// The md::Device the plug-in runs, reached without the device lock: taking that lock pauses the
	// machine's rendering (Plugin::withDeviceLocked), which the host shows as a CPU spike when it
	// happens on every editor frame or click. Keeps what the Device shares with other threads, its
	// status, live Kit, front panel and panel input, and takes them again under the lock only once another
	// Device or another machine (a project state committed) takes over.
	// Message thread, or an offline render's thread; never the audio thread, and never from inside a
	// withDeviceLocked callback.
	class LiveDevice
	{
	public:
		explicit LiveDevice(pluginLib::Processor& _processor);

		// None without an md::Device (a remote one, say)
		std::optional<md::MachineStatus::Values> status();
		// The Kit the machine plays (md::Device::getLiveKit); none without one, or before its first
		std::optional<md::LiveKit> liveKit();
		std::shared_ptr<md::FrontPanelPublisher> frontPanel();
		// A front-panel packet ([row][mask]) for the live machine; false when its queue rejected it
		// or there is no machine.
		bool sendPanelEvent(uint8_t _command, uint8_t _argument);

	private:
		void refresh();	// with m_mutex held

		pluginLib::Processor& m_processor;
		std::mutex m_mutex;
		bool m_known = false;				// the members below come from Device generation m_generation
		uint64_t m_generation = 0;
		uint64_t m_hardwareEpoch = 0;		// m_panelInput's machine
		std::shared_ptr<const md::MachineStatus> m_status;
		std::shared_ptr<const md::LiveKitSnapshot> m_liveKit;
		std::shared_ptr<md::FrontPanelPublisher> m_frontPanel;
		std::shared_ptr<md::PanelInputQueue> m_panelInput;
	};
}
