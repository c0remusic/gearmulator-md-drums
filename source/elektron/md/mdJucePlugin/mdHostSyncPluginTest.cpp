// Drives the real plug-in processor on the MM and MD firmware through the
// "Follow host tempo" setting: the Device learns the active Global from the
// controller's polling, reports the machine's own setting while the option is
// off, sets the machine to follow the host when it is turned on, and gives the
// machine its own tempo back when it is turned off.

#include "mdAutomationTestSupport.h"
#include "synthLib/romLoader.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	using namespace mdAutomationTest;
	using State = md::HostSync::State;

	// Processes until the host sync reports _expected; returns the audio seconds it took.
	double waitFor(Harness& _harness, const State _expected, const int _maxSeconds, const std::string& _what)
	{
		const int blocks = _maxSeconds * 48000 / BlockSize;
		auto last = _harness.processor.getHostSyncState();
		for(int block = 0; block < blocks; ++block)
		{
			_harness.process(1);
			const auto state = _harness.processor.getHostSyncState();
			if(state != last)
			{
				std::printf("mdHostSyncPluginTest: %s state %d -> %d at %.2f s\n", _what.c_str(),
					static_cast<int>(last), static_cast<int>(state), block * BlockSize / 48000.0);
				last = state;
			}
			if(state == _expected)
				return block * BlockSize / 48000.0;
			require(state != State::Failed, _what + ": the machine did not take the setting");
		}
		throw std::runtime_error(_what + ": no result within " + std::to_string(_maxSeconds) + " s of audio");
	}

	void setFollowHostTempo(Harness& _harness, const bool _enabled)
	{
		_harness.processor.getConfig().setValue(
			mdJucePlugin::AudioPluginAudioProcessor::FollowHostTempoConfigKey, _enabled);
		_harness.processor.applyFollowHostTempoSetting(true);
	}

	bool run(const md::MachineModel _model)
	{
		const std::string name = _model == md::MachineModel::Monomachine ? "MM" : "MD";
		Harness harness(_model);
		if(!harness.hasLocalFirmware())
		{
			allowMissingFirmware("mdHostSyncPluginTest", _model);
			return false;
		}
		harness.prepare();
		const auto booted = waitFor(harness, State::NotFollowing, 60, name + " startup");
		setFollowHostTempo(harness, true);
		const auto following = waitFor(harness, State::Following, 30, name + " follow");
		setFollowHostTempo(harness, false);
		const auto released = waitFor(harness, State::NotFollowing, 30, name + " release");
		std::printf("mdHostSyncPluginTest: %s own setting read after %.1f s, following after %.1f s, "
			"released after %.1f s\n", name.c_str(), booted, following, released);
		return true;
	}
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const auto* const md = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const auto* const mm = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	if((md == nullptr || !*md) && (mm == nullptr || !*mm))
	{
		std::printf("mdHostSyncPluginTest: SKIP (no firmware supplied)\n");
		return SkipReturnCode;
	}
	try
	{
		juce::ScopedJuceInitialiser_GUI gui;
		bool ran = false;
		for(const auto* const path : {mm, md})
		{
			if(path != nullptr && *path)
				synthLib::RomLoader::addSearchPath(juce::File(path).getParentDirectory().getFullPathName().toStdString());
		}
		if(mm != nullptr && *mm)
			ran |= run(md::MachineModel::Monomachine);
		if(md != nullptr && *md)
			ran |= run(md::MachineModel::Machinedrum);
		if(!ran)
			return SkipReturnCode;
		std::printf("mdHostSyncPluginTest: PASS\n");
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::fprintf(stderr, "mdHostSyncPluginTest: FAIL %s\n", _error.what());
		return 1;
	}
}
