// Test pattern generator — stands in for DeckLink capture until M1.
//
// Declares no source texture: it is a generator, and an unbound texture
// argument would fail Metal validation.
//
// Parameters:
//   0  pattern  0..2   0 bars, 1 plasma, 2 grid
//   1  speed    0..4   animation rate multiplier
//   2  markers  0/1    draw the motion markers

constant float3 kBars[8] =
{
    float3(1.00, 1.00, 1.00),  // white
    float3(1.00, 1.00, 0.00),  // yellow
    float3(0.00, 1.00, 1.00),  // cyan
    float3(0.00, 1.00, 0.00),  // green
    float3(1.00, 0.00, 1.00),  // magenta
    float3(1.00, 0.00, 0.00),  // red
    float3(0.00, 0.00, 1.00),  // blue
    float3(0.05, 0.05, 0.05),  // near black
};

static inline float3 colourBars(float2 uv, float t)
{
    // Upper three quarters: bars, slowly scrolling so motion is always present.
    const float scrolled = fract(uv.x + t * 0.05);
    const int   index    = clamp(int(scrolled * 8.0), 0, 7);
    float3      colour   = kBars[index];

    // Lower quarter: a luminance ramp, for banding and black level.
    if (uv.y > 0.75)
    {
        const float ramp = saturate(uv.x);
        colour = float3(ramp, ramp, ramp);
    }

    return colour;
}

static inline float3 plasma(float2 uv, float t)
{
    float v = sin(uv.x * 10.0 + t)
            + sin(uv.y * 12.0 - t * 1.3)
            + sin((uv.x + uv.y) * 8.0 + t * 0.7)
            + sin(length(uv - 0.5) * 20.0 - t * 2.0);

    v *= 0.25 * 3.14159265;

    return 0.5 + 0.5 * float3(sin(v), sin(v + 2.09439), sin(v + 4.18879));
}

static inline float3 grid(float2 uv, float t, float aspect)
{
    const float2 p = uv * float2(aspect, 1.0) * 16.0 + float2(t * 0.5, t * 0.25);

    const float2 cell  = fract(p);
    const float  lines = step(0.94, max(cell.x, cell.y));

    return float3(0.04, 0.04, 0.06) + lines * float3(0.0, 0.85, 0.55);
}

fragment float4 fragment_main(VSOutput in [[stage_in]],
                              constant EffectConstants& c [[buffer(0)]])
{
    const int   pattern = int(c.params[0].x + 0.5);
    const float speed   = c.params[0].y;
    const float markers = c.params[0].z;

    const float  t      = c.time * speed;
    const float  aspect = c.resolution.x / c.resolution.y;
    const float2 uv     = in.uv;

    float3 colour;
    if (pattern == 0)
    {
        colour = colourBars(uv, t);
    }
    else if (pattern == 1)
    {
        colour = plasma(uv, t);
    }
    else
    {
        colour = grid(uv, t, aspect);
    }

    if (markers > 0.5)
    {
        // Sweeping vertical bar: one pass every four seconds at speed 1.
        const float sweep = fract(t * 0.25);
        const float bar   = smoothstep(0.0035, 0.0, abs(uv.x - sweep));
        colour = mix(colour, float3(1.0, 1.0, 1.0), bar);

        // Orbiting dot: circular motion exposes judder better than a bar.
        const float2 centred = (uv - 0.5) * float2(aspect, 1.0);
        const float2 orbit   = float2(cos(t * 1.5), sin(t * 1.5)) * 0.18;
        const float  d       = length(centred - orbit);
        colour = mix(colour, float3(1.0, 0.25, 0.1), smoothstep(0.022, 0.014, d));
    }

    return float4(colour, 1.0);
}
