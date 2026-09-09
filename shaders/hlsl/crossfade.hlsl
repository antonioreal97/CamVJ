// Crossfade — the processed picture dissolved back into the one that entered.
//
// This is how PROGRAM leaves and rejoins the effect chain. Clean is not a
// switch that removes effects, it is this pass driven to zero: the operator
// sees the look dissolve out of the picture instead of a cut on air.
//
// gSource is the wet image, gHistory the dry one it replaces. Both are full
// frames at processing resolution, so the mix is per-pixel and needs no
// knowledge of what produced either side.
//
// Parameters:
//   0.x  amount  0 dry, 1 wet

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    const float amount = saturate(uParams[0].x);
    return lerp(gHistory.Sample(gSampler, input.uv),
                gSource.Sample(gSampler, input.uv),
                amount);
}
