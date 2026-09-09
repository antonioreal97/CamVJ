#pragma once

// Generic, UI-renderable effect parameters.
//
// This header is deliberately free of any Direct3D or Win32 dependency: it is
// the contract between an effect and the user interface, and the UI must be
// able to render an effect written after the UI was compiled.

#include "effects/parameter_automation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atemfx {

enum class ParameterType
{
    Float,
    Int,
    Bool,
};

// A single scalar control. Carries its own range and default so no other code
// has to know what it means.
struct Parameter
{
    std::string   id;
    std::string   label;
    ParameterType type = ParameterType::Float;

    float value        = 0.0f;
    float defaultValue = 0.0f;
    float minValue     = 0.0f;
    float maxValue     = 1.0f;
    ParameterAutomation automation;

    // Optional names for a zero-based integer selection. The value stays a
    // scalar, so shader packing and automation use the same contract.
    std::vector<std::string> choices;

    static Parameter makeFloat(std::string id, std::string label,
                               float defaultValue, float minValue, float maxValue)
    {
        Parameter p;
        p.id           = std::move(id);
        p.label        = std::move(label);
        p.type         = ParameterType::Float;
        p.value        = defaultValue;
        p.defaultValue = defaultValue;
        p.minValue     = minValue;
        p.maxValue     = maxValue;
        p.automation.minValue = p.minValue;
        p.automation.maxValue = p.maxValue;
        return p;
    }

    static Parameter makeInt(std::string id, std::string label,
                             int defaultValue, int minValue, int maxValue)
    {
        Parameter p;
        p.id           = std::move(id);
        p.label        = std::move(label);
        p.type         = ParameterType::Int;
        p.value        = static_cast<float>(defaultValue);
        p.defaultValue = static_cast<float>(defaultValue);
        p.minValue     = static_cast<float>(minValue);
        p.maxValue     = static_cast<float>(maxValue);
        p.automation.minValue = p.minValue;
        p.automation.maxValue = p.maxValue;
        return p;
    }

    static Parameter makeBool(std::string id, std::string label, bool defaultValue)
    {
        Parameter p;
        p.id           = std::move(id);
        p.label        = std::move(label);
        p.type         = ParameterType::Bool;
        p.value        = defaultValue ? 1.0f : 0.0f;
        p.defaultValue = p.value;
        p.minValue     = 0.0f;
        p.maxValue     = 1.0f;
        p.automation.waveform = AutomationWaveform::Square;
        return p;
    }

    static Parameter makeChoice(std::string id, std::string label,
                                int defaultValue, std::vector<std::string> choices)
    {
        const int maxValue = choices.empty() ? 0 : static_cast<int>(choices.size()) - 1;
        Parameter p = makeInt(std::move(id), std::move(label),
                              std::clamp(defaultValue, 0, maxValue), 0, maxValue);
        p.choices = std::move(choices);
        return p;
    }

    float currentValue() const noexcept
    {
        float low = std::isfinite(minValue) ? minValue : 0.0f;
        float high = std::isfinite(maxValue) ? maxValue : 1.0f;
        if (low > high) std::swap(low, high);

        float effective = std::isfinite(value) ? value
            : (std::isfinite(defaultValue) ? defaultValue : 0.0f);
        if (automation.enabled)
        {
            float from = std::isfinite(automation.minValue)
                ? std::clamp(automation.minValue, low, high) : low;
            float to = std::isfinite(automation.maxValue)
                ? std::clamp(automation.maxValue, low, high) : high;
            if (from > to) std::swap(from, to);

            // Double intermediates keep interpolation finite for every finite
            // float range without modifying the operator's manual value.
            effective = static_cast<float>(static_cast<double>(from)
                + (static_cast<double>(to) - from) * automation.normalizedValue());
        }
        effective = std::clamp(effective, low, high);
        if (type == ParameterType::Int) return std::round(effective);
        if (type == ParameterType::Bool) return effective >= 0.5f ? 1.0f : 0.0f;
        return effective;
    }

    float asFloat() const noexcept { return currentValue(); }
    int asInt() const noexcept
    {
        // float cannot represent INT_MAX exactly; clamp in double before the
        // conversion to avoid an out-of-range cast for a valid integer limit.
        const double rounded = std::round(static_cast<double>(currentValue()));
        return static_cast<int>(std::clamp(rounded,
            static_cast<double>(std::numeric_limits<int>::min()),
            static_cast<double>(std::numeric_limits<int>::max())));
    }
    bool asBool() const noexcept { return currentValue() >= 0.5f; }

    void setFloat(float v) { value = v; }
    void setInt(int v) { value = static_cast<float>(v); }
    void setBool(bool v) { value = v ? 1.0f : 0.0f; }

    void reset() noexcept
    {
        value = defaultValue;
        automation = {};
        automation.minValue = minValue;
        automation.maxValue = maxValue;
        if (type == ParameterType::Bool) automation.waveform = AutomationWaveform::Square;
    }
    bool isDefault() const noexcept { return !automation.enabled && value == defaultValue; }
};

// An ordered list of parameters. Order matters: it is both the display order
// and the packing order into the shader constant buffer.
class ParameterSet
{
public:
    Parameter& add(Parameter parameter)
    {
        parameters_.push_back(std::move(parameter));
        return parameters_.back();
    }

    Parameter* find(std::string_view id)
    {
        for (Parameter& p : parameters_)
        {
            if (p.id == id)
            {
                return &p;
            }
        }
        return nullptr;
    }

    const Parameter* find(std::string_view id) const
    {
        return const_cast<ParameterSet*>(this)->find(id);
    }

    // Convenience for effect implementations that know their own parameters.
    // Returns the fallback rather than throwing if the id is unknown, because
    // this may be reached from the per-frame path.
    float valueOr(std::string_view id, float fallback) const
    {
        const Parameter* p = find(id);
        return p ? p->currentValue() : fallback;
    }

    void advanceAutomations(double deltaSeconds) noexcept
    {
        for (Parameter& p : parameters_) p.automation.advance(deltaSeconds);
    }

    std::size_t activeAutomationCount() const noexcept
    {
        std::size_t count = 0;
        for (const Parameter& p : parameters_)
            if (p.automation.enabled) ++count;
        return count;
    }

    void resetAll()
    {
        for (Parameter& p : parameters_)
        {
            p.reset();
        }
    }

    std::vector<Parameter>&       all() { return parameters_; }
    const std::vector<Parameter>& all() const { return parameters_; }

    std::size_t size() const { return parameters_.size(); }
    bool        empty() const { return parameters_.empty(); }

private:
    std::vector<Parameter> parameters_;
};

// Identity of an effect type, used by the registry and the UI.
struct EffectDescriptor
{
    std::string typeId;       // stable, used by presets: "rgb_split"
    std::string displayName;  // shown in the UI: "RGB Split"
    std::string category;     // grouping in the add menu: "Distort"
    std::string description;  // one line of help
};

} // namespace atemfx
