#pragma once

#include <cstddef>
#include <cstdint>

namespace atemfx {

// Maximum number of scalar parameters an effect can pack into the shared
// constant buffer. Four float4 registers.
inline constexpr std::size_t kMaxEffectParameters = 16;

// CPU mirror of the EffectCB declared in shaders/common.hlsli.
// Both must change together. Size must stay a multiple of 16 bytes.
struct EffectConstants
{
    float resolution[2]    = {0.0f, 0.0f};
    float invResolution[2] = {0.0f, 0.0f};
    float time             = 0.0f;
    float deltaTime        = 0.0f;
    float pad0[2]          = {0.0f, 0.0f};
    float params[4][4]     = {};
};

static_assert(sizeof(EffectConstants) == 96, "EffectConstants must match EffectCB");
static_assert(sizeof(EffectConstants) % 16 == 0, "Constant buffers must be 16-byte aligned");

// Per-frame values, identical for every pass in the frame.
inline void setFrameConstants(EffectConstants& constants,
                              std::uint32_t    width,
                              std::uint32_t    height,
                              float            time,
                              float            deltaTime)
{
    constants.resolution[0]    = static_cast<float>(width);
    constants.resolution[1]    = static_cast<float>(height);
    constants.invResolution[0] = width > 0 ? 1.0f / static_cast<float>(width) : 0.0f;
    constants.invResolution[1] = height > 0 ? 1.0f / static_cast<float>(height) : 0.0f;
    constants.time             = time;
    constants.deltaTime        = deltaTime;
}

// Scalar parameter `index` lands in uParams[index / 4][index % 4].
inline void setParameterConstant(EffectConstants& constants, std::size_t index, float value)
{
    if (index < kMaxEffectParameters)
    {
        constants.params[index / 4][index % 4] = value;
    }
}

// Packs a range of raw values in order. Callers that also have internal,
// non-user constants (a capture source needs the frame's own dimensions) pack
// their parameters first and then set the remaining slots by hand.
template <typename Iterator>
inline void setParameterConstants(EffectConstants& constants,
                                  Iterator         begin,
                                  Iterator         end,
                                  std::size_t      firstIndex = 0)
{
    std::size_t index = firstIndex;
    for (Iterator it = begin; it != end && index < kMaxEffectParameters; ++it, ++index)
    {
        setParameterConstant(constants, index, *it);
    }
}

} // namespace atemfx
