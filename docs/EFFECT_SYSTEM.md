# CamVJ — Effect System

**Status: Implemented (M0).** The engine ships a linear `EffectChain`, a
registry, generic `ParameterSet` UI, and eleven built-in effects — four from
M0, `auto_frame` from the subject tracking extension, plus `subpixel`,
`fm_raster`, `crt`, `shutter`, `frame_delay` and `vhs`. A DAG / graph
editor (FX-007) and a feedback *effect* (FX-008) are not done. Persistent
targets exist as infrastructure; that is not M2.

The current extensions add optional looping automation to each parameter of
each effect node, and `auto_frame`, which follows a tracked subject
([TRACKING.md](TRACKING.md)). Both use the existing linear chain, with no
graph editor, preset storage or external control source. Validation of these
extensions is pending.

---

## Goal

Thirty effects should be reachable without rebuilding the engine. The pipeline
must never know what an effect *is*.

Forbidden:

```cpp
if (rgbSplit) ...
if (glitch)   ...
if (pixelate) ...
```

---

## Anatomy

```text
INPUT ──► EFFECT NODE ──► EFFECT NODE ──► EFFECT NODE ──► OUTPUT
```

Each node is an `Effect` (`src/effects/Effect.h`):

```cpp
class Effect {
    const EffectDescriptor& descriptor();   // type id, display name, category
    ParameterSet&           parameters();   // generic, UI-renderable
    bool                    enabled();

    bool initialize(EffectContext&);                    // allocate here
    bool process(EffectContext&, source, destination);  // hot path
    void shutdown();
};
```

`process()` returns true if `destination` was written. False means the effect
declined to run and the chain keeps the previous image (a broken shader is a
no-op, not a black frame).

`ParameterSet` is the whole reason the UI is generic. A parameter carries its
own id, label, type, range and default, so `EffectsPanel` renders any effect —
including ones written after the UI was compiled — without a single `if`.

Types today: `Float`, `Int`, `Bool`. An integer can optionally carry zero-based
choice names (`Parameter::makeChoice`); the generic UI draws those as a combo
while packing and automation remain scalar. Test Pattern uses this for its
named patterns. Integers without choices, such as Mirror's mode, remain sliders.

---

## Parameter loops

Every `Parameter` owns a `ParameterAutomation` value
(`src/effects/parameter_automation.h/.cpp`). Two instances of the same effect
have independent settings and clocks. Selecting another node or reordering
the chain does not reset or redirect its automation. Removing a node removes
its loops.

| Setting | Behaviour |
| ------- | --------- |
| Loop | Enable automation; disabled parameters use their manual value |
| Shape | Sine, Triangle, Ramp up, Ramp down or Pulse (Square) |
| Range | Lower and upper values, bounded by the parameter's declared range |
| Cycle (s) | Seconds for one complete cycle; 0.05–600, default 4 |
| Phase (%) | Offset within the cycle, 0–100%; stored as 0–1, with 1 wrapping to 0 |
| Pause / Resume | Freeze or resume the loop clock |
| Restart | Reset the clock to zero, preserving the configured phase offset |

The waveform is evaluated in the normalized range 0–1 and mapped to the
configured value range. Sine and Triangle travel from the lower value to the
upper value and back; ramps wrap at the cycle boundary; Square alternates
between the two bounds. Int parameters round to whole values. Bool parameters
use a 0.5 threshold and default to Square.

`Parameter::value` remains the manual setting. `currentValue()` resolves the
loop, clamps to the declared range and applies the parameter type. Accessors
(`asFloat`, `asInt`, `asBool`) and `ParameterSet::valueOr` use that effective
value. Disabling Loop restores the preserved manual setting and stops its
clock; enabling it again resumes the saved phase. A parameter reset restores
its manual default and clears its loop configuration, pause state and clock.

