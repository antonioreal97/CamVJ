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

constant float kTwoPi = 6.28318530718;

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float frequency  = max(c.params[0].x, 4.0);
    const float modulation = c.params[0].y;
    const float threshold  = saturate(c.params[0].z);
    const float lineWidth  = saturate(c.params[0].w);
    const float direction  = c.params[1].x;
    const float speed      = c.params[1].y;
    const float contrast   = max(c.params[1].z, 0.25);
    const float amount     = saturate(c.params[1].w);

    const float4 original = source.sample(samp, in.uv);
    const float  luma     = dot(original.rgb, float3(0.299, 0.587, 0.114));
    const float  lumaC    = saturate((luma - 0.5) * contrast + 0.5);

    const float  a   = toRadians(direction);
    const float2 dir = float2(cos(a), sin(a));
    const float2 p   = (in.uv - 0.5) * aspectScale(c);
    const float  axis = dot(p, dir);

    const float phase = axis * frequency * kTwoPi + lumaC * modulation * kTwoPi + c.time * speed;
    const float s     = sin(phase) * 0.5 + 0.5;
    const float width = max(lineWidth, 0.004);
    float line = smoothstep(threshold - width, threshold + width, s);
    line *= smoothstep(0.02, 0.10, luma);

    const float3 raster = original.rgb * line;
    return mix(original, float4(raster, original.a), amount);
}
