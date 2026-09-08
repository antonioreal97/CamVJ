#pragma once

namespace atemfx {

enum class AutomationWaveform
{
    Sine,
    Triangle,
    RampUp,
    RampDown,
    Square,
};

// Each parameter owns its clock, so moving or copying an effect cannot bind it
// to the lifetime or index of another node.
struct ParameterAutomation
{
    bool enabled = false;
    bool paused = false;
    AutomationWaveform waveform = AutomationWaveform::Sine;
    double periodSeconds = 4.0;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float phaseOffset = 0.0f;

    void advance(double deltaSeconds) noexcept;
    void restart() noexcept;

    // Includes phaseOffset, matching the phase used to evaluate the waveform.
    double phase() const noexcept;
    float normalizedValue() const noexcept;
    static float sample(AutomationWaveform waveform, double phase) noexcept;

private:
    double phase_ = 0.0;
};

} // namespace atemfx
