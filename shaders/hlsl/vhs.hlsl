// VHS — tape transport artefacts: wobble, chroma bleed, dropouts, tracking.
//
// A tape look, not a display look. It models what the recorder and the head
// did to the signal and leaves the picture tube to `crt`; chain vhs → crt for
// the full old-television. Six samples: the sharp tap, three trailing chroma
// taps, the ghost echo and the untouched original for the mix. No blur pass,
// so it stays in the same cost class as `crt`.
//
// Parameters:
//   0  wobble        0..1   line drift and tape wave
//   1  chroma_bleed  0..1   colour smeared to the right of its luma
//   2  noise         0..1   snow
//   3  tracking      0..1   rolling misalignment band and head-switch tear
//   4  dropouts      0..1   bright dashes where the tape lost head contact
//   5  ghost         0..1   delayed echo
//   6  interlace     0..1   alternate line darkening
//   7  wear          0..1   lifted blacks, washed colour, tape tint
//   8  speed         0..4   rate of every time-varying artefact (0 freezes)
//   9  mix           0..1   original ↔ effect

#include "common.hlsli"

static const float3 kLuma = float3(0.299, 0.587, 0.114);

float hash21(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float lineNoise(float y, float seed)
{
    float i = floor(y);
    float w = frac(y) * frac(y) * (3.0 - 2.0 * frac(y));
    return lerp(hash21(float2(i, seed)), hash21(float2(i + 1.0, seed)), w);
}

float4 main(VSOutput input) : SV_Target
{
    float wobble     = saturate(uParams[0].x);
    float bleed      = saturate(uParams[0].y);
    float snowAmount = saturate(uParams[0].z);
    float tracking   = saturate(uParams[0].w);
    float dropouts   = saturate(uParams[1].x);
    float ghost      = saturate(uParams[1].y);
    float interlace  = saturate(uParams[1].z);
    float wear       = saturate(uParams[1].w);
    float speed      = max(uParams[2].x, 0.0);
    float amount     = saturate(uParams[2].y);

    // Sines take the running clock; hashes take a wrapped one, because frac()
    // of a large float quantises and the grain would freeze after an hour on
    // stage. 500 is a whole number of roll periods, so the wrap does not show.
    float t     = uTime * speed;
    float tw    = fmod(t, 500.0);
    float frame = floor(tw * 25.0);   // tape frame: jitter and dropouts hold
    float field = floor(tw * 50.0);   // field rate: snow updates

    // Servo error is low frequency: neighbouring lines drift together. A
    // per-line hash on its own reads as edge noise rather than as tape, so
    // the hard per-line tear is kept for the band and the head switch below.
    float lineIndex = floor(input.uv.y * uResolution.y);
    float drift     = lineNoise(lineIndex * 0.2, frame) * 2.0 - 1.0;
    float tear      = hash21(float2(lineIndex, frame)) * 2.0 - 1.0;
    float wave      = sin(input.uv.y *  9.0 + t * 1.7) * 0.6
                    + sin(input.uv.y * 53.0 - t * 3.1) * 0.4;
    float shift = (drift * 0.30 + wave * 0.70) * wobble * 0.012;

    // A mistracked tape puts a band of misalignment that rolls up the frame.
    float roll = frac(-tw * 0.08);
    float band = 1.0 - smoothstep(0.0, 0.09, abs(frac(input.uv.y - roll + 0.5) - 0.5));
    shift += band * tracking * (tear * 0.05 + 0.012);

    // Head switching: the bottom few lines of every VHS frame tear.
    float head = 1.0 - smoothstep(0.0, 0.035, 1.0 - input.uv.y);
    shift += head * tracking * tear * 0.05;

    float2 uv = float2(saturate(input.uv.x + shift), input.uv.y);

    float4 original = gSource.Sample(gSampler, input.uv);
    float4 base     = gSource.Sample(gSampler, uv);

    // Chroma bandwidth: keep the luma of the sharp tap and take the colour
    // from samples behind it. The one-sided weighting is why VHS colour lags
    // to the right of an edge instead of blurring evenly around it.
    float  reach = bleed * 0.018;
    float3 tap1  = gSource.Sample(gSampler, float2(saturate(uv.x - reach * 0.33), uv.y)).rgb;
    float3 tap2  = gSource.Sample(gSampler, float2(saturate(uv.x - reach * 0.66), uv.y)).rgb;
    float3 tap3  = gSource.Sample(gSampler, float2(saturate(uv.x - reach), uv.y)).rgb;
    float3 smear = base.rgb * 0.4 + tap1 * 0.3 + tap2 * 0.2 + tap3 * 0.1;
    float  relum = dot(base.rgb, kLuma) - dot(smear, kLuma);
    float3 rgb = lerp(base.rgb, smear + float3(relum, relum, relum), bleed);

    // Ghost: the reflection an unterminated cable returns a fixed delay later.
    float3 echo = gSource.Sample(gSampler, float2(saturate(uv.x - 0.03), uv.y)).rgb;
    rgb += echo * ghost * 0.22;

    float grain = hash21(float2(input.uv.x * uResolution.x + field,
                                input.uv.y * uResolution.y - field)) - 0.5;
    rgb += grain * (snowAmount * 0.30 + band * tracking * 0.45);

    // Dropouts: a few lines per frame lose contact, in short bright runs.
    float dropLine = hash21(float2(lineIndex, frame + 91.0));
    float dropRun  = hash21(float2(floor(uv.x * 26.0), lineIndex + frame * 3.0));
    float drop     = step(1.0 - dropouts * 0.10, dropLine) * step(0.82, dropRun);
    rgb = lerp(rgb, float3(0.90, 0.90, 0.90), drop);

    float even = frac(input.uv.y * uResolution.y * 0.5) < 0.5 ? 1.0 : 0.70;
    rgb *= lerp(1.0, even, interlace);

    // Tape wear: lifted blacks, lost highlights, colour drifting to magenta.
    float grey = dot(rgb, kLuma);
    float3 worn = lerp(float3(grey, grey, grey), rgb, 0.80) * 0.86 + 0.055;
    worn *= float3(1.03, 0.98, 1.02);
    rgb = lerp(rgb, worn, wear);

    return lerp(original, float4(saturate(rgb), base.a), amount);
}
