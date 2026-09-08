// Subpixel — luminance-gated RGB cells that float off the grid.
//
// One sample per fragment, taken at the cell centre. R, G and B are drawn as
// separate sprites that orbit inside that cell. A neighbourhood gather would
// let sprites cross into empty cells, but at 1920x1080 it blows the 16.68 ms
// budget; this stays in the same cost class as pixelate.
//
// Parameters:
//   0  grid         4..128   cell size in pixels
//   1  threshold    0..1     luma below this emits nothing
//   2  scatter      0..1     how far sprites leave the cell centre
//   3  rgb_spread   0..1     R/G/B separation inside a cell
//   4  noise_scale  0..1     0 = cells share motion, 1 = independent
//   5  speed        0..4     orbit rate (0 is a frozen scatter)
//   6  stretch      0..1     widen sprites into bars
//   7  mix          0..1     original ↔ effect

#include "common.hlsli"

static const float kTwoPi = 6.28318530718;

float2 hash22(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

float rectCoverage(float2 p, float2 center, float2 halfSize, float aa)
{
    float2 d = abs(p - center) - halfSize;
    float2 w = 1.0 - smoothstep(float2(-aa, -aa), float2(aa, aa), d);
    return saturate(w.x) * saturate(w.y);
}

float4 main(VSOutput input) : SV_Target
{
    float size       = max(uParams[0].x, 4.0);
    float threshold  = saturate(uParams[0].y);
    float scatter    = saturate(uParams[0].z);
    float rgbSpread  = saturate(uParams[0].w);
    float noiseScale = saturate(uParams[1].x);
    float speed      = uParams[1].y;
    float stretch    = saturate(uParams[1].z);
    float amount     = saturate(uParams[1].w);

    float2 blocks   = uResolution / size;
    float2 cellP    = input.uv * blocks;
    float2 cell     = floor(cellP);
    float2 cellUv   = (cell + 0.5) / blocks;
    float4 original = gSource.SampleLevel(gSampler, input.uv, 0);
    float4 src      = gSource.SampleLevel(gSampler, cellUv, 0);

    float luma = dot(src.rgb, float3(0.299, 0.587, 0.114));
    float mask = smoothstep(threshold, threshold + 0.10, luma);

    float grouping = lerp(6.0, 1.0, noiseScale);
    grouping = max(grouping, 1.0);

    float2 h     = hash22(floor(cell / grouping));
    float2 orbit = float2(sin(uTime * speed + h.x * kTwoPi),
                          cos(uTime * speed * 0.81 + h.y * kTwoPi));
    float2 offset = (h * 2.0 - 1.0) * (scatter * 0.35) + orbit * (scatter * 0.28);
    float2 center = cell + 0.5 + offset;

    float2 halfSprite = float2(0.18 + stretch * 0.42, max(0.18 * (1.0 - stretch * 0.45), 0.05));
    float  aa         = 0.70 / size;
    float  spread     = rgbSpread * 0.38;

    float3 acc = float3(
        rectCoverage(cellP, center + float2(-spread, 0.0), halfSprite, aa) * src.r,
        rectCoverage(cellP, center, halfSprite, aa) * src.g,
        rectCoverage(cellP, center + float2(spread, 0.0), halfSprite, aa) * src.b) * mask;

    return lerp(original, float4(saturate(acc), original.a), amount);
}
