// RGB Split — chromatic separation of the red and blue channels.
//
// Parameters:
//   0  amount  0..1     displacement, up to 6% of frame width
//   1  angle   0..360   direction of the split, degrees
//   2  radial  0/1      displace outward from centre instead of a fixed angle

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const float amount = c.params[0].x;
    const float angle  = c.params[0].y;
    const float radial = c.params[0].z;

    const float  a         = toRadians(angle);
    const float2 direction = float2(cos(a), sin(a));

    // Isotropic displacement: scale Y so a 45 degree split is not skewed.
    const float2 linearOffset = direction * amount * 0.06 * aspectScale(c);

    // Radial mode grows the displacement toward the edges of the frame.
    const float2 radialOffset = (in.uv - 0.5) * amount * 0.12;

    const float2 offset = mix(linearOffset, radialOffset, radial);

    const float  r      = source.sample(samp, in.uv + offset).r;
    const float4 centre = source.sample(samp, in.uv);
    const float  b      = source.sample(samp, in.uv - offset).b;

    return float4(r, centre.g, b, centre.a);
}
