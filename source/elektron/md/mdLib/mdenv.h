#pragma once

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>

namespace md
{
	// Environment overrides are for experiments. Unset, empty and
	// unparsable all count as unset: std::atoi, std::atof and std::strtoul
	// read the last two as 0, which most overrides take as a real setting,
	// and PowerShell's [Environment]::SetEnvironmentVariable($name, $null)
	// leaves an empty variable that child processes see. Both pair leads
	// read that way stopped the Monomachine at the pair handoff.
	inline const char* envOverride(const char* _name)
	{
		const char* const value = std::getenv(_name);
		return value && *value ? value : nullptr;
	}

	inline std::optional<double> envNumber(const char* _name)
	{
		const char* const value = envOverride(_name);
		if(!value)
			return std::nullopt;
		char* end = nullptr;
		const double number = std::strtod(value, &end);
		const bool parsed = end != value;
		while(std::isspace(static_cast<unsigned char>(*end)))
			++end;
		if(!parsed || *end || !std::isfinite(number))
		{
			std::fprintf(stderr, "[MD] %s=\"%s\" ignored: not a number\n", _name, value);
			return std::nullopt;
		}
		return number;
	}

	// A count also fits T and is not negative (std::strtoul wraps "-1").
	template<typename T>
	std::optional<T> envCount(const char* _name)
	{
		const auto number = envNumber(_name);
		if(!number)
			return std::nullopt;
		if(*number < 0.0 || *number >= std::ldexp(1.0, std::numeric_limits<T>::digits))
		{
			std::fprintf(stderr, "[MD] %s=%g ignored: not a count\n", _name, *number);
			return std::nullopt;
		}
		return static_cast<T>(*number);
	}
}
