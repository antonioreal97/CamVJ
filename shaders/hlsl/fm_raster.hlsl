// FM Raster — scanlines frequency-modulated by source luminance.
//
// The photograph becomes a carrier: luma shifts the phase of a sine, then a
// threshold turns that into hard (or soft) lines. Dark regions emit nothing,
// so a face stays readable as electronic topography. Cheap: one sample.
//
// Parameters:
//   0  frequency    4..160   cycles across the frame
//   1  modulation   0..8     how hard luma warps the lines
//   2  threshold    0..1     duty cycle of the pulse
//   3  line_width   0..1     softness / thickness around the threshold
//   4  direction    0..360   line angle, degrees (90 = horizontal)
//   5  speed        0..8     phase scroll
//   6  contrast     0.25..4  luma contrast before modulation
//   7  mix          0..1     original ↔ effect

#include "common.hlsli"

static const float kTwoPi = 6.28318530718;

float4 main(VSOutput input) : SV_Target
{
    float frequency  = max(uParams[0].x, 4.0);
    float modulation = uParams[0].y;
    float threshold  = saturate(uParams[0].z);
    float lineWidth  = saturate(uParams[0].w);
    float direction  = uParams[1].x;
    float speed      = uParams[1].y;
    float contrast   = max(uParams[1].z, 0.25);
    float amount     = saturate(uParams[1].w);

    float4 original = gSource.Sample(gSampler, input.uv);
    float  luma     = dot(original.rgb, float3(0.299, 0.587, 0.114));
    float  lumaC    = saturate((luma - 0.5) * contrast + 0.5);

    float  a   = radians(direction);
    float2 dir = float2(cos(a), sin(a));
    float2 p   = (input.uv - 0.5) * aspectScale();
    float  axis = dot(p, dir);

    float phase = axis * frequency * kTwoPi + lumaC * modulation * kTwoPi + uTime * speed;
    float s     = sin(phase) * 0.5 + 0.5;
    float width = max(lineWidth, 0.004);
    float line  = smoothstep(threshold - width, threshold + width, s);
    line *= smoothstep(0.02, 0.10, luma);

    float3 raster = original.rgb * line;
    return lerp(original, float4(raster, original.a), amount);
}
