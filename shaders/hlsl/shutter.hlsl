// Shutter — temporal smear of motion, like a long exposure.
//
// The previous output lives in gHistory. Decay keeps that picture; a luma
// delta gate so a still frame does not trail sensor noise. After the mix the
// C++ side copies the destination back into the persistent history.
//
// Parameters:
//   0  decay      0..0.95  how much of the last frame remains
//   1  threshold  0..0.5   motion below this stays sharp
//   2  mix        0..1     original ↔ shutter

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float decay     = saturate(uParams[0].x);
    float threshold = saturate(uParams[0].y);
    float amount    = saturate(uParams[0].z);

    float4 current = gSource.Sample(gSampler, input.uv);
    float4 prev    = gHistory.Sample(gSampler, input.uv);

    float motion = length(current.rgb - prev.rgb);
    float gate   = smoothstep(threshold, threshold + 0.10, motion);
    float keep   = min(decay, 0.95) * gate;

    float3 smeared = lerp(current.rgb, prev.rgb, keep);
    return lerp(current, float4(smeared, current.a), amount);
}
