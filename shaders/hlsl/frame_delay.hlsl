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

#include "common.hlsli"

static const float3 kLuma = float3(0.299, 0.587, 0.114);

// What is allowed to leave a copy. Everything else becomes the neutral the
// blend cannot see — black for Lighten and Screen, white for Darken — so the
// background does not smear its own sensor noise across the frame.
float3 frameDelayKeyed(float3 colour, float key, bool dark)
{
    float luma = dot(saturate(colour), kLuma);

    if (dark)
    {
        float edge = saturate(1.0 - key);
        float gate = 1.0 - smoothstep(max(edge - 0.10, 0.0), edge, luma);
        return lerp(float3(1.0, 1.0, 1.0), colour, gate);
    }

    float gate = smoothstep(key, key + 0.10, luma);
    return colour * gate;
}

float4 main(VSOutput input) : SV_Target
{
    float mode   = uParams[0].x;
    float key    = saturate(uParams[0].y);
    float amount = saturate(uParams[0].z);
    float decay  = saturate(uParams[0].w);
    float stage  = uParams[1].x;

    bool dark   = mode > 1.5;
    bool screen = mode > 0.5 && mode < 1.5;

    float4 live = gSource.Sample(gSampler, input.uv);

    // Seed: the trail starts as the live frame, so the first copy coincides
    // with the picture instead of arriving as a flash of whatever the texture
    // held before.
    if (stage > 1.5)
    {
        return float4(frameDelayKeyed(live.rgb, key, dark), 1.0);
    }

    float3 ghost = gHistory.Sample(gSampler, input.uv).rgb;

    if (stage > 0.5)
    {
        float3 aged  = dark ? (1.0 - (1.0 - ghost) * decay) : ghost * decay;
        float3 keyed = frameDelayKeyed(live.rgb, key, dark);
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

    return float4(lerp(live.rgb, echoed, amount), live.a);
}
