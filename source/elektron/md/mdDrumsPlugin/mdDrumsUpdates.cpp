#include "mdDrumsUpdates.h"

#include <algorithm>
#include <mutex>
#include <string_view>
#include <vector>

namespace mdDrums::updates
{
	namespace
	{
		// The repository's release list, which the check reads
		constexpr const char* g_listUrl = "https://api.github.com/repos/c0remusic/gearmulator-md-drums/releases?per_page=30";
		constexpr std::string_view g_tagPrefix = "md-drums-v";

		// What the process's read found, beyond the life of the Check that ran it
		std::mutex g_mutex;
		std::optional<Release> g_latest;
		bool g_started = false;		// message thread

		std::vector<int> numbers(const std::string& _version)
		{
			std::vector<int> result;
			int value = 0;
			bool any = false;
			for(const char c : _version)
			{
				if(c >= '0' && c <= '9')
				{
					value = value * 10 + (c - '0');
					any = true;
				}
				else if(c == '.')
				{
					result.push_back(value);
					value = 0;
					any = false;
				}
				else
				{
					break;
				}
			}
			if(any || !result.empty())
				result.push_back(value);
			return result;
		}

		// Numbers separated by dots and nothing else, as "0.1.10": the editor shows it
		bool isVersion(const std::string& _version)
		{
			bool digit = false;
			for(const char c : _version)
			{
				if(c >= '0' && c <= '9')
					digit = true;
				else if(c == '.' && digit)
					digit = false;
				else
					return false;
			}
			return digit;
		}
	}

	int compare(const std::string& _a, const std::string& _b)
	{
		const auto a = numbers(_a);
		const auto b = numbers(_b);
		for(size_t i = 0; i < std::max(a.size(), b.size()); ++i)
		{
			const auto x = i < a.size() ? a[i] : 0;
			const auto y = i < b.size() ? b[i] : 0;
			if(x != y)
				return x < y ? -1 : 1;
		}
		return 0;
	}

	std::optional<Release> newest(const juce::var& _releases)
	{
		const auto* list = _releases.getArray();
		if(!list)
			return {};
		std::optional<Release> best;
		for(const auto& entry : *list)
		{
			if(static_cast<bool>(entry["draft"]))
				continue;
			const auto tag = entry["tag_name"].toString().toStdString();
			if(tag.rfind(g_tagPrefix, 0) != 0)
				continue;
			const auto version = tag.substr(g_tagPrefix.size());
			if(!isVersion(version))
				continue;
			if(!best || compare(version, best->version) > 0)
				best = Release{version, entry["html_url"].toString().toStdString()};
		}
		return best;
	}

	Check::Check() : juce::Thread("MD Drums update check")
	{
	}

	Check::~Check()
	{
		// The last editor closed: a read still running gets its connection's time to give up
		stopThread(8000);
	}

	void Check::start()
	{
		if(g_started)
			return;
		g_started = true;
		startThread(juce::Thread::Priority::low);
	}

	std::optional<Release> Check::latest()
	{
		const std::lock_guard lock(g_mutex);
		return g_latest;
	}

	void Check::run()
	{
		const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
			.withConnectionTimeoutMs(6000)
			.withExtraHeaders("User-Agent: MD-Drums/" MdDrumsVersion "\r\nAccept: application/vnd.github+json");
		const auto stream = juce::URL(g_listUrl).createInputStream(options);
		if(!stream || threadShouldExit())
			return;
		if(auto release = newest(juce::JSON::parse(stream->readEntireStreamAsString())))
		{
			const std::lock_guard lock(g_mutex);
			g_latest = std::move(release);
		}
	}
}
