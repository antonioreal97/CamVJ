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

constant float kTwoPi = 6.28318530718;

static inline float2 hash22(float2 p)
{
    float3 p3 = fract(float3(p.x, p.y, p.x) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

static inline float rectCoverage(float2 p, float2 center, float2 halfSize, float aa)
{
    const float2 d = abs(p - center) - halfSize;
    const float2 w = 1.0 - smoothstep(float2(-aa, -aa), float2(aa, aa), d);
    return saturate(w.x) * saturate(w.y);
}

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float size       = max(c.params[0].x, 4.0);
    const float threshold  = saturate(c.params[0].y);
    const float scatter    = saturate(c.params[0].z);
    const float rgbSpread  = saturate(c.params[0].w);
    const float noiseScale = saturate(c.params[1].x);
    const float speed      = c.params[1].y;
    const float stretch    = saturate(c.params[1].z);
    const float amount     = saturate(c.params[1].w);

    const float2 blocks   = c.resolution / size;
    const float2 cellP    = in.uv * blocks;
    const float2 cell     = floor(cellP);
    const float2 cellUv   = (cell + 0.5) / blocks;
    const float4 original = source.sample(samp, in.uv, level(0.0));
    const float4 src      = source.sample(samp, cellUv, level(0.0));

    const float luma = dot(src.rgb, float3(0.299, 0.587, 0.114));
    const float mask = smoothstep(threshold, threshold + 0.10, luma);

    float grouping = mix(6.0, 1.0, noiseScale);
    grouping = max(grouping, 1.0);

    const float2 h     = hash22(floor(cell / grouping));
    const float2 orbit = float2(sin(c.time * speed + h.x * kTwoPi),
                                cos(c.time * speed * 0.81 + h.y * kTwoPi));
    const float2 offset = (h * 2.0 - 1.0) * (scatter * 0.35) + orbit * (scatter * 0.28);
    const float2 center = cell + 0.5 + offset;

    const float2 halfSprite = float2(0.18 + stretch * 0.42,
                                     max(0.18 * (1.0 - stretch * 0.45), 0.05));
    const float  aa         = 0.70 / size;
    const float  spread     = rgbSpread * 0.38;

    const float3 acc = float3(
        rectCoverage(cellP, center + float2(-spread, 0.0), halfSprite, aa) * src.r,
        rectCoverage(cellP, center, halfSprite, aa) * src.g,
        rectCoverage(cellP, center + float2(spread, 0.0), halfSprite, aa) * src.b) * mask;

    return mix(original, float4(saturate(acc), original.a), amount);
}
