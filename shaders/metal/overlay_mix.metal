// Crossfade two premultiplied PNG variants before normal alpha compositing.
// Both art textures are mapped onto the fixed 1920x1080 canvas; outside the
// portrait strip the result is transparent, never a stretched border pixel.

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> newer [[texture(0)]],
                              texture2d<float> older [[texture(1)]],
                              sampler samp [[sampler(0)]])
{
    float2 uv = in.uv;
    if (c.params[0].y > 0.5)
    {
        const float width = 81.0 / 256.0;
        const float left = (1.0 - width) * 0.5;
        if (uv.x < left || uv.x > left + width) return float4(0.0);
        uv.x = (uv.x - left) / width;
    }
    return mix(older.sample(samp, uv), newer.sample(samp, uv),
               saturate(c.params[0].x));
}
