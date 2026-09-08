// Passthrough — an exact copy of the source.
// Useful as a reference pass and as a sanity check that the chain is wired up.
//
// Parameters: none.

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    return gSource.Sample(gSampler, input.uv);
}
