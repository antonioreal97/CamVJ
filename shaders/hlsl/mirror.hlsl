// Mirror — reflects one half of the frame onto the other.
//
// Parameters:
//   0  mode   0..4   0 left>right, 1 right>left, 2 top>bottom,
//                    3 bottom>top, 4 quad
//   1  pivot  0..1   position of the mirror axis

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    int   mode  = (int)(uParams[0].x + 0.5);
    float pivot = saturate(uParams[0].y);

    float2 uv = input.uv;

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

    return gSource.Sample(gSampler, saturate(uv));
}
