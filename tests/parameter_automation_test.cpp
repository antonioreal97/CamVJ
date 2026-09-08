#include "effects/EffectParameters.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int failures = 0;
int checks = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

bool near(double actual, double expected, double tolerance = 0.000001)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void checkWaveforms()
{
    using atemfx::AutomationWaveform;
    using atemfx::ParameterAutomation;
    struct Sample
    {
        AutomationWaveform waveform;
        double phase;
        float expected;
    };
    constexpr Sample samples[] = {
        {AutomationWaveform::Sine, 0.0, 0.0f},
        {AutomationWaveform::Sine, 0.25, 0.5f},
        {AutomationWaveform::Sine, 0.5, 1.0f},
        {AutomationWaveform::Sine, 0.75, 0.5f},
        {AutomationWaveform::Triangle, 0.0, 0.0f},
        {AutomationWaveform::Triangle, 0.25, 0.5f},
        {AutomationWaveform::Triangle, 0.5, 1.0f},
        {AutomationWaveform::Triangle, 0.75, 0.5f},
        {AutomationWaveform::RampUp, 0.0, 0.0f},
        {AutomationWaveform::RampUp, 0.25, 0.25f},
        {AutomationWaveform::RampDown, 0.0, 1.0f},
        {AutomationWaveform::RampDown, 0.75, 0.25f},
        {AutomationWaveform::Square, 0.0, 0.0f},
        {AutomationWaveform::Square, 0.499, 0.0f},
        {AutomationWaveform::Square, 0.5, 1.0f},
        {AutomationWaveform::Square, 0.999, 1.0f},
    };
    for (const Sample& sample : samples)
    {
        expect(near(ParameterAutomation::sample(sample.waveform, sample.phase), sample.expected),
               "waveform samples match their documented shape");
        expect(near(ParameterAutomation::sample(sample.waveform, sample.phase + 17.0), sample.expected),
               "positive whole cycles wrap without changing the sample");
        expect(near(ParameterAutomation::sample(sample.waveform, sample.phase - 17.0), sample.expected),
               "negative whole cycles wrap without changing the sample");
    }

    constexpr AutomationWaveform waveforms[] = {
        AutomationWaveform::Sine, AutomationWaveform::Triangle,
        AutomationWaveform::RampUp, AutomationWaveform::RampDown, AutomationWaveform::Square,
    };
    for (AutomationWaveform waveform : waveforms)
    {
        bool bounded = true;
        for (int index = -1000; index <= 1000; ++index)
        {
            const float value = ParameterAutomation::sample(waveform, index / 999.0);
            bounded = bounded && std::isfinite(value) && value >= 0.0f && value <= 1.0f;
        }
        expect(bounded, "waveform stays finite and normalized across cycle boundaries");
        expect(std::isfinite(ParameterAutomation::sample(waveform,
            std::numeric_limits<double>::quiet_NaN())), "invalid preview phase has a finite fallback");
    }
    expect(near(ParameterAutomation::sample(AutomationWaveform::RampUp,
        -std::numeric_limits<double>::denorm_min()), 0.0),
        "negative subnormal phases cannot round beyond the end of the cycle");
}

