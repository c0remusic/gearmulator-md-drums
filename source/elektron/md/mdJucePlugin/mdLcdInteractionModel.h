#pragma once

#include <cstdint>
#include <optional>

#include "mdLib/mdfrontpanel.h"
#include "mdProtocol/mdtypes.h"

// Recognition and native-LCD geometry for the deliberately small set of LCD
// fields whose semantics have been qualified against the original firmware.
// This layer has no JUCE/RmlUi dependency so recognition can be corpus-tested.
namespace mdJucePlugin::lcdInteraction
{
	inline constexpr const char* configKey = "lcdRotaryInteraction";
	// Qualified rotary surfaces are usable without a hidden first-run setup step.
	// The setting remains available as a kill switch while the feature soaks.
	inline constexpr bool defaultEnabled = true;

	enum class SurfaceKind : uint8_t
	{
		EditGrid,
		Lfo,
		MasterFxEcho,
		MasterFxReverb,
		MasterFxEq,
		MasterFxDynamics,
	};

	enum class LayoutKind : uint8_t
	{
		Standard,
		Lfo,
		MasterFx,
	};

	struct NativeRect
	{
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;

		constexpr bool contains(const int _x, const int _y) const
		{
			return _x >= x && _x < x + width && _y >= y && _y < y + height;
		}
	};

	struct State
	{
		SurfaceKind surface = SurfaceKind::EditGrid;
		LayoutKind layout = LayoutKind::Standard;
		uint8_t activeEncoderMask = 0;
		uint64_t identityToken = 0;
	};

	// _dataEntrySwitchHeld is explicit input state, not a guessed pixel
	// property. Firmware's held-value overlay preserves the underlying grid and
	// page LEDs, so suppressing it from known panel state is the reliable test.
	std::optional<State> classify(const md::FrontPanel& _panel,
		md::MachineModel _model, bool _dataEntrySwitchHeld = false);

	// True when an LED input used by classify() changed. LCD changes are tracked
	// separately by the editor; keeping the LED predicate here prevents the UI
	// integration from duplicating model-specific page-identity knowledge.
	bool classificationLedsChanged(const md::FrontPanel& _before,
		const md::FrontPanel& _after, md::MachineModel _model);

	NativeRect encoderRect(LayoutKind _layout, unsigned _encoderIndex);

	// Returns the DATA ENTRY encoder index A=0 .. H=7. Bounds are half-open, so
	// a point on a shared edge can never resolve to two fields.
	std::optional<unsigned> hitTest(const State& _state, int _nativeX, int _nativeY);
}
