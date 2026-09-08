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

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float scanlines  = saturate(uParams[0].x);
    float maskAmount = saturate(uParams[0].y);
    float aberration = saturate(uParams[0].z);
    float contrast   = clamp(uParams[0].w, 0.5, 2.0);
    float amount     = saturate(uParams[1].x);

    float2 ca     = float2(aberration * 0.004, 0.0) * aspectScale();
    float  r      = gSource.Sample(gSampler, input.uv + ca).r;
    float4 centre = gSource.Sample(gSampler, input.uv);
    float  b      = gSource.Sample(gSampler, input.uv - ca).b;
    float3 rgb    = float3(r, centre.g, b);

    float scan = sin(input.uv.y * uResolution.y * 3.14159265);
    scan = lerp(1.0, scan * 0.5 + 0.5, scanlines);
    rgb *= scan;

    float slot = floor(frac(input.uv.x * uResolution.x / 3.0) * 3.0);
    float3 grille = float3(slot < 0.5 ? 1.0 : 0.18,
                           (slot >= 0.5 && slot < 1.5) ? 1.0 : 0.18,
                           slot >= 1.5 ? 1.0 : 0.18);
    rgb *= lerp(float3(1.0, 1.0, 1.0), grille, maskAmount);

    rgb = saturate((rgb - 0.5) * contrast + 0.5);

    return lerp(centre, float4(rgb, centre.a), amount);
}
