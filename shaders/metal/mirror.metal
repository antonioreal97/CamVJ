// Mirror — reflects one half of the frame onto the other.
//
// Parameters:
//   0  mode   0..4   0 left>right, 1 right>left, 2 top>bottom,
//                    3 bottom>top, 4 quad
//   1  pivot  0..1   position of the mirror axis

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]],
                              texture2d<float> source [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    const int   mode  = int(c.params[0].x + 0.5);
    const float pivot = saturate(c.params[0].y);

    float2 uv = in.uv;

    if (mode == 0)
    {
        uv.x = (uv.x > pivot) ? (2.0 * pivot - uv.x) : uv.x;
    }
    else if (mode == 1)
    {
        uv.x = (uv.x < pivot) ? (2.0 * pivot - uv.x) : uv.x;
    }
    else if (mode == 2)
    {
        uv.y = (uv.y > pivot) ? (2.0 * pivot - uv.y) : uv.y;
    }
    else if (mode == 3)
    {
        uv.y = (uv.y < pivot) ? (2.0 * pivot - uv.y) : uv.y;
    }
    else
    {
        uv.x = (uv.x > 0.5) ? (1.0 - uv.x) : uv.x;
        uv.y = (uv.y > 0.5) ? (1.0 - uv.y) : uv.y;
    }

    return source.sample(samp, saturate(uv));
}