`EffectChain::process()` advances every node's parameter clocks exactly once,
using `EffectContext::deltaTime`, before checking which effects are enabled.
Bypassing an effect therefore keeps its active loops moving, even when its
parameters are hidden. Paused or disabled loops do not advance. The clock uses
a wrapped double-precision phase instead of an ever-growing elapsed counter.
Evaluation performs only scalar arithmetic, with no allocation, locks, logging
or image processing on the CPU.

Loop controls are available in the effect parameter panel; source parameters
keep their existing manual controls. While Loop is enabled, the parameter's
normal control displays the effective value and manual editing is disabled.
Right-clicking that control still resets the parameter and removes the loop.
All loop state lasts for the current session only. Persistence remains M3;
MIDI and audio modulation remain M5.

---

## ShaderEffect

Almost every effect is "one pixel shader plus some scalars". `ShaderEffect`
implements that case completely:

- resolves a base name — `"rgb_split"` — to the backend's own source file
  (`shaders/hlsl/rgb_split.hlsl` or `shaders/metal/rgb_split.metal`);
- packs each parameter's `currentValue()`, in declaration order, into the
  shared constant block;
- runs one fullscreen pass from the source texture to the destination texture.

The effect owns no GPU resources at all, which is why the same class works
unchanged on Direct3D 11 and on Metal. Shader handles are looked up by name
every frame so a hot reload cannot leave a dangling pointer.

Automation resolves before packing, so both backends receive the same scalar
values. It requires no new shader, RHI entry point or constant-buffer field.

### Constant buffer layout

Shared by every effect and by the test source. It exists three times and all
three must agree byte for byte: `EffectConstants` in
`src/gpu/EffectConstants.h`, the `cbuffer` in `shaders/hlsl/common.hlsli`, and
the `struct` in `shaders/metal/common.metal`.

The C++ struct carries a `static_assert` on its size (96 bytes) for exactly
this reason.

```hlsl
cbuffer EffectCB : register(b0)
{
    float2 uResolution;      // pixels
    float2 uInvResolution;   // 1 / pixels
    float  uTime;            // seconds since start
    float  uDeltaTime;       // seconds
    float2 uPad0;
    float4 uParams[4];       // 16 scalar parameter slots
};
```

Parameter `i` declared by the effect lands in `uParams[i / 4][i % 4]`.
Bools arrive as `0.0` or `1.0`, ints as their float value.

Sixteen slots is a deliberate ceiling. An effect that needs more needs its own
constant buffer and should override `ShaderEffect::packConstants`, or derive
from `Effect` directly.

Overriding `packConstants` is also how an effect sends the shader something
other than its parameters. `auto_frame` has thirteen controls and packs six
values: the operator configures a framing controller, and the shader receives
the source crop plus the (fixed) output window — full frame for 16:9, a
centred letterbox for 9:16. The node also writes `EffectContext::framing` so
the SOURCE preview can draw the crop on the camera, not on the already-cropped
program picture.

---

## Adding an effect — the complete checklist

1. `shaders/hlsl/my_effect.hlsl` — include `common.hlsli`, write `main`.
2. `shaders/metal/my_effect.metal` — write `fragment_main`. The shared
   declarations are prepended automatically; MSL compiled from source has no
   include path, so do not try to `#include` them.
3. `src/effects/MyEffect.cpp` — describe it, declare its parameters. Copy
   `PassthroughEffect.cpp`.
4. One line in `src/effects/BuiltinEffects.cpp`.

Nothing else. Not the renderer, not the chain, not the UI.

Then verify it on both, or at least on the one you can run:

```bash
./build/bin/atem_fx --headless --frames 60 --enable my_effect --dump my_effect.ppm
```

The two shaders must produce the same image. Differences to watch for: MSL has
no `radians()`, `lerp` is `mix`, `frac` is `fract`, and array constants live in
the `constant` address space.

Registration is explicit rather than a static-initialiser trick: the order in
`BuiltinEffects.cpp` is the order shown in the "Add effect" menu, and explicit
registration cannot be silently dropped by the linker.

---

## Built-in effects

