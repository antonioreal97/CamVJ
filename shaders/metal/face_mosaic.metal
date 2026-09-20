// Face Mosaic, background pass — the picture the tiles land on.
// See shaders/hlsl/face_mosaic.hlsl; the two must produce the same image.
//
// Parameters (packed by FaceMosaicEffect):
//   0  background  0..1  camera level under the tiles; 0 is black

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float  level  = saturate(c.params[0].x);
    const float4 colour = source.sample(samp, in.uv);
    return float4(colour.rgb * level, 1.0);
}
