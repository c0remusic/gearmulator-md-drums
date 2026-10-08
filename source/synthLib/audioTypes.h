#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace synthLib
{
	// MD Drums renders the most: Main (stereo) and Out 01-16 (mono)
	inline constexpr size_t MaxAudioInputs	= 4;
	inline constexpr size_t MaxAudioOutputs	= 18;

	template<typename T> using TAudioInputsT		= std::array<const T*,MaxAudioInputs>;
	template<typename T> using TAudioOutputsT		= std::array<T*,MaxAudioOutputs>;

	using TAudioInputs		= TAudioInputsT<float>;
	using TAudioOutputs		= TAudioOutputsT<float>;
	using TAudioInputsInt	= TAudioInputsT<uint32_t>;
	using TAudioOutputsInt	= TAudioOutputsT<uint32_t>;
}
