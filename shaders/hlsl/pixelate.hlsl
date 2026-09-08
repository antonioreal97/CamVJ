// Pixelate — quantises the frame into square blocks.
//
// Parameters:
//   0  size  1..256   block size in pixels at processing resolution
//   1  mix   0..1     blend between the original and the pixelated result

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float size = max(uParams[0].x, 1.0);
    float mix  = saturate(uParams[0].y);

    float2 blocks = uResolution / size;

    // Sample the centre of each block: sampling the corner makes the result
    // shimmer under motion.
    float2 blockUv = (floor(input.uv * blocks) + 0.5) / blocks;

    float4 pixelated = gSource.Sample(gSampler, blockUv);
    float4 original  = gSource.Sample(gSampler, input.uv);

    return lerp(original, pixelated, mix);
}
