// Frame Delay — the picture from a few frames ago, several times over.
//
// A true multi-tap delay would cost one persistent texture and one pass per
// copy, and the pass primitive samples two textures. So the copies live in a
// single trail instead: every `spacing` frames the live picture is stamped
// into it and whatever was already there fades by `decay`. The trail then
// holds one step back at full strength, two steps back at decay, three at
// decay squared — the sum a multi-tap delay would produce, at two passes.
//
// Lighten and Screen keep the brighter picture, which is the look this effect
// exists for: a lit body echoing across a dark stage. Darken is the same
// arithmetic inverted, for a dark subject on a bright wall.
//
// The C++ side packs these; the generic parameter packer is overridden.
//   0.x  blend  0 lighten, 1 screen, 2 darken
//   0.y  key    only pixels past this luminance leave a copy
//   0.z  mix    live picture ↔ echoed picture
//   0.w  decay  per-copy fade, derived from the copy count
//   1.x  stage  0 composite, 1 accumulate, 2 seed

constant float3 kLuma = float3(0.299, 0.587, 0.114);

// What is allowed to leave a copy. Everything else becomes the neutral the
// blend cannot see — black for Lighten and Screen, white for Darken — so the
// background does not smear its own sensor noise across the frame.
static inline float3 frameDelayKeyed(float3 colour, float key, bool dark)
{
    const float luma = dot(saturate(colour), kLuma);

    if (dark)
    {
        const float edge = saturate(1.0 - key);
        const float gate = 1.0 - smoothstep(max(edge - 0.10, 0.0), edge, luma);
        return mix(float3(1.0), colour, gate);
    }

    const float gate = smoothstep(key, key + 0.10, luma);
    return colour * gate;
}

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              texture2d<float> trail [[texture(1)]],
                              sampler samp [[sampler(0)]])
{
    const float mode   = c.params[0].x;
    const float key    = saturate(c.params[0].y);
    const float amount = saturate(c.params[0].z);
    const float decay  = saturate(c.params[0].w);
    const float stage  = c.params[1].x;

    const bool dark   = mode > 1.5;
    const bool screen = mode > 0.5 && mode < 1.5;

    const float4 live = source.sample(samp, in.uv);

    // Seed: the trail starts as the live frame, so the first copy coincides
    // with the picture instead of arriving as a flash of whatever the texture
    // held before.
    if (stage > 1.5)
    {
        return float4(frameDelayKeyed(live.rgb, key, dark), 1.0);
    }

    const float3 ghost = trail.sample(samp, in.uv).rgb;

    if (stage > 0.5)
    {
        const float3 aged   = dark ? (1.0 - (1.0 - ghost) * decay) : ghost * decay;
        const float3 keyed  = frameDelayKeyed(live.rgb, key, dark);
        return float4(dark ? min(aged, keyed) : max(aged, keyed), 1.0);
    }

    float3 echoed;
    if (dark)
    {
        echoed = min(live.rgb, ghost);
    }
    else if (screen)
    {
        echoed = 1.0 - (1.0 - saturate(live.rgb)) * (1.0 - saturate(ghost));
    }
    else
    {
        echoed = max(live.rgb, ghost);
    }

    return float4(mix(live.rgb, echoed, amount), live.a);
}
