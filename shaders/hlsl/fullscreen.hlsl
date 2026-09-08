// Fullscreen triangle vertex shader, shared by every pass.
// Generated from SV_VertexID: no vertex buffer, no index buffer, no input
// layout. Draw(3, 0) with topology TRIANGLELIST.

#include "common.hlsli"

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);

    output.uv       = uv;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

    return output;
}
