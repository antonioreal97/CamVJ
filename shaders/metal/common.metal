// ATEM FX — shared Metal shader declarations.
//
// MSL compiled from source at runtime has no include search path, so this file
// is prepended to every effect source by MetalShaderLibrary rather than being
// #included. Effect files therefore contain only their fragment function.
//
// EffectConstants must match src/gpu/EffectConstants.h byte for byte:
// float2(8) float2(8) float(4) float(4) float2(8) then float4[4](64) = 96.

#include <metal_stdlib>
using namespace metal;

struct EffectConstants
{
    float2 resolution;      // processing resolution, pixels
    float2 invResolution;   // 1.0 / resolution
    float  time;            // seconds since application start
    float  deltaTime;       // seconds since previous frame
    float2 pad0;
    float4 params[4];       // 16 scalar parameter slots
};

// Parameter i, in the order the effect declared it, is params[i / 4][i % 4].

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

// Fullscreen triangle from the vertex id: no vertex buffer, no index buffer.
// Metal's clip space and texture origin match Direct3D's, so this is the same
// arithmetic as fullscreen.hlsl.
vertex VSOutput fullscreen_vertex(uint vertexId [[vertex_id]])
{
    const float2 uv = float2(float((vertexId << 1) & 2u), float(vertexId & 2u));

    VSOutput output;
    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

// MSL has no radians(); HLSL does. Spelled out here so the two shader sets
// stay line-for-line comparable.
static inline float toRadians(float degrees)
{
    return degrees * 0.017453292519943295;
}

// Multiply a UV-space offset by this to keep motion isotropic on a non-square
// frame.
static inline float2 aspectScale(constant EffectConstants& c)
{
    return float2(1.0, c.resolution.x / c.resolution.y);
}
