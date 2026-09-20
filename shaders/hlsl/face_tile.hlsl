// Face tile — one face crop drawn as a sprite (SpritePass).
// See shaders/metal/face_tile.metal; the two must produce the same image.
//
// Parameters (packed by FaceMosaicEffect):
//   0  feather    0..1  soft edge width, fraction of the tile's half extent
//   1  roundness  0..1  corner radius, fraction of the tile's half extent
//
// Output is premultiplied: the sprite blend state is (ONE, INV_SRC_ALPHA).
// The entry point is sprite_fragment, which is what marks this a sprite
// shader for the D3D11 library.

#include "common.hlsli"

// Signed distance to a rounded box in the tile's -1..1 space; negative inside.
float roundedBox(float2 p, float radius)
{
    const float2 q = abs(p) - (1.0 - radius);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

float4 sprite_fragment(SpriteVSOutput input) : SV_Target
{
    const float feather = max(saturate(uParams[0].x), 0.002);
    const float radius  = saturate(uParams[0].y);

    const float distance = roundedBox(input.local, radius);
    const float coverage = 1.0 - smoothstep(-feather, 0.0, distance);

    const float  alpha  = coverage * saturate(input.opacity);
    const float3 colour = gSource.Sample(gSampler, saturate(input.uv)).rgb;
    return float4(colour * alpha, alpha);
}