void checkClock()
{
    atemfx::ParameterAutomation automation;
    automation.advance(1.0);
    expect(near(automation.phase(), 0.0), "disabled automation does not advance");
    automation.enabled = true;
    automation.advance(1.0);
    expect(near(automation.phase(), 0.25), "period controls cycle duration");
    automation.advance(9.0);
    expect(near(automation.phase(), 0.5), "elapsed time may cover several cycles");
    automation.paused = true;
    automation.advance(9.0);
    expect(near(automation.phase(), 0.5), "pause freezes phase");
    automation.phaseOffset = 0.75f;
    expect(near(automation.phase(), 0.25), "offset wraps the effective phase");
    automation.restart();
    expect(near(automation.phase(), 0.75), "restart preserves phase offset");
    expect(automation.paused, "restart preserves pause state");
    automation.paused = false;
    automation.advance(1.0);
    expect(near(automation.phase(), 0.0), "resume advances from the restarted clock");
    automation.phaseOffset = -0.25f;
    expect(near(automation.phase(), 0.0), "negative offsets wrap");

    automation.phaseOffset = 0.0f;
    automation.restart();
    automation.advance(1.0);
    automation.advance(-1.0);
    automation.advance(std::numeric_limits<double>::quiet_NaN());
    automation.advance(std::numeric_limits<double>::infinity());
    expect(near(automation.phase(), 0.25), "invalid elapsed time leaves the clock intact");
    automation.enabled = false;
    automation.advance(3.0);
    expect(near(automation.phase(), 0.25), "disabling retains the stopped phase");
    automation.enabled = true;
    automation.advance(1.0);
    expect(near(automation.phase(), 0.5), "enabling resumes the stopped phase");

    automation.restart();
    automation.periodSeconds = 0.0;
    automation.advance(0.025);
    expect(near(automation.periodSeconds, 0.05) && near(automation.phase(), 0.5),
           "zero period is bounded to the minimum");
    automation.restart();
    automation.periodSeconds = -3.0;
    automation.advance(0.025);
    expect(near(automation.phase(), 0.5), "negative periods cannot reverse or corrupt the clock");
    automation.restart();
    automation.periodSeconds = 99999.0;
    automation.advance(300.0);
    expect(near(automation.periodSeconds, 600.0) && near(automation.phase(), 0.5),
           "period is bounded to the maximum");
    automation.restart();
    automation.periodSeconds = std::numeric_limits<double>::quiet_NaN();
    automation.advance(1.0);
    expect(near(automation.phase(), 0.25), "nonfinite period falls back to four seconds");
    automation.phaseOffset = std::numeric_limits<float>::infinity();
    expect(near(automation.phase(), 0.25), "nonfinite phase offset is treated as zero");
    automation.advance(std::numeric_limits<double>::max());
    expect(std::isfinite(automation.phase()) && automation.phase() >= 0.0 && automation.phase() < 1.0,
           "extreme finite elapsed time cannot overflow the phase");

    automation.restart();
    automation.phaseOffset = 0.0f;
    automation.periodSeconds = 7.0;
    constexpr int frames = 1000000;
    constexpr double delta = 1001.0 / 60000.0;
    for (int index = 0; index < frames; ++index) automation.advance(delta);
    expect(near(automation.phase(), std::fmod(frames * delta, 7.0) / 7.0, 0.00000001),
           "a million frames retain a bounded, accurate cycle clock");
}

