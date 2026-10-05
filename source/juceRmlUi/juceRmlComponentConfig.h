#pragma once

#include <cstdint>
#include <vector>

namespace juceRmlUi
{
	enum class SoftwareRendererMode : int8_t
	{
		Auto = -1,
		ForceOff = 0,
		ForceOn = 1,
	};

	struct RmlComponentConfig
	{
		int refreshRateLimitHz = -1;
		// Frame rate of the accelerated renderers (OpenGL, Metal) when refreshRateLimitHz sets
		// none; -1 keeps the platform default. The software renderer keeps its own default.
		int acceleratedRefreshRateHz = -1;
		SoftwareRendererMode forceSoftwareRenderer = SoftwareRendererMode::Auto;
		std::vector<std::string> additionalTemplateFiles;
	};
}
