// Face tile — one face crop drawn as a sprite (SpritePass).
// See shaders/hlsl/face_tile.hlsl; the two must produce the same image.
//
// Parameters (packed by FaceMosaicEffect):
//   0  feather    0..1  soft edge width, fraction of the tile's half extent
//   1  roundness  0..1  corner radius, fraction of the tile's half extent
//
// Output is premultiplied: the sprite pipeline blends source-over with
// (One, OneMinusSourceAlpha).

// Signed distance to a rounded box in the tile's -1..1 space; negative inside.
static inline float roundedBox(float2 p, float radius)
{
    const float2 q = abs(p) - (1.0 - radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

fragment float4 sprite_fragment(SpriteVSOutput in [[stage_in]],
                                constant EffectConstants& c [[buffer(0)]],
                                texture2d<float> source [[texture(0)]],
                                sampler samp [[sampler(0)]])
{
    const float feather = max(saturate(c.params[0].x), 0.002);
    const float radius  = saturate(c.params[0].y);

    const float distance = roundedBox(in.local, radius);
    const float coverage = 1.0 - smoothstep(-feather, 0.0, distance);

    const float  alpha  = coverage * saturate(in.opacity);
    const float3 colour = source.sample(samp, saturate(in.uv)).rgb;
    return float4(colour * alpha, alpha);
}
