#include "mdDrumsFilterResponse.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mdDrums
{
	namespace
	{
		constexpr double g_pi = 3.14159265358979323846;

		double fraction(const int32_t _word)
		{
			return static_cast<double>(_word) / 8388608.0;
		}
	}

	double FilterResponse::Biquad::magnitude(const double _hz) const
	{
		const auto w = 2.0 * g_pi * _hz / SampleRate;
		const std::complex<double> z1(std::cos(w), -std::sin(w)), z2 = z1 * z1;
		return std::abs(b0 + b1 * z1 + b2 * z2) / std::abs(1.0 - a1 * z1 - a2 * z2);
	}

	FilterResponse::FilterResponse(std::shared_ptr<const md::engine::TrackFx::Tables> _tables) : m_tables(std::move(_tables))
	{
		m_fx = std::make_unique<md::engine::TrackFx>(*m_tables);
		m_fx->stopAfter = md::engine::TrackFx::Stop::AfterFilter2;
		set({});
		m_reference = raw(1000.0);
	}

	FilterResponse::~FilterResponse() = default;

	void FilterResponse::set(const Settings& _settings)
	{
		m_settings = _settings;
		const auto word = [](const int _value) { return static_cast<uint32_t>(std::clamp(_value, 0, 127) << 7); };

		// A block through a TrackFx computes every coefficient word: the EQ's (TrackFx::eqWords), and the filter's,
		// whose targets it leaves in the Track's state (TrackFx::filter1 and filter2): the low-pass's 2rk - 1, -r^2 and
		// 1 - 2rk + r^2 at $2a-$2c, the high-pass's rk - 1/2, -r^2 and (1 + 2rk + r^2) / 8 at $2e-$30
		const std::array<uint32_t, 9> words{0, 0, word(_settings.eqf), word(_settings.eqg), word(_settings.fltf),
			word(_settings.fltw), word(_settings.fltq), 0, 0};
		md::engine::TrackFx::State state;
		md::engine::TrackFx::init(state);
		md::engine::TrackFx::setParams(state, words.data());
		std::array<int32_t, md::engine::TrackFx::kBlock> silence{}, out{};
		m_fx->resetScratch();
		m_fx->process(state, silence.data(), out.data());
		const auto& y = state.y;

		// Each an all-pole section, twice: y[n] = 2rk y[n-1] - r^2 y[n-2] + (1 - 2rk + r^2) x[n], unity at DC
		m_lowPass = {fraction(y[0x2c]), 0.0, 0.0, fraction(y[0x2a]) + 1.0, fraction(y[0x2b])};
		// Each g (1 - z^-1)^2 over the same denominator, half at Nyquist
		const auto g = fraction(y[0x30]);
		m_highPass = {g, -2.0 * g, g, 2.0 * fraction(y[0x2e]) + 1.0, fraction(y[0x2f])};

		// The EQ's words as TrackFx::eq computed them: its poles' -r^2 and rk - 1/2, then its zeros' taps, which the DSP's
		// 24-step division scales to pass DC unchanged where it can (it saturates for the steepest boosts)
		const auto& w = m_fx->eqWords();
		m_eq = {fraction(w[4]), fraction(w[3]), fraction(w[2]), 2.0 * fraction(w[1]) + 1.0, fraction(w[0])};
		// Where it acts: the angle of the pair nearer the unit circle, its poles for a boost, its zeros for a cut
		const auto poles = std::sqrt(std::max(0.0, -m_eq.a2));
		const auto zeros = m_eq.b0 != 0.0 ? std::sqrt(std::max(0.0, m_eq.b2 / m_eq.b0)) : 0.0;
		const auto cosine = zeros > poles ? -m_eq.b1 / (2.0 * m_eq.b0 * zeros) : poles > 0.0 ? m_eq.a1 / (2.0 * poles) : 1.0;
		m_eqCentre = std::acos(std::clamp(cosine, -1.0, 1.0)) / (2.0 * g_pi) * SampleRate;
	}

	double FilterResponse::raw(const double _hz) const
	{
		const auto low = m_lowPass.magnitude(_hz), high = m_highPass.magnitude(_hz);
		const auto magnitude = m_eq.magnitude(_hz) * low * low * high * high;
		return 20.0 * std::log10(std::max(magnitude, 1e-12));
	}

	double FilterResponse::gain(const double _hz) const
	{
		return raw(_hz) - m_reference;
	}

	double FilterResponse::eqCentre() const
	{
		return m_eqCentre;
	}
}