Registered in `src/effects/BuiltinEffects.cpp`. Default chain in
`App::createDefaultChain()` adds all eleven; `auto_frame` starts enabled
unless `--enable` overrides. Auto Frame is first so the rest of the chain
treats the LED picture, not the wide shot. `shutter` and `frame_delay` sit
after `subpixel` so the look can smear and echo; `vhs` then `crt` are last,
in that order — the tape damages the signal and the tube then displays it.

| typeId        | Display     | Category | Parameters                                      | Sampler |
| ------------- | ----------- | -------- | ----------------------------------------------- | ------- |
| `passthrough` | Passthrough | Utility  | none                                            | Point   |
| `rgb_split`   | RGB Split   | Distort  | `amount` 0–1 (0.20), `angle` 0–360, `radial`    | Linear  |
| `pixelate`    | Pixelate    | Distort  | `size` 1–256 (16), `mix` 0–1                    | Point   |
| `fm_raster`   | FM Raster   | Distort  | `frequency`, `modulation`, `threshold`, `line_width`, `direction`, `speed`, `contrast`, `mix` | Linear  |
| `subpixel`    | Subpixel    | Distort  | `grid`, `threshold`, `scatter`, `rgb_spread`, `noise_scale`, `speed`, `stretch`, `mix` | Point   |
| `shutter`     | Shutter     | Temporal | `decay`, `threshold`, `mix`                     | Linear  |
| `frame_delay` | Frame Delay | Temporal | `copies` 1–24 (8), `spacing` 1–30 (3), `blend` 0–2, `key`, `freeze`, `mix` | Point   |
| `vhs`         | VHS         | Distort  | `wobble`, `chroma_bleed`, `noise`, `tracking`, `dropouts`, `ghost`, `interlace`, `wear`, `speed`, `mix` | Linear  |
| `crt`         | CRT         | Distort  | `scanlines`, `mask`, `aberration`, `contrast`, `mix` | Linear  |
| `mirror`      | Mirror      | Geometry | `mode` 0–4, `pivot` 0–1 (0.5)                   | Linear  |
| `auto_frame`  | Auto Frame  | Framing  | `follow`, `portrait`, framing — see TRACKING.md | Linear  |

`passthrough` exists to prove the chain is wired and as the template to copy.

`fm_raster` turns luma into the phase of a sine, then thresholds it into
scanlines. Dark regions emit nothing, so a face stays readable as electronic
topography. Place it before `subpixel` when chaining.

`subpixel` is a luminance-gated RGB cell scatter: each grid cell emits separate
R, G and B sprites that orbit inside the cell. Sprites stay sharp because the
shader draws them as rectangles from one point sample, rather than warping the
photograph. A neighbourhood gather that let cells fly into empty space was
measured over budget at 1920×1080, so scatter is local to the cell. It is not
`pixelate` plus `rgb_split`.

`shutter` smears motion across frames. It keeps the last output in
`TargetPool::persistent()`, samples it as a second texture on the same
fullscreen pass, then blits the mix back. Decay is how long the trail lives;
threshold ignores small deltas so a still frame stays sharp. This is not the
M2 feedback graph (FX-008): one node, one history, linear chain.

`frame_delay` is the other half of that pair and the opposite look: where
`shutter` blurs motion into one smear, this leaves the picture from `spacing`
frames ago standing behind the live one, again and again — the echo a dancer
drags across a dark stage. Every `spacing` frames the live picture is stamped
into a trail and what is already there fades by a decay derived from `copies`,
so the trail carries one step back at full strength, two steps back faded once,
three faded twice. That sum is what a multi-tap delay line would produce, at
two passes a frame instead of one pass and one 1080p target per copy. The
copies stay sharp because nothing is ever resampled: the trail is read and
written at the same resolution, on the same UVs, with a point sampler.

