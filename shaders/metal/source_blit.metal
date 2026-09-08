// Source blit — brings a captured frame to project resolution.
// See shaders/hlsl/source_blit.hlsl; the two must produce the same image.
//
// Parameters:
//   0  fit     0..2   0 fit (letterbox), 1 fill (crop), 2 stretch
//   1  mirror  0/1    flip horizontally, for a self-view camera
//   2  srcW    internal, the captured frame's width in pixels
//   3  srcH    internal, the captured frame's height in pixels
//   4  flipY   internal, set for capture stacks that deliver bottom-up frames

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const int   mode         = int(c.params[0].x + 0.5);
    const float mirror       = c.params[0].y;
    const float sourceWidth  = max(c.params[0].z, 1.0);
    const float sourceHeight = max(c.params[0].w, 1.0);
    const float flipY        = c.params[1].x;

    float2 uv = in.uv;
    if (mirror > 0.5)
    {
        uv.x = 1.0 - uv.x;
    }
    if (flipY > 0.5)
    {
        uv.y = 1.0 - uv.y;
    }

    const float targetAspect = c.resolution.x / c.resolution.y;
    const float sourceAspect = sourceWidth / sourceHeight;

    // Scale applied to centred coordinates. Greater than one shrinks the image
    // inside the frame, which is what produces the black bars in fit mode.
    float2 scale = float2(1.0, 1.0);
    if (mode == 0)
    {
        if (sourceAspect > targetAspect) scale.y = sourceAspect / targetAspect;
        else                             scale.x = targetAspect / sourceAspect;
    }
    else if (mode == 1)
    {
        if (sourceAspect > targetAspect) scale.x = targetAspect / sourceAspect;
        else                             scale.y = sourceAspect / targetAspect;
    }

    const float2 sourceUv = (uv - 0.5) * scale + 0.5;

    // Letterbox bars, rather than the clamped edge pixel smeared outward.
    if (mode == 0 &&
        (sourceUv.x < 0.0 || sourceUv.x > 1.0 || sourceUv.y < 0.0 || sourceUv.y > 1.0))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float3 colour = source.sample(samp, saturate(sourceUv)).rgb;
    return float4(colour, 1.0);
}
