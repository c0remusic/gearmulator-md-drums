#include "mdDrumsKitFiles.h"

#include "mdProtocol/mdmachines.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>

namespace mdDrums
{
	KitFiles::KitFiles(std::filesystem::path _folder) : m_folder(std::move(_folder))
	{
	}

	KitFiles::~KitFiles()
	{
		std::error_code error;
		for(const auto& [id, device] : m_devices)
			std::filesystem::remove(file(id), error);
	}

	std::filesystem::path KitFiles::defaultFolder()
	{
		std::error_code error;
		return std::filesystem::temp_directory_path(error) / "md-drums-seq";
	}

	std::filesystem::path KitFiles::file(const uint32_t _id) const
	{
		char name[16];
		std::snprintf(name, sizeof(name), "%06x.kit", _id & 0xffffffu);
		return m_folder / name;
	}

	std::string KitFiles::text(const md::automation::sysex::MdKit& _kit)
	{
		constexpr auto model = md::MachineModel::Machinedrum;
		std::string out;
		out.reserve(3000);
		out.append("md-drums-kit 1\nkit ").append(_kit.displayName()).append("\n");
		for(uint8_t t = 0; t < md::automation::machinedrum::TrackCount; ++t)
		{
			const auto id = _kit.machine(t);
			if(const auto* machine = md::machines::find(model, id))
			{
				out += "track " + std::to_string(t + 1) + " " + std::to_string(id) + " " + std::string(machine->name);
				const auto* names = md::machines::parameterNames(model, id);
				for(size_t p = 0; p < 8; ++p)
				{
					const auto name = names ? (*names)[p] : std::string_view();
					out += " " + (name.empty() ? std::string("-") : std::string(name));
				}
				out += "\n";
			}
			out += "values " + std::to_string(t + 1);
			for(const auto value : _kit.parameters[t])
				out += " " + std::to_string(std::min<int>(value, 127));
			out += "\n";
		}
		return out;
	}

	void KitFiles::hello(const uint32_t _id, const double _nowMs)
	{
		const auto [device, isNew] = m_devices.try_emplace(_id);
		device->second.lastHello = _nowMs;
		if(isNew)
			m_nextStep = std::min(m_nextStep, _nowMs);
	}

	void KitFiles::step(const std::string& _text, const double _nowMs)
	{
		m_nextStep = _nowMs + StepMs;
		for(auto it = m_devices.begin(); it != m_devices.end();)
		{
			auto& device = it->second;
			if(_nowMs - device.lastHello > ForgetMs)
			{
				std::error_code error;
				std::filesystem::remove(file(it->first), error);
				it = m_devices.erase(it);
				continue;
			}
			// A file the device holds open stays as it was until a later step
			if(device.written != _text && write(it->first, _text))
				device.written = _text;
			++it;
		}
	}

	bool KitFiles::write(const uint32_t _id, const std::string& _text) const
	{
		std::error_code error;
		std::filesystem::create_directories(m_folder, error);
		const auto* bytes = reinterpret_cast<const uint8_t*>(_text.data());
		return baseLib::filesystem::writeFileAtomic(file(_id).u8string(), bytes, _text.size());
	}
}
