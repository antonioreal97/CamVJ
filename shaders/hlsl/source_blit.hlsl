// Source blit — brings a captured frame to project resolution.
//
// A camera rarely matches the project format: 1280x720 or 1920x1080 at some
// other aspect. Scaling on the GPU here is what keeps the CPU side of capture
// down to a memcpy.
//
// Parameters:
//   0  fit     0..2   0 fit (letterbox), 1 fill (crop), 2 stretch
//   1  mirror  0/1    flip horizontally, for a self-view camera
//   2  srcW    internal, the captured frame's width in pixels
//   3  srcH    internal, the captured frame's height in pixels
//   4  flipY   internal, set for capture stacks that deliver bottom-up frames

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    int   mode   = (int)(uParams[0].x + 0.5);
    float mirror = uParams[0].y;
    float sourceWidth  = max(uParams[0].z, 1.0);
    float sourceHeight = max(uParams[0].w, 1.0);
    float flipY        = uParams[1].x;

    float2 uv = input.uv;
    if (mirror > 0.5)
    {
        uv.x = 1.0 - uv.x;
    }
    if (flipY > 0.5)
    {
        uv.y = 1.0 - uv.y;
    }

    float targetAspect = uResolution.x / uResolution.y;
    float sourceAspect = sourceWidth / sourceHeight;

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

    float2 sourceUv = (uv - 0.5) * scale + 0.5;

    // Letterbox bars, rather than the clamped edge pixel smeared outward.
    if (mode == 0 &&
        (sourceUv.x < 0.0 || sourceUv.x > 1.0 || sourceUv.y < 0.0 || sourceUv.y > 1.0))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float3 colour = gSource.Sample(gSampler, saturate(sourceUv)).rgb;
    return float4(colour, 1.0);
}