void checkParameters()
{
    using atemfx::AutomationWaveform;
    using atemfx::Parameter;
    Parameter amount = Parameter::makeFloat("amount", "Amount", 0.3f, 0.0f, 1.0f);
    expect(near(amount.currentValue(), 0.3), "manual parameters retain their value");
    amount.automation.enabled = true;
    expect(!amount.isDefault(), "an enabled loop is not the default state");
    amount.automation.minValue = 0.2f;
    amount.automation.maxValue = 0.8f;
    amount.automation.advance(1.0);
    expect(near(amount.currentValue(), 0.5), "loop maps the waveform into its selected range");
    expect(near(amount.value, 0.3), "evaluation preserves the stored manual value");
    amount.setFloat(0.4f);
    expect(near(amount.currentValue(), 0.5), "manual editing does not interrupt enabled automation");
    amount.automation.enabled = false;
    expect(near(amount.currentValue(), 0.4), "disabling restores the latest manual value");
    amount.automation.enabled = true;
    amount.automation.minValue = -10.0f;
    amount.automation.maxValue = 10.0f;
    amount.automation.restart();
    expect(near(amount.currentValue(), 0.0), "automation lower endpoint is clamped to the parameter range");
    amount.automation.advance(2.0);
    expect(near(amount.currentValue(), 1.0), "automation upper endpoint is clamped to the parameter range");
    amount.automation.minValue = 0.8f;
    amount.automation.maxValue = 0.2f;
    amount.automation.restart();
    expect(near(amount.currentValue(), 0.2), "inverted automation endpoints are ordered safely");
    amount.automation.minValue = std::numeric_limits<float>::quiet_NaN();
    amount.automation.maxValue = std::numeric_limits<float>::infinity();
    amount.automation.advance(1.0);
    expect(near(amount.currentValue(), 0.5), "nonfinite automation bounds fall back to the parameter range");

    Parameter integer = Parameter::makeInt("mode", "Mode", 0, -4, 4);
    expect(near(integer.automation.minValue, -4.0) && near(integer.automation.maxValue, 4.0),
           "integer factories initialize automation bounds");
    integer.automation.enabled = true;
    integer.automation.waveform = AutomationWaveform::RampUp;
    integer.automation.advance(0.75);
    expect(near(integer.currentValue(), -3.0) && integer.asInt() == -3,
           "negative integer half steps round away from zero");
    integer.automation.advance(2.0);
    expect(near(integer.currentValue(), 2.0) && integer.asInt() == 2,
           "positive integer half steps round away from zero");
    Parameter integerLimit = Parameter::makeInt("limit", "Limit",
        std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    expect(integerLimit.asInt() == std::numeric_limits<int>::max(),
           "integer upper limit converts without overflow");

    Parameter radial = Parameter::makeBool("radial", "Radial", true);
    expect(radial.automation.waveform == AutomationWaveform::Square, "bool automation defaults to square wave");
    radial.automation.enabled = true;
    expect(!radial.asBool() && near(radial.asFloat(), 0.0), "boolean loop begins off");
    radial.automation.advance(2.0);
    expect(radial.asBool() && near(radial.currentValue(), 1.0), "boolean loop switches on at half cycle");
    radial.automation.waveform = AutomationWaveform::RampUp;
    radial.automation.restart();
    radial.automation.advance(1.0);
    expect(near(radial.currentValue(), 0.0), "continuous waves quantize boolean output below threshold");
    radial.automation.advance(1.0);
    expect(near(radial.currentValue(), 1.0), "continuous waves quantize boolean output at threshold");

    Parameter copy = amount;
    copy.automation.advance(1.0);
    expect(near(copy.currentValue(), 1.0) && near(amount.currentValue(), 0.5),
           "copied parameters own independent automation clocks");
    amount.automation.paused = true;
    amount.automation.phaseOffset = 0.3f;
    amount.automation.periodSeconds = 18.0;
    amount.automation.waveform = AutomationWaveform::RampDown;
    amount.reset();
    expect(amount.isDefault() && near(amount.currentValue(), 0.3), "reset restores the manual default");
    expect(!amount.automation.enabled && !amount.automation.paused && near(amount.automation.phase(), 0.0),
           "reset disables and clears the automation clock");
    expect(near(amount.automation.periodSeconds, 4.0)
           && near(amount.automation.minValue, amount.minValue)
           && near(amount.automation.maxValue, amount.maxValue)
           && amount.automation.waveform == AutomationWaveform::Sine,
           "reset restores automation settings and parameter-specific bounds");
    radial.reset();
    expect(radial.automation.waveform == AutomationWaveform::Square && radial.asBool(),
           "reset restores boolean waveform and manual default");

    amount.setFloat(std::numeric_limits<float>::quiet_NaN());
    expect(near(amount.currentValue(), 0.3), "invalid manual value uses the finite default");
    amount.setFloat(2.0f);
    expect(near(amount.currentValue(), 1.0), "manual output respects the parameter range");
}

void checkParameterSet()
{
    atemfx::ParameterSet parameters;
    parameters.add(atemfx::Parameter::makeFloat("amount", "Amount", 0.3f, 0.0f, 1.0f));
    parameters.add(atemfx::Parameter::makeBool("radial", "Radial", true));
    parameters.find("amount")->automation.enabled = true;
    parameters.find("radial")->automation.enabled = true;
    parameters.find("radial")->automation.paused = true;
    expect(parameters.activeAutomationCount() == 2, "active count includes enabled paused loops");
    parameters.advanceAutomations(1.0);
    expect(near(parameters.valueOr("amount", -1.0f), 0.5), "valueOr reads the automated value");
    expect(near(parameters.valueOr("radial", -1.0f), 0.0), "parameter sets respect individual pause states");
    expect(near(parameters.valueOr("missing", -1.0f), -1.0), "missing parameters preserve fallback behavior");
    parameters.find("amount")->automation.periodSeconds = 8.0;
    parameters.find("radial")->automation.periodSeconds = 2.0;
    parameters.find("radial")->automation.phaseOffset = 0.5f;
    parameters.find("radial")->automation.paused = false;
    parameters.advanceAutomations(2.0);
    expect(near(parameters.find("amount")->automation.phase(), 0.5)
           && near(parameters.find("radial")->automation.phase(), 0.5),
           "parameters use independent periods and offsets in the same frame");
    parameters.advanceAutomations(1.0);
    expect(near(parameters.find("amount")->automation.phase(), 0.625)
           && near(parameters.find("radial")->automation.phase(), 0.0),
           "different periods remain independent across consecutive frames");
    parameters.resetAll();
    expect(parameters.activeAutomationCount() == 0 && near(parameters.valueOr("amount", -1.0f), 0.3)
           && near(parameters.valueOr("radial", -1.0f), 1.0), "resetAll resets every clock and manual value");
}

} // namespace

int main()
{
    checkWaveforms();
    checkClock();
    checkParameters();
    checkParameterSet();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d of %d parameter automation checks failed\n", failures, checks);
        return 1;
    }
    std::printf("All %d parameter automation checks passed\n", checks);
    return 0;
}
