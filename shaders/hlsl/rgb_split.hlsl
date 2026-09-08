// RGB Split — chromatic separation of the red and blue channels.
//
// Parameters:
//   0  amount  0..1     displacement, up to 6% of frame width
//   1  angle   0..360   direction of the split, degrees
//   2  radial  0/1      displace outward from centre instead of a fixed angle

#include "common.hlsli"

float4 main(VSOutput input) : SV_Target
{
    float amount = uParams[0].x;
    float angle  = uParams[0].y;
    float radial = uParams[0].z;

    float  radians_ = radians(angle);
    float2 direction = float2(cos(radians_), sin(radians_));

    // Isotropic displacement: scale Y so a 45 degree split is not skewed.
    float2 linearOffset = direction * amount * 0.06 * aspectScale();

    // Radial mode grows the displacement toward the edges of the frame.
    float2 radialOffset = (input.uv - 0.5) * amount * 0.12;

    float2 offset = lerp(linearOffset, radialOffset, radial);

    float r = gSource.Sample(gSampler, input.uv + offset).r;
    float4 centre = gSource.Sample(gSampler, input.uv);
    float b = gSource.Sample(gSampler, input.uv - offset).b;

    return float4(r, centre.g, b, centre.a);
}
