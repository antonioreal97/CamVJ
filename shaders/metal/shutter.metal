// Shutter — temporal smear of motion, like a long exposure.
//
// The previous output lives in history (texture 1). Decay keeps that picture;
// a luma delta gate so a still frame does not trail sensor noise. After the
// mix the C++ side copies the destination back into the persistent history.
//
// Parameters:
//   0  decay      0..0.95  how much of the last frame remains
//   1  threshold  0..0.5   motion below this stays sharp
//   2  mix        0..1     original ↔ shutter

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              texture2d<float> history [[texture(1)]],
                              sampler samp [[sampler(0)]])
{
    const float decay     = saturate(c.params[0].x);
    const float threshold = saturate(c.params[0].y);
    const float amount    = saturate(c.params[0].z);

    const float4 current = source.sample(samp, in.uv);
    const float4 prev    = history.sample(samp, in.uv);

    const float motion = length(current.rgb - prev.rgb);
    const float gate   = smoothstep(threshold, threshold + 0.10, motion);
    const float keep   = min(decay, 0.95) * gate;

    const float3 smeared = mix(current.rgb, prev.rgb, keep);
    return mix(current, float4(smeared, current.a), amount);
}
