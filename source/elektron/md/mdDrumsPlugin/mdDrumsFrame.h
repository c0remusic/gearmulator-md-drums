#pragma once

#include <cstdint>

namespace mdDrums
{
	// What every view reads at the start of the editor's frame (Editor::frame), once for all of them
	struct Frame
	{
		double now = 0.0;			// ms, on the editor's clock
		uint8_t shown = 0;			// the Track shown
		bool shownChanged = true;	// another Track than the last frame's, or the first frame
		// The first frame, or the first after a pause of more than a second (a window closed meanwhile): what piled up
		// in the Telemetry (peaks, Hits, notes that played nothing) is taken without being shown
		bool resumed = true;

		// Whether a view that looks _hz times a second is due, _nextMs moved on when it is
		bool every(double& _nextMs, const int _hz) const
		{
			if(now < _nextMs)
				return false;
			_nextMs = now + 1000.0 / _hz;
			return true;
		}
	};
}
