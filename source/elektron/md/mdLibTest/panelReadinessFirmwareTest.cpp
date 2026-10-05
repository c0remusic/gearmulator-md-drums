#include "mdLib/mdhardware.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdstate.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void require(const bool _condition, const char* const _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	void advance(md::Hardware& _hardware, const uint32_t _seconds)
	{
		constexpr uint32_t blockSize = 128;
		const uint32_t frameCount = md::g_samplerate * _seconds;
		for(uint32_t frame = 0; frame < frameCount; frame += blockSize)
			_hardware.advance(std::min(blockSize, frameCount - frame));
	}

	void requireBootedPanel(md::Hardware& _hardware, const char* const _phase)
	{
		const auto panel = _hardware.getFrontPanelSnapshot();
		if(!_hardware.getUC().isPanelHandshakeComplete())
			throw std::runtime_error(std::string(_phase) + ": panel handshake did not complete");
		if(!_hardware.isFirmwareMidiReady())
			throw std::runtime_error(std::string(_phase) + ": firmware MIDI did not become ready");
		if(panel.getByteCount() < 20'000)
			throw std::runtime_error(std::string(_phase) + ": panel UART stream stopped early");
		if(panel.getTileWriteCount() < 1'000)
			throw std::runtime_error(std::string(_phase) + ": LCD tile stream stopped early");
		if(panel.countLitPixels() < 500)
			throw std::runtime_error(std::string(_phase) + ": LCD never reached a usable screen");
	}
}

int main()
{
	const auto* const path = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	if(path == nullptr || *path == '\0')
	{
		std::cout << "Set GEARMULATOR_MD_FIRMWARE_BIN to Machinedrum OS 1.63\n";
		return 77;
	}

	try
	{
		std::vector<uint8_t> rom;
		require(baseLib::filesystem::readFile(rom, path), "could not read MD firmware");
		require(md::RomLoader::isRomForModel(rom, md::MachineModel::Machinedrum),
			"firmware is not the canonical Machinedrum OS 1.63 image");

		// A blank UW flash exercises the long first-run initialization whose LCD
		// animation is paced by the firmware's own DSP-HREQ-driven software timer.
		// On the heap: a Hardware outgrows the default 1 MB stack of Windows' main thread
		auto freshStorage = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum);
		auto& fresh = *freshStorage;
		require(fresh.isValid(), "fresh machine was invalid");
		require(fresh.isFactoryFlashInitializationExpected(),
			"fresh machine did not enter factory initialization");
		advance(fresh, 18);
		requireBootedPanel(fresh, "fresh boot");
		require(fresh.flashDirty(), "fresh boot did not initialize UW flash");
		require(fresh.isFactoryFlashCacheReady(),
			"fresh boot did not publish the initialized flash cache");

		const auto initializedFlash = fresh.copyFlashData();
		const auto factoryCache = fresh.copyFactoryFlashCache();
		require(!factoryCache.empty(), "fresh boot produced an empty factory cache");
		std::vector<uint8_t> decodedFlash;
		require(md::decodeFactoryFlashCache(decodedFlash, factoryCache, rom),
			"fresh factory cache could not be decoded");
		require(decodedFlash == initializedFlash,
			"fresh factory cache did not reproduce initialized flash");

		// Repeat launch from that exact initialized image. This covers the ordinary
		// startup path as well as the blank-flash path above.
		auto repeatStorage = std::make_unique<md::Hardware>(rom, path, md::MachineModel::Machinedrum,
			std::vector<uint8_t>{}, std::shared_ptr<md::FrontPanelPublisher>{}, initializedFlash, factoryCache);
		auto& repeat = *repeatStorage;
		require(repeat.isValid(), "repeat-launch machine was invalid");
		require(!repeat.isFactoryFlashInitializationExpected(),
			"repeat launch unexpectedly entered factory initialization");
		advance(repeat, 12);
		requireBootedPanel(repeat, "repeat boot");

		const auto freshPanel = fresh.getFrontPanelSnapshot();
		const auto repeatPanel = repeat.getFrontPanelSnapshot();
		std::cout << "MD firmware-owned panel readiness passed: fresh bytes="
			<< freshPanel.getByteCount() << ", tiles=" << freshPanel.getTileWriteCount()
			<< ", lit=" << freshPanel.countLitPixels() << "; repeat bytes="
			<< repeatPanel.getByteCount() << ", tiles=" << repeatPanel.getTileWriteCount()
			<< ", lit=" << repeatPanel.countLitPixels() << '\n';
		return 0;
	}
	catch(const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
