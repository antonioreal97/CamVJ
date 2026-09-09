// Crossfade — the processed picture dissolved back into the one that entered.
//
// This is how PROGRAM leaves and rejoins the effect chain. Clean is not a
// switch that removes effects, it is this pass driven to zero: the operator
// sees the look dissolve out of the picture instead of a cut on air.
//
// Texture 0 is the wet image, texture 1 the dry one it replaces. Both are
// full frames at processing resolution, so the mix is per-pixel and needs no
// knowledge of what produced either side.
//
// Parameters:
//   0.x  amount  0 dry, 1 wet

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> wet [[texture(0)]],
                              texture2d<float> dry [[texture(1)]],
                              sampler samp [[sampler(0)]])
{
    const float amount = saturate(c.params[0].x);
    return mix(dry.sample(samp, in.uv), wet.sample(samp, in.uv), amount);
}
