// The update check's reading (ticket 35 of the editor map), without the network: versions in order, number by number;
// the newest md-drums-vX.Y.Z release in a release list as GitHub's API gives it, pre-releases included, drafts, other
// tags and versions that are not numbers left out; nothing from an empty or broken list; and the version the editor shows.

#include "mdDrumsUpdates.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// Trimmed from api.github.com/repos/c0remusic/gearmulator-md-drums/releases, with what the check must skip
	constexpr const char* g_list = R"([
		{"tag_name": "md-drums-v0.1.0", "draft": false, "prerelease": true,
		 "html_url": "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/md-drums-v0.1.0"},
		{"tag_name": "md-drums-v0.1.10", "draft": false, "prerelease": true,
		 "html_url": "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/md-drums-v0.1.10"},
		{"tag_name": "md-drums-v0.1.9", "draft": false, "prerelease": false,
		 "html_url": "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/md-drums-v0.1.9"},
		{"tag_name": "md-drums-v0.3.0", "draft": true,
		 "html_url": "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/untagged-1"},
		{"tag_name": "mdmm-v9.9.9", "draft": false,
		 "html_url": "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/mdmm-v9.9.9"},
		{"tag_name": "md-drums-vnext", "draft": false, "html_url": "x"},
		{"tag_name": "md-drums-v9.0<b>", "draft": false, "html_url": "x"}
	])";
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	try
	{
		using mdDrums::updates::compare;
		require(compare("0.1.10", "0.1.9") > 0 && compare("0.1.9", "0.1.10") < 0, "0.1.10 is not after 0.1.9");
		require(compare("0.1.1", "0.1.1") == 0 && compare("0.1", "0.1.0") == 0, "equal versions differ");
		require(compare("0.2", "0.1.9") > 0 && compare("1.0.0", "0.99.99") > 0, "a higher number further left loses");

		const auto newest = mdDrums::updates::newest(juce::JSON::parse(g_list));
		require(newest.has_value(), "no release found in the list");
		std::printf("newest in the list: %s at %s\n", newest->version.c_str(), newest->url.c_str());
		require(newest->version == "0.1.10"
			&& newest->url == "https://github.com/c0remusic/gearmulator-md-drums/releases/tag/md-drums-v0.1.10",
			"the newest is not md-drums-v0.1.10, a pre-release, past a draft, another product's tag and markup");

		require(!mdDrums::updates::newest(juce::JSON::parse("[]")).has_value(), "an empty list gave a release");
		require(!mdDrums::updates::newest(juce::JSON::parse("{\"message\": \"API rate limit exceeded\"}")).has_value(),
			"GitHub's refusal gave a release");
		require(!mdDrums::updates::newest(juce::JSON::parse("not json")).has_value(), "a broken answer gave a release");

		std::printf("version shown: %s, against 0.1.0: %d\n", MdDrumsVersion, compare(MdDrumsVersion, "0.1.0"));
		require(compare(MdDrumsVersion, "0.1.0") > 0, "this build is not newer than the first preview");

		std::cout << "mdDrumsUpdateTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cout << "mdDrumsUpdateTest: FAIL: " << _e.what() << '\n';
		return 1;
	}
}
