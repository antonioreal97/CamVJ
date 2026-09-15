// Normal-alpha overlay laid over the picture below it.
//
// gSource is a premultiplied-alpha PNG upload. gHistory is the already processed
// video plus every lower overlay. Portrait art occupies the centred 9:16
// output window inside CamVJ's fixed 16:9 processing canvas.
//
// Parameters:
//   0.x  layer opacity, including its enable/disable and Clean fades
//   0.y  0 landscape, 1 portrait

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    const float4 under = gHistory.Sample(gSampler, input.uv);
    float2 overlayUv = input.uv;

    if (uParams[0].y > 0.5)
    {
        // (9/16) / (16/9) = 81/256 of the fixed landscape canvas.
        const float windowWidth = 81.0 / 256.0;
        const float windowLeft  = (1.0 - windowWidth) * 0.5;
        if (input.uv.x < windowLeft || input.uv.x > windowLeft + windowWidth)
        {
            return under;
        }
        overlayUv.x = (input.uv.x - windowLeft) / windowWidth;
    }

    const float4 over = gSource.Sample(gSampler, overlayUv);
    const float opacity = saturate(uParams[0].x);
    const float alpha = saturate(over.a * opacity);
    return float4(over.rgb * opacity + under.rgb * (1.0 - alpha),
                  alpha + under.a * (1.0 - alpha));
}
