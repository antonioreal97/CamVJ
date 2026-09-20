// Face Mosaic, background pass — the picture the tiles land on.
// See shaders/metal/face_mosaic.metal; the two must produce the same image.
//
// Parameters (packed by FaceMosaicEffect):
//   0  background  0..1  camera level under the tiles; 0 is black

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    const float  level  = saturate(uParams[0].x);
    const float4 colour = gSource.Sample(gSampler, input.uv);
    return float4(colour.rgb * level, 1.0);
}
