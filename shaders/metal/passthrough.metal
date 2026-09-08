// Passthrough — an exact copy of the source.
// Parameters: none.

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    return source.sample(samp, in.uv);
}
