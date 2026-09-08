#include "effects/parameter_automation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace atemfx {
namespace {

double wrapPhase(double phase) noexcept
{
    if (!std::isfinite(phase)) return 0.0;
    const double wrapped = std::fmod(phase, 1.0);
    const double normalized = wrapped < 0.0 ? wrapped + 1.0 : wrapped;
    return normalized < 1.0 ? normalized : 0.0;
}

} // namespace

void ParameterAutomation::advance(double deltaSeconds) noexcept
{
    if (!enabled || paused || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
        return;

    periodSeconds = std::isfinite(periodSeconds)
        ? std::clamp(periodSeconds, 0.05, 600.0) : 4.0;

    // Reduce elapsed time before dividing so even a very large finite delta
    // cannot overflow. Keeping only the fractional cycle avoids long-run drift.
    phase_ = wrapPhase(phase_ + std::fmod(deltaSeconds, periodSeconds) / periodSeconds);
}

void ParameterAutomation::restart() noexcept
{
    phase_ = 0.0;
}

double ParameterAutomation::phase() const noexcept
{
    return wrapPhase(phase_ + wrapPhase(static_cast<double>(phaseOffset)));
}

float ParameterAutomation::normalizedValue() const noexcept
{
    return sample(waveform, phase());
}

float ParameterAutomation::sample(AutomationWaveform waveform, double phase) noexcept
{
    const double p = wrapPhase(phase);
    switch (waveform)
    {
        case AutomationWaveform::Triangle:
            return static_cast<float>(1.0 - std::abs(2.0 * p - 1.0));
        case AutomationWaveform::RampUp:
            return static_cast<float>(p);
        case AutomationWaveform::RampDown:
            return static_cast<float>(1.0 - p);
        case AutomationWaveform::Square:
            return p < 0.5 ? 0.0f : 1.0f;
        case AutomationWaveform::Sine:
        default:
            return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * p));
    }
}

} // namespace atemfx
