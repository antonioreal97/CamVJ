# CamVJ — Overlays

**Status: FX-026 implemented as a narrow M6 extension.** CamVJ can import
operator-authored transparent PNG graphics, keep 16:9 and 9:16 variants under
one logical asset, and composite a bounded four-layer stack on the GPU. Static
PNG and PNG-sequence playback are implemented. Fill/key output, blend modes,
video files, text generation and a general layer graph are not.

This document is the contract for `src/overlays/`. The frame order and PROGRAM
semantics remain canonical in [RUNTIME.md](RUNTIME.md).

---

## Operator model

The sidebar section is **OVERLAYS**, between PRESETS and EFFECTS. Import opens
the native file or folder picker and adds content to CamVJ's managed library;
it never puts the graphic on air. The operator explicitly adds a library asset
to the active stack.

An asset has a stable ID, a display name, one media type and up to two variants:

| Variant | Required source raster | Placement on the engine canvas |
| ------- | ---------------------- | ------------------------------ |
| 16:9    | exactly 1920×1080      | full 1920×1080 canvas          |
| 9:16    | exactly 1080×1920      | scaled into the centred 9:16 output strip |

CamVJ does not fit, crop or stretch an invalid file into compliance. A new
import infers the variant from the exact raster. Adding or replacing a variant
uses the format slot selected by the operator and validates it against that
slot. Both variants of a PNG sequence must contain the same number of frames.

The stack contains at most four assets. The panel lists it front-to-back; the
runtime and preset representation store it back-to-front so normal alpha
compositing is deterministic. Layers expose enable, opacity and order. PNG
sequences also expose loop/one-shot, playback rate from 1 to 30 fps, pause and
restart. A sequence contains 2–300 naturally sorted PNG frames. Several
sequence assets may remain in the stack, but only one may be enabled or fading
out at a time.

Enable, disable and removal use the shared 0.35 second eased envelope. A
one-shot returns to disabled after its final frame and fade. Replacing the
variant currently in use keeps the last-good texture visible until the first
new frame is ready, then transitions to the replacement over 0.35 seconds.

---

## Pixel pipeline and PROGRAM

Overlays are a dedicated compositor, not effects and not effect-chain nodes:

```text
VideoSource → EffectChain → OverlaySystem → ProgramOutput → consumers
                                │
                                └──────────────► FX preview bus
```

`OverlayCompositor` performs one fullscreen GPU pass for each visible layer,
using premultiplied normal alpha. It owns two RGBA16 processing targets for
ping-pong; decoded images enter through fixed BGRA8 upload textures. It does
not widen `gpu/Rhi.h` and has matching `overlay_composite` shaders in HLSL and
MSL.

The existing `effectMix` also multiplies overlay opacity. Consequently:

- **FX** shows the effects and overlay stack;
- **Clean** retains framing but dissolves both visual effects and overlays out;
- **Freeze** and **Black** replace the whole composed picture while capture,
  tracking, effects, overlay clocks and sequence preparation keep running;
- the **FX** preview bus includes overlays before `ProgramOutput`, so the next
  look remains visible while PROGRAM is held;
- the LED Mapping calibration source bypasses both effects and overlays.

Only the variant matching `EffectContext::outputAspect` is sampled. A missing
variant makes that layer transparent and reports the reason in the OVERLAYS
panel; it never substitutes the other format. The 1920×1080 engine canvas is
unchanged in portrait mode.

---

## Managed library and import

The library lives under the platform Application Support directory:

```text
CamVJ/overlays/
├── ovl_<stable-id>/
│   ├── manifest.json
│   └── content_<version>/...
├── .staging/
└── .trash/
```

Selected files are copied into `.staging` before PNG and raster validation, so
the bytes published are the bytes that were checked. A complete asset is
published by rename; a variant replacement copies its new content first and
then atomically replaces the manifest. Cancellation or failure leaves the old
manifest authoritative. Removing an unreferenced asset moves its directory to
`.trash` instead of recursively deleting it from the UI thread.

The picker, validation and copy run outside GPU processing and can be
cancelled. The macOS implementation uses AppKit plus ImageIO/CoreGraphics; the
Windows implementation uses the shell dialog plus WIC. No third-party image
library was added. The library owns imported content, so moving or deleting the
operator's original file after a successful import does not break a show.

Asset removal is refused while the asset is in the active stack or referenced
by a user preset. Operation lock blocks imports, variant replacement, removal,
layer add/remove/reorder and preset recall/save. It deliberately leaves layer
visibility, opacity and sequence transport available as live controls.

---

## Real-time contract

PNG files are decoded on one background worker. The processing frame only
drains a bounded single-producer/single-consumer queue, uploads already decoded
BGRA pixels into a fixed texture ring, advances scalar clocks and submits GPU
passes. After initialization, steady-state overlay processing performs no disk
I/O, allocation, locking or logging.

There are two upload textures per static layer and four shared sequence upload
textures per aspect. Only one sequence is active, so the shared ring is
sufficient. The decoder keeps eight reusable result buffers. If every upload
slot is still in use by the GPU, the last-good frame remains visible and the
underflow counter advances; the render thread never waits.

On Metal, overlay upload textures carry the serial of their most recent GPU
read. A command-buffer completion handler publishes the completed serial and
an upload refuses a texture still in flight. That tracking is private to the
Metal backend and only applies to resources named `overlay.*`; camera upload
behaviour and the public RHI are unchanged. D3D11 relies on the same fixed-ring
ownership contract through its backend upload path.

---

## Presets and recovery

Scene preset schema version 2 stores the back-to-front overlay stack: asset ID,
enabled state, opacity, loop/one-shot mode and playback fps. Runtime layer IDs,
current frame, pause state and fade progress are not stored. Version 1 presets
remain valid and load with an empty overlay stack. Factory looks intentionally
carry an empty stack.

Recall validates every referenced asset and the one-active-sequence invariant
before rebuilding either effects or overlays. A missing or duplicate asset,
invalid value or unsupported stack fails the whole recall rather than leaving a
half-applied look. Venue boot remains separate and still stores only routing
and portrait state.

Malformed library manifests are skipped with warnings. Decode or upload
failure keeps the last-good frame where one exists, otherwise leaves that layer
transparent, and reports an error without stopping PROGRAM.

---

## Verification

Portable checks cover playback/fades, compositor order and failure fallback,
managed-library import/replace/cancellation/removal, and scene-preset v1→v2
migration. The required rendering gates remain:

```bash
ctest --test-dir build --output-on-failure
./build/bin/atem_fx --check-shaders
./build/bin/atem_fx --headless --frames 200
```

The macOS implementation is built and GPU-validated. The Windows overlay code
is present behind the same contract but still requires a Windows build and
native picker/WIC validation.
