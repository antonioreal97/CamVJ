// Pixelate — quantises the frame into square blocks.
//
// Parameters:
//   0  size  1..256   block size in pixels at processing resolution
//   1  mix   0..1     blend between the original and the pixelated result

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float size   = max(c.params[0].x, 1.0);
    const float amount = saturate(c.params[0].y);

    const float2 blocks = c.resolution / size;

    // Sample the centre of each block: sampling the corner makes the result
    // shimmer under motion.
    const float2 blockUv = (floor(in.uv * blocks) + 0.5) / blocks;

    const float4 pixelated = source.sample(samp, blockUv);
    const float4 original  = source.sample(samp, in.uv);

    return mix(original, pixelated, amount);
}
