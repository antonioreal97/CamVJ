// Test pattern generator — stands in for DeckLink capture until M1.
//
// Three animated patterns, plus motion markers that make judder and dropped frames
// visible by eye: a sweeping bar and an orbiting dot. If either stutters, the
// pipeline is not holding its frame budget.
//
// Parameters:
//   0  pattern  0..3   0 bars, 1 plasma, 2 grid, 3 LED mapping (static)
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

// Block glyphs keep the calibration labels self-contained and sharp at 1080p.
// 0..9, colon and x; each bit is one cell, row-major from the top left.
static const uint kMappingGlyphs[12] =
{
    31599u, 29850u, 29671u, 31207u, 18925u, 31183u, 31695u, 18727u, 31727u, 31215u, 1040u, 2728u
};

float mappingGlyph(float2 p, int glyph)
{
    if (p.x < 0.0 || p.x >= 3.0 || p.y < 0.0 || p.y >= 5.0)
    {
        return 0.0;
    }

    int bitIndex = int(floor(p.y)) * 3 + int(floor(p.x));
    return float((kMappingGlyphs[glyph] >> bitIndex) & 1u);
}

float mappingAspectLabel(float2 p, float2 origin, bool portrait)
{
    float2 local = (p - origin) / 12.0;
    if (local.x < 0.0 || local.x >= 15.0 || local.y < 0.0 || local.y >= 5.0)
    {
        return 0.0;
    }

    int slot = int(floor(local.x / 4.0));
    int glyph = portrait
        ? (slot == 0 ? 9 : slot == 1 ? 10 : slot == 2 ? 1 : 6)
        : (slot == 0 ? 1 : slot == 1 ? 6 : slot == 2 ? 10 : 9);
    return mappingGlyph(local - float2(float(slot) * 4.0, 0.0), glyph);
}

float mappingRasterLabel(float2 p)
{
    float2 local = (p - float2(96.0, 152.0)) / 5.0;
    if (local.x < 0.0 || local.x >= 35.0 || local.y < 0.0 || local.y >= 5.0)
    {
        return 0.0;
    }

    int slot = int(floor(local.x / 4.0));
    // 1920x1080, in the same glyph grid as the aspect labels.
    int glyph = slot == 0 || slot == 5 ? 1
              : slot == 1 ? 9 : slot == 2 ? 2
              : slot == 4 ? 11 : slot == 7 ? 8 : 0;
    return mappingGlyph(local - float2(float(slot) * 4.0, 0.0), glyph);
}

float3 ledMapping(float2 uv)
{
    // The panel mapping uses the fixed engine canvas, including the fractional
    // portrait edges (656.25 and 1263.75), not an inset approximation.
    float2 p = uv * float2(1920.0, 1080.0);
    float2 centred = p - float2(960.0, 540.0);
    float2 edge = float2(960.0, 540.0) - abs(centred);
    float2 portraitEdge = float2(303.75, 540.0) - abs(centred);
    float3 cyan = float3(23.0, 169.0, 224.0) / 255.0;
    float3 magenta = float3(230.0, 36.0, 102.0) / 255.0;
    float3 white = float3(0.90, 0.92, 0.95);

    // Sixteen by nine square cells make non-uniform scaling immediately visible.
    float2 cellIndex = floor(p / 120.0);
    float parity = cellIndex.x + cellIndex.y;
    parity -= floor(parity * 0.5) * 2.0;
    float3 colour = float3(10.0, 11.0, 13.0) / 255.0 + parity * 0.018;
    float2 cell = p - cellIndex * 120.0;
    if (min(min(cell.x, cell.y), min(120.0 - cell.x, 120.0 - cell.y)) < 1.0)
    {
        colour = float3(0.16, 0.18, 0.21);
    }

    float radius = length(centred);
    if (abs(radius - 120.0) < 2.5 ||
        (abs(centred.x) < 2.5 && abs(centred.y) < 42.0) ||
        (abs(centred.y) < 2.5 && abs(centred.x) < 42.0))
    {
        colour = white;
    }

    // Quiet plates preserve label legibility over the checker and grid.
    if ((p.x >= 76.0 && p.x < 296.0 && p.y >= 52.0 && p.y < 197.0) ||
        (p.x >= 850.0 && p.x < 1070.0 && p.y >= 52.0 && p.y < 152.0))
    {
        colour = float3(10.0, 11.0, 13.0) / 255.0;
    }
    if (mappingAspectLabel(p, float2(96.0, 72.0), false) > 0.5 ||
        mappingRasterLabel(p) > 0.5)
    {
        colour = cyan;
    }
    if (mappingAspectLabel(p, float2(870.0, 72.0), true) > 0.5)
    {
        colour = magenta;
    }

    float2 tick = abs(p - floor(p / 120.0 + 0.5) * 120.0);
    bool landscapeBorder = min(edge.x, edge.y) < 5.0;
    bool landscapeCorner = (edge.x < 14.0 && edge.y < 56.0) ||
                           (edge.y < 14.0 && edge.x < 56.0);
    bool landscapeTick = (edge.y < 24.0 && tick.x < 2.0) ||
                         (edge.x < 24.0 && tick.y < 2.0);
    if (landscapeBorder || landscapeCorner || landscapeTick)
    {
        colour = cyan;
    }

    // Draw each border inward so its outside edge is the actual mapping limit.
    if (portraitEdge.x >= 0.0 && portraitEdge.y >= 0.0)
    {
        bool portraitBorder = min(portraitEdge.x, portraitEdge.y) < 5.0;
        bool portraitCorner = (portraitEdge.x < 14.0 && portraitEdge.y < 56.0) ||
                              (portraitEdge.y < 14.0 && portraitEdge.x < 56.0);
        bool portraitTick = (portraitEdge.y < 24.0 && tick.x < 2.0) ||
                            (portraitEdge.x < 24.0 && tick.y < 2.0);
        if (portraitBorder || portraitCorner || portraitTick)
        {
            colour = magenta;
        }
    }

    // Midpoint ticks are longer than the ruler ticks and share the centre cross.
    if ((edge.x < 40.0 && abs(centred.y) < 3.0) ||
        (edge.y < 40.0 && abs(centred.x) < 3.0) ||
        (portraitEdge.x >= 0.0 && portraitEdge.x < 40.0 && abs(centred.y) < 3.0))
    {
        colour = white;
    }

    return colour;
}

float4 main(VSOutput input) : SV_Target
{
    int   pattern = (int)(uParams[0].x + 0.5);
    float speed   = uParams[0].y;
    float markers = uParams[0].z;

    float  t  = uTime * speed;
    float2 uv = input.uv;

    float3 colour;
    if (pattern == 3)
    {
        return float4(ledMapping(uv), 1.0);
    }

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
