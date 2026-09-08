// ATEM FX — shared shader declarations.
//
// Every effect and every generator shares this constant buffer layout so the
// C++ side can pack parameters generically. See docs/EFFECT_SYSTEM.md.
// The C++ mirror of this layout is src/gpu/EffectConstants.h — change both.

#ifndef ATEMFX_COMMON_HLSLI
#define ATEMFX_COMMON_HLSLI

cbuffer EffectCB : register(b0)
{
    float2 uResolution;     // processing resolution, pixels
    float2 uInvResolution;  // 1.0 / uResolution
    float  uTime;           // seconds since application start
    float  uDeltaTime;      // seconds since previous frame
    float2 uPad0;
    float4 uParams[4];      // 16 scalar parameter slots
};

// Parameter i, in the order the effect declared it, is uParams[i / 4][i % 4].

Texture2D    gSource  : register(t0);
Texture2D    gHistory : register(t1); // optional; bound by shutter-style effects
SamplerState gSampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Pixel aspect correction: multiply a UV-space offset by this to keep motion
// isotropic on a non-square frame.
float2 aspectScale()
{
    return float2(1.0, uResolution.x / uResolution.y);
}

#endif // ATEMFX_COMMON_HLSLI
