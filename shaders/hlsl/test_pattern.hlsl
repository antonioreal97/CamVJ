// Test pattern generator — stands in for DeckLink capture until M1.
//
// Three patterns, plus motion markers that make judder and dropped frames
// visible by eye: a sweeping bar and an orbiting dot. If either stutters, the
// pipeline is not holding its frame budget.
//
// Parameters:
//   0  pattern  0..2   0 bars, 1 plasma, 2 grid
//   1  speed    0..4   animation rate multiplier
//   2  markers  0/1    draw the motion markers

#include "common.hlsli"

static const float3 kBars[8] =
{
    float3(1.00, 1.00, 1.00),  // white
    float3(1.00, 1.00, 0.00),  // yellow
    float3(0.00, 1.00, 1.00),  // cyan
    float3(0.00, 1.00, 0.00),  // green
    float3(1.00, 0.00, 1.00),  // magenta
    float3(1.00, 0.00, 0.00),  // red
    float3(0.00, 0.00, 1.00),  // blue
    float3(0.05, 0.05, 0.05),  // near black
};

float3 colourBars(float2 uv, float t)
{
    // Upper three quarters: bars, slowly scrolling so motion is always present.
    float scrolled = frac(uv.x + t * 0.05);
    int   index    = clamp((int)(scrolled * 8.0), 0, 7);
    float3 colour  = kBars[index];

    // Lower quarter: a luminance ramp, for banding and black level.
    if (uv.y > 0.75)
    {
        float ramp = saturate(uv.x);
        colour = float3(ramp, ramp, ramp);
    }

    return colour;
}

float3 plasma(float2 uv, float t)
{
    float v = sin(uv.x * 10.0 + t)
            + sin(uv.y * 12.0 - t * 1.3)
            + sin((uv.x + uv.y) * 8.0 + t * 0.7)
            + sin(length(uv - 0.5) * 20.0 - t * 2.0);

    v *= 0.25 * 3.14159265;

    return 0.5 + 0.5 * float3(sin(v), sin(v + 2.09439), sin(v + 4.18879));
}

float3 grid(float2 uv, float t)
{
    float2 p = uv * float2(uResolution.x / uResolution.y, 1.0) * 16.0
             + float2(t * 0.5, t * 0.25);

    float2 cell  = frac(p);
    float  lines = step(0.94, max(cell.x, cell.y));

    return float3(0.04, 0.04, 0.06) + lines * float3(0.0, 0.85, 0.55);
}

float4 main(VSOutput input) : SV_Target
{
    int   pattern = (int)(uParams[0].x + 0.5);
    float speed   = uParams[0].y;
    float markers = uParams[0].z;

    float  t  = uTime * speed;
    float2 uv = input.uv;

    float3 colour;
    if (pattern == 0)
    {
        colour = colourBars(uv, t);
    }
    else if (pattern == 1)
    {
        colour = plasma(uv, t);
    }
    else
    {
        colour = grid(uv, t);
    }

    if (markers > 0.5)
    {
        // Sweeping vertical bar: one pass every four seconds at speed 1.
        float sweep = frac(t * 0.25);
        float bar   = smoothstep(0.0035, 0.0, abs(uv.x - sweep));
        colour = lerp(colour, float3(1.0, 1.0, 1.0), bar);

        // Orbiting dot: circular motion exposes judder better than a bar.
        float2 centred = (uv - 0.5) * float2(uResolution.x / uResolution.y, 1.0);
        float2 orbit   = float2(cos(t * 1.5), sin(t * 1.5)) * 0.18;
        float  dot_    = length(centred - orbit);
        colour = lerp(colour, float3(1.0, 0.25, 0.1), smoothstep(0.022, 0.014, dot_));
    }

    return float4(colour, 1.0);
}
