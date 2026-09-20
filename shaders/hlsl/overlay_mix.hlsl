// Crossfade two premultiplied PNG variants before normal alpha compositing.
// Both art textures are mapped onto the fixed 1920x1080 canvas; outside the
// portrait strip the result is transparent, never a stretched border pixel.

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float2 uv = input.uv;
    if (uParams[0].y > 0.5)
    {
        const float width = 81.0 / 256.0;
        const float left = (1.0 - width) * 0.5;
        if (uv.x < left || uv.x > left + width) return float4(0.0, 0.0, 0.0, 0.0);
        uv.x = (uv.x - left) / width;
    }
    return lerp(gHistory.Sample(gSampler, uv), gSource.Sample(gSampler, uv),
                saturate(uParams[0].x));
}
