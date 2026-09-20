// Sprite vertex shader, shared by every sprite shader. The HLSL half of
// sprite_vertex in shaders/metal/common.metal; the two must place the same
// quads. DrawInstanced(6, count) with topology TRIANGLELIST, no vertex buffer.

#include "common.hlsli"

// src/gpu/Rhi.h SpriteInstance, three float4 rows per instance:
//   row 0  destination centre xy, half extent zw (canvas UV)
//   row 1  source centre xy, half extent zw (source UV)
//   row 2  x rotation (rad), y opacity, zw free
cbuffer SpriteCB : register(b1)
{
    float4 uSprites[64 * 3];  // kMaxSpriteInstances
};

static const float2 kCorners[6] = {
    float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0),
    float2(-1.0, 1.0),  float2(1.0, -1.0), float2(1.0, 1.0),
};

SpriteVSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const float4 destination = uSprites[instanceId * 3 + 0];
    const float4 source      = uSprites[instanceId * 3 + 1];
    const float4 style       = uSprites[instanceId * 3 + 2];

    const float2 corner = kCorners[vertexId % 6];

    // Rotation in pixels so a turned tile keeps its shape on a 16:9 canvas.
    const float  angle  = style.x;
    const float2 offset = corner * destination.zw * uResolution;
    const float2 turned = float2(offset.x * cos(angle) - offset.y * sin(angle),
                                 offset.x * sin(angle) + offset.y * cos(angle));
    const float2 canvas = destination.xy + turned * uInvResolution;

    SpriteVSOutput output;
    output.position = float4(canvas * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv       = source.xy + corner * source.zw;
    output.local    = corner;
    output.opacity  = style.y;
    return output;
}
