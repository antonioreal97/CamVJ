// CRT — scanlines, aperture grille and a light chromatic split.
//
// This is a display look, not a bloom. Real glow needs a downsample the RHI
// does not offer; the contrast lift is the cheap substitute. Keep it last in
// the chain so it sits on the already-processed picture.
//
// Parameters:
//   0  scanlines    0..1   darken between horizontal lines
//   1  mask         0..1   RGB aperture-grille strength
//   2  aberration   0..1   red/blue split, up to ~0.4% of frame width
//   3  contrast     0.5..2
//   4  mix          0..1   original ↔ effect

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float scanlines  = saturate(c.params[0].x);
    const float maskAmount = saturate(c.params[0].y);
    const float aberration = saturate(c.params[0].z);
    const float contrast   = clamp(c.params[0].w, 0.5, 2.0);
    const float amount     = saturate(c.params[1].x);

    const float2 ca     = float2(aberration * 0.004, 0.0) * aspectScale(c);
    const float  r      = source.sample(samp, in.uv + ca).r;
    const float4 centre = source.sample(samp, in.uv);
    const float  b      = source.sample(samp, in.uv - ca).b;
    float3 rgb          = float3(r, centre.g, b);

    float scan = sin(in.uv.y * c.resolution.y * 3.14159265);
    scan = mix(1.0, scan * 0.5 + 0.5, scanlines);
    rgb *= scan;

    const float slot = floor(fract(in.uv.x * c.resolution.x / 3.0) * 3.0);
    const float3 grille = float3(slot < 0.5 ? 1.0 : 0.18,
                                 (slot >= 0.5 && slot < 1.5) ? 1.0 : 0.18,
                                 slot >= 1.5 ? 1.0 : 0.18);
    rgb *= mix(float3(1.0, 1.0, 1.0), grille, maskAmount);

    rgb = saturate((rgb - 0.5) * contrast + 0.5);

    return mix(centre, float4(rgb, centre.a), amount);
}
