#pragma once

#include <cstdint>
#include <cstring>

namespace atemfx {

// IEEE 754 binary16 to binary32. The processing targets are RGBA16F, so any
// readback for a screenshot or a self-test has to come back through here.
inline float halfToFloat(uint16_t value)
{
    const uint32_t sign     = static_cast<uint32_t>(value >> 15) & 0x1u;
    const uint32_t exponent = static_cast<uint32_t>(value >> 10) & 0x1Fu;
    const uint32_t mantissa = static_cast<uint32_t>(value) & 0x3FFu;

    uint32_t bits = 0;

    if (exponent == 0)
    {
        if (mantissa != 0)
        {
            // Subnormal: normalise it into a binary32 exponent.
            uint32_t shifted = mantissa;
            int      shift   = 0;
            while ((shifted & 0x400u) == 0)
            {
                shifted <<= 1;
                ++shift;
            }
            shifted &= 0x3FFu;
            bits = (sign << 31) | (static_cast<uint32_t>(127 - 15 - shift) << 23) | (shifted << 13);
        }
        else
        {
            bits = sign << 31;
        }
    }
    else if (exponent == 31)
    {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = (sign << 31) | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

} // namespace atemfx
