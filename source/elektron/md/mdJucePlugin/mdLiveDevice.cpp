#include "mdLiveDevice.h"

#include "jucePluginLib/processor.h"

#include "mdLib/mddevice.h"

namespace mdJucePlugin
{
	LiveDevice::LiveDevice(pluginLib::Processor& _processor) : m_processor(_processor)
	{
	}

	std::optional<md::MachineStatus::Values> LiveDevice::status()
	{
		const std::lock_guard lock(m_mutex);
		refresh();
		if(!m_status)
			return std::nullopt;
		return m_status->read();
	}

	std::optional<md::LiveKit> LiveDevice::liveKit()
	{
		const std::lock_guard lock(m_mutex);
		refresh();
		if(!m_liveKit)
			return std::nullopt;
		return m_liveKit->read();
	}

	std::shared_ptr<md::FrontPanelPublisher> LiveDevice::frontPanel()
	{
		const std::lock_guard lock(m_mutex);
		refresh();
		return m_frontPanel;
	}

	bool LiveDevice::sendPanelEvent(const uint8_t _command, const uint8_t _argument)
	{
		const std::lock_guard lock(m_mutex);
		refresh();
		return m_panelInput && m_panelInput->tryPush(_command, _argument);
	}

	void LiveDevice::refresh()
	{
		auto& plugin = m_processor.getPlugin();
		if(m_known && plugin.getDeviceGeneration() == m_generation
			&& (!m_status || m_status->hardwareEpoch() == m_hardwareEpoch))
			return;
		plugin.withDeviceLocked([&](synthLib::Device* const _device)
		{
			// Read under the lock: Plugin::setDevice changes both together
			m_generation = plugin.getDeviceGeneration();
			m_known = true;
			const auto* const device = dynamic_cast<const md::Device*>(_device);
			m_status = device ? device->getStatus() : nullptr;
			m_liveKit = device ? device->getLiveKit() : nullptr;
			m_frontPanel = device ? device->getFrontPanelPublisher() : nullptr;
			m_panelInput = device ? device->getPanelInput() : nullptr;
			m_hardwareEpoch = device ? device->hardwareEpoch() : 0;
		});
	}
}
