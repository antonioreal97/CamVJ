// Auto Frame — samples the frame through the rectangle the framing controller
// chose. See shaders/hlsl/auto_frame.hlsl; the two must produce the same image.
//
// Parameters (packed by AutoFrameEffect, not the operator's controls):
//   0  centerX     0..1   crop centre in the source
//   1  centerY     0..1
//   2  halfWidth   0..0.5 source half extents
//   3  halfHeight  0..0.5
//   4  dstHalfW    0..0.5 output window in the destination (0.5 = full frame)
//   5  dstHalfH    0..0.5 9:16 letterbox is ~0.158 x 0.5, centred

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float2 srcCenter = float2(c.params[0].x, c.params[0].y);
    const float2 srcHalf   = float2(c.params[0].z, c.params[0].w);
    const float2 dstHalf   = max(float2(c.params[1].x, c.params[1].y), float2(1.0e-4, 1.0e-4));
    const float2 dstCenter = float2(0.5, 0.5);

    const float2 inWindow = (in.uv - dstCenter) / (dstHalf * 2.0) + 0.5;

    if (inWindow.x < 0.0 || inWindow.x > 1.0 || inWindow.y < 0.0 || inWindow.y > 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float2 srcUv = srcCenter + (inWindow - 0.5) * srcHalf * 2.0;

    return source.sample(samp, saturate(srcUv));
}
