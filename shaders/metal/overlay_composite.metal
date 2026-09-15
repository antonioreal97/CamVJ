// Normal-alpha overlay laid over the picture below it.
//
// Texture 0 is a premultiplied-alpha PNG upload. Texture 1 is the already
// processed video plus every lower overlay. Portrait art occupies the centred
// 9:16 output window inside CamVJ's fixed 16:9 processing canvas.
//
// Parameters:
//   0.x  layer opacity, including its enable/disable and Clean fades
//   0.y  0 landscape, 1 portrait

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> overlay [[texture(0)]],
                              texture2d<float> underTexture [[texture(1)]],
                              sampler samp [[sampler(0)]])
{
    const float4 under = underTexture.sample(samp, in.uv);
    float2 overlayUv = in.uv;

    if (c.params[0].y > 0.5)
    {
        // (9/16) / (16/9) = 81/256 of the fixed landscape canvas.
        const float windowWidth = 81.0 / 256.0;
        const float windowLeft  = (1.0 - windowWidth) * 0.5;
        if (in.uv.x < windowLeft || in.uv.x > windowLeft + windowWidth)
        {
            return under;
        }
        overlayUv.x = (in.uv.x - windowLeft) / windowWidth;
    }

    const float4 over = overlay.sample(samp, overlayUv);
    const float opacity = saturate(c.params[0].x);
    const float alpha = saturate(over.a * opacity);
    return float4(over.rgb * opacity + under.rgb * (1.0 - alpha),
                  alpha + under.a * (1.0 - alpha));
}
