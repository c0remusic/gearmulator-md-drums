#pragma once

#include "juce_core/juce_core.h"

#include <optional>
#include <string>

namespace mdDrums::updates
{
	// A release of MD Drums on GitHub, from its tag md-drums-vX.Y.Z
	struct Release
	{
		std::string version;	// "0.1.1"
		std::string url;		// its page
	};

	// Below, equal to or above zero as _a is older than, the same as or newer than _b, number by number ("0.1.10" after
	// "0.1.9"; a missing number counts as 0)
	int compare(const std::string& _a, const std::string& _b);

	// The newest MD Drums release in GitHub's release list (its JSON parsed), pre-releases included, drafts, other tags
	// and versions that are not numbers and dots left out; none when the list holds none
	std::optional<Release> newest(const juce::var& _releases);

	// The process's check (ticket 35): the release list read once in the process's life, on a thread of its own, while
	// an editor holds a Check (juce::SharedResourcePointer): the last editor closed stops a read still running, on the
	// message thread, never as the plug-in's module unloads. Nothing is installed: the editor says that a newer release
	// is out and opens its page.
	class Check final : juce::Thread
	{
	public:
		Check();
		~Check() override;

		Check(const Check&) = delete;
		Check& operator=(const Check&) = delete;

		// Message thread: reads the list, unless the process already did
		void start();
		// The newest release the read found; none before it came, or when it failed (no network, GitHub refusing)
		static std::optional<Release> latest();

	private:
		void run() override;
	};
}
