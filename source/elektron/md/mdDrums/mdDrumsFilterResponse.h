#pragma once

#include "TrackFx.h"

#include <array>
#include <memory>

namespace mdDrums
{
	// What a Track's EQ and filter do to its sound, from the engine's own coefficients (Design/HANDOFF.md, "Filter and
	// EQ"; research/05-ecrans.md): the mixer DSP's EQ, a peaking biquad, then its filter, a 4-pole low-pass at FLTF +
	// FLTW and a 4-pole high-pass at FLTF, from the coefficient words that a TrackFx of its own computes over the engine's
	// tables in one block, as the DSP does (an impulse response would not do: the high-pass at FLTF 0 rings for seconds,
	// and the fixed-point recursion holds an offset of its own). Their product is evaluated at any frequency in a
	// microsecond; mdDrumsScreensTest checks it against the chain playing sines. AMD, SRR and DIST are left out. In dB
	// from an untouched Track's chain at 1 kHz.
	class FilterResponse
	{
	public:
		static constexpr double SampleRate = 44100.0;

		struct Settings
		{
			int eqf = 64;
			int eqg = 64;
			int fltf = 0;
			int fltw = 127;
			int fltq = 0;

			bool operator==(const Settings& _other) const
			{
				return eqf == _other.eqf && eqg == _other.eqg && fltf == _other.fltf && fltw == _other.fltw && fltq == _other.fltq;
			}
			bool operator!=(const Settings& _other) const { return !(*this == _other); }
		};

		explicit FilterResponse(std::shared_ptr<const md::engine::TrackFx::Tables> _tables);
		~FilterResponse();

		FilterResponse(const FilterResponse&) = delete;
		FilterResponse& operator=(const FilterResponse&) = delete;

		// The coefficients of _settings (0-127 each), which gain() then evaluates
		void set(const Settings& _settings);
		const Settings& settings() const { return m_settings; }
		// The response at _hz, in dB from an untouched Track's at 1 kHz
		double gain(double _hz) const;
		// Where the EQ acts: the angle of its poles for a boost, of its zeros for a cut (the pair nearer the unit circle)
		double eqCentre() const;

		// A 2-pole section as the DSP runs it: y[n] = a1 y[n-1] + a2 y[n-2] + b0 x[n] + b1 x[n-1] + b2 x[n-2]
		struct Biquad
		{
			double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
			double magnitude(double _hz) const;
		};
		const Biquad& eq() const { return m_eq; }
		const Biquad& lowPass() const { return m_lowPass; }	// twice over
		const Biquad& highPass() const { return m_highPass; }	// twice over

	private:
		double raw(double _hz) const;

		std::shared_ptr<const md::engine::TrackFx::Tables> m_tables;
		std::unique_ptr<md::engine::TrackFx> m_fx;
		Settings m_settings;
		Biquad m_eq, m_lowPass, m_highPass;
		double m_eqCentre = 0.0;
		double m_reference = 0.0;	// an untouched Track's chain at 1 kHz, in dB
	};
}
