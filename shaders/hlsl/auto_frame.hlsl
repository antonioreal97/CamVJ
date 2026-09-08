// Auto Frame — samples the frame through the rectangle the framing controller
// chose, which is what turns a tracked subject into a moving camera.
//
// The whole effect is one crop. Everything that decides where the crop goes —
// tracking, dead zone, smoothing, speed limits — happens on the CPU in
// src/tracking/framing.cpp, because it is scalar arithmetic with memory, not
// image processing. The shader is deliberately this dumb.
//
// Parameters (packed by AutoFrameEffect, not the operator's controls):
//   0  centerX     0..1   crop centre in the source
//   1  centerY     0..1
//   2  halfWidth   0..0.5 source half extents
//   3  halfHeight  0..0.5
//   4  dstHalfW    0..0.5 output window in the destination (0.5 = full frame)
//   5  dstHalfH    0..0.5 9:16 letterbox is ~0.158 x 0.5, centred

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float2 srcCenter = float2(uParams[0].x, uParams[0].y);
    float2 srcHalf   = float2(uParams[0].z, uParams[0].w);
    float2 dstHalf   = max(float2(uParams[1].x, uParams[1].y), float2(1.0e-4, 1.0e-4));
    float2 dstCenter = float2(0.5, 0.5);

    float2 inWindow = (input.uv - dstCenter) / (dstHalf * 2.0) + 0.5;

    if (inWindow.x < 0.0 || inWindow.x > 1.0 || inWindow.y < 0.0 || inWindow.y > 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 srcUv = srcCenter + (inWindow - 0.5) * srcHalf * 2.0;

    return gSource.Sample(gSampler, saturate(srcUv));
}