`blend` picks which picture wins where they overlap: Lighten (0) and Screen
(1) keep the brighter one, for a lit body on a dark stage; Darken (2) is the
same arithmetic inverted, for a dark subject against a bright wall. `key`
decides what is allowed to leave a copy at all, so background grain does not
smear itself across the frame. `freeze` stops the trail without clearing it —
the copies already on the wall stay where they are. A bypassed node re-seeds
when it comes back: enabling the effect starts a new echo instead of flashing
whatever was on air when it was switched off, and the same applies after the
input is lost and returns.

`vhs` is the tape, not the television: line drift from servo error, chroma
smeared to the right of its luma, snow, dropout dashes, a mistracking band
that rolls up the frame, the head-switch tear along the bottom, and tape wear
that lifts the blacks. Six samples, no blur pass, so it costs about what `crt`
costs. Chain `vhs` → `crt` for the full old-television look; each alone is
half the picture. `speed` at 0 freezes every artefact into a still.

`crt` adds scanlines, an RGB aperture grille and a light chromatic split. It
is not a bloom; glow would need a downsample the RHI does not offer. Keep it
last so it sits on the already-processed picture.

`auto_frame` is the one built-in that reads something other than its own
parameters: the subject's position, from `EffectContext::tracking`. It is also
the one that keeps state between frames — a `FramingController`, per node, the
same way each parameter keeps its own loop clock. Its parameters configure that
controller rather than reaching the shader, so it overrides `packConstants` and
packs the source crop and the output window the controller produced. **Portrait
(9:16)** letterboxes a vertical crop into the 1920×1080 canvas; the canvas
itself does not change. The full behaviour, including what happens when the
subject disappears, is in [TRACKING.md](TRACKING.md).

Shaders that are **not** effects: `test_pattern` (the source generator),
`fullscreen.hlsl` (D3D11 shared vertex shader), `common.hlsli` /
`common.metal` (shared declarations).

Glitch appears in the original product vision and is not implemented. Do not
document it as if it were. `shutter` covers the temporal trail the vision
called Trails, and `vhs` covers the tape look it called VHS.

---

## Runtime UI

`EffectsPanel` (`src/ui/EffectsPanel.cpp`) is fully generic:

- combo of `EffectRegistry::entries()` to add a type;
- enable checkbox, up/down reorder, remove;
- clicking a row inspects that node.

Its parameters are drawn by the inspector (`src/ui/InspectorPanel.cpp`), the
wide panel under the preview, which otherwise shows the stats strip:

- `drawParametersColumns()` walks the inspected node's `ParameterSet`;
- each parameter exposes Loop and generic waveform, range, cycle, phase,
  pause/restart controls with a curve preview.

Both layouts share one per-parameter renderer, so the sidebar's single column
and the inspector's several cannot drift apart. An effect that needs bespoke UI
code is a design smell. Adding a built-in never requires changing either panel.

The default chain is a convenience for the first frame, not a preset system.
Presets (FX-009) are M3.

---

## Effects that need history

Feedback, trails and motion effects need buffers that outlive the frame. Ask
the pool, by key:

```cpp
GpuTexture& history = ctx.targets->persistent("feedback.history");
```

The pool creates it on first request at project resolution and keeps it until
shutdown. The chain's ping-pong is unaffected. `shutter` is the first shipped
effect that does this: it allocates in `initialize()`, samples the history as
texture slot 1, and blits the mix back. Call `persistent()` in `initialize()`,
never on the per-frame path.

`frame_delay` needs two, and they are the reason: a pass cannot read and write
the same texture, so the trail is ping-ponged between them — one holds the
copies as they stand while the other receives them faded with the live frame
stamped in. Both are allocated in `initialize()`, which is what keeps 33 MB of
allocation out of the moment the operator enables the effect on air.

Otherwise only `TestPatternSource` uses the pool, writing `"source.frame"`.

---

## Hazard rule

The chain reuses two targets in rotation, so a target that was read this pass
becomes a render target two passes later. `FullscreenPass::draw` handles the
unbinding on both backends; an effect that drives the RHI itself must do the
same.
