# ATEM FX — Architecture

**Status: M0 implemented; M1 in progress.** FX-010 adds a standalone DeckLink
discovery command, with Windows build and hardware validation still pending.
Capture, playback and ATEM are not implemented. The M1 threading split and
the M4 control plane remain design — see [VIDEO_PIPELINE.md](VIDEO_PIPELINE.md) and
[ATEM_INTEGRATION.md](ATEM_INTEGRATION.md).

This file describes the structure that exists in `src/`. When a task
description conflicts with this document, **this document wins**.

The frame loop, ownership and CLI are in [RUNTIME.md](RUNTIME.md). How to add
an effect is in [EFFECT_SYSTEM.md](EFFECT_SYSTEM.md). Subject tracking and the
framing controller are in [TRACKING.md](TRACKING.md).

---

## 1. The one principle

The pixel pipeline and the control plane are **completely separate**.

```text
CONTROL PLANE                      PIXEL PIPELINE
─────────────                      ──────────────

UI Thread ────┐
ATEM Thread ──┤                    (M1) Capture ──► Process ──► Output
MIDI Thread ──┼──► PARAMETERS ──►
Audio Thread ─┘     (snapshot)     (M0) TestPattern ──► Chain ──► Preview
```

The ATEM, when it exists, will control *behaviour*. It is never part of the
pixel path. No control thread may ever block Capture, Process or Output.

Parameters cross the boundary by value, as a snapshot taken once per frame.
Never by shared mutable state read mid-frame. In M0 that snapshot is
`EffectContext`, filled in `App::updateEffectContext()` before the chain runs.

Subject tracking is control plane by the same rule. It runs on its own thread,
reads the frames capture already produced in system memory, and reaches the
pipeline only as a `TrackingSnapshot` copied into `EffectContext`. It never
touches the GPU and never reads back from it, so a detector that stalls or
fails costs no frame. Auto Frame writes the crop it chose back onto the same
context so the SOURCE preview can overlay it; that is display, not detection.
See [TRACKING.md](TRACKING.md).

---

## 2. Current pipeline (M0)

M0 has no capture and no playback hardware. The same processing core runs with
a GPU-generated test source, on a single thread, driven by the window loop
(or a tight loop in `--headless`).

```text
        ┌──────────────── UI Thread (Win32 / AppKit + ImGui) ──────────────┐
        │                                                                  │
        │   TestPatternSource ──► EffectChain ──► Preview (ImGui) / PPM    │
        │        (GPU)              (GPU)              (GPU)               │
        └──────────────────────────────────────────────────────────────────┘
```

M0 deliberately runs single-threaded: there is no capture clock to decouple
from yet. The seam where the processing thread will be split off is
`App::renderFrame()`, which already takes all its inputs from an
`EffectContext` snapshot rather than from global state.

**Processing always happens at the project resolution (1920×1080), regardless
of window size.** The window is a monitor, not the canvas. This is what keeps
M0's timings meaningful for M1.

The engine does **not** lock the frame rate to 59.94. In windowed mode the
display (or `CAMetalLayer nextDrawable`) paces the loop. 59.94 fps / 16.68 ms
is the engineering budget the timings are judged against, not a vsync mode.

---

## 3. Target pipeline (M1 onward)

**Status: Design — not implemented.** `src/decklink/` currently contains
discovery only. There is no `FrameQueue`, capture thread or output thread.

```text
DeckLink Capture Thread
        │
        ▼
   Frame Queue           bounded, lock-free, drops oldest
        │
        ▼
GPU Processing Thread
        │
        ▼
   Effect Chain
        │
        ▼
   Output Queue
        │
        ▼
DeckLink Playback
```

Contracts and seams for that split live in [VIDEO_PIPELINE.md](VIDEO_PIPELINE.md).

---

## 4. Module tree (what exists)

```text
src/
├── main.cpp          CLI, standalone discovery dispatch, DPI awareness, App lifetime
├── app/              Application lifetime, frame loop, wiring. Owns rendering.
├── core/             Minimal printf logger. No dependencies.
├── decklink/         Standalone discovery; optional Windows SDK implementation
│                     or unavailable stub. No capture or playback.
├── platform/         Window and event loop.
│   ├── win32/          Win32 message pump
│   └── mac/            AppKit window, manual event pump
├── gpu/              The rendering interface (Rhi.h) and its implementations.
│   ├── d3d11/          Direct3D 11 backend
│   └── metal/          Metal backend
├── video/            Video inputs behind VideoSource, plus FrameTiming.
│   ├── mac/            AVFoundation camera capture
│   └── win32/          Media Foundation camera capture
├── effects/          Effect abstraction, registry, chain, built-in effects.
│                     Per-parameter loop clocks and scalar evaluation.
│                     Depends on gpu/Rhi.h, never on a backend.
├── tracking/         Subject detection and the framing controller. Control
│   └── mac/          plane: no GPU, no readback, its own thread. Windows has
│                     no detector yet (docs/TRACKING.md).
└── ui/               ImGui panels, shared; ui/backend/ holds the one file per
                      platform that ImGui's own backends force us to split.
```

**Not present, and must not be invented ahead of their milestone:**
`src/atem/` (M4), `src/audio/` / `src/midi/` (M5).

`--list-decklink` is a separate startup path: it enumerates and logs devices,
then exits before creating `App`, a GPU device or UI. The SDK and COM types
stay inside `decklink_discovery_win.cpp`; `decklink_discovery.h` is portable.
CMake selects that implementation only for an opted-in Windows build
(`ATEMFX_ENABLE_DECKLINK=ON`, external `ATEMFX_DECKLINK_SDK_DIR`), and selects
`decklink_discovery_stub.cpp` otherwise. SDK build support lives in
`cmake/decklink.cmake`. Discovery has no dependency on the frame loop or RHI.

The file-level inventory is in [RUNTIME.md](RUNTIME.md) and
`memory-bank/file-map.md`.

Dependency direction is strictly downward:

```text
app ──► ui ──► effects ──► gpu/Rhi.h ◄── gpu/d3d11, gpu/metal
        │        │
        └────────┴──► video ──► gpu/Rhi.h

        effects ──► tracking/framing.h        (scalar, no GPU, no platform)
        app     ──► tracking/Tracker.h
```

Nothing above `gpu/Rhi.h` may include a backend header. One exception to the
module direction, deliberate: `video/` and `ui/` include
`effects/EffectParameters.h` directly. It and `parameter_automation.h` are
portable value types with no GPU or windowing dependency. `tracking/` is a
leaf on the same terms: `TrackingSnapshot.h` is a plain struct with no
includes at all, and `framing.h` adds only scalar arithmetic.

### Video inputs

Inputs are behind `src/video/VideoSource.h`, the same way the GPU is behind
`Rhi.h`:

```text
              VideoSource
                   │
   ┌───────────────┼──────────────────┐
TestPatternSource  CameraSource     DeckLinkSource (M1)
 (GPU generator)        │
                        ▼
                  CameraCapture
                  ┌─────┴──────┐
             AVFoundation   Media Foundation
```

`VideoSource` carries two optional hooks for consumers that work in the
source's own coordinates rather than the canvas: `setFrameObserver`, which
taps the frames capture produced in system memory, and `mapping()`, which says
where the source's image landed on the canvas. Tracking is the only consumer
today; a source with no CPU frames ignores both and the defaults are identity.

Every input produces the same thing: one texture at project resolution. The
chain, the UI and the frame loop never learn where it came from, which is why
adding SDI in M1 means adding an implementation rather than changing the
pipeline — and why the capture-to-GPU path is already built and exercised
before the hardware arrives.

The one place CPU pixels enter the pipeline is `TargetPool::upload`, because
capture hardware hands over system memory and there is no way around it. It is
a memcpy into a BGRA8 texture; everything after that — scaling to the project
raster, aspect handling, mirroring, the vertical flip that bottom-up capture
stacks need — happens on the GPU in `source_blit`.

---

## 5. The RHI seam

`src/gpu/Rhi.h` is the whole graphics interface. It is deliberately narrow:

```text
GpuTexture       a colour texture, renderable and samplable
ShaderLibrary    name  ──►  compiled shader, with hot reload
FullscreenPass   (target, shader, optional source, constants, filter, optional history)
TargetPool       two ping-pong targets plus persistent ones by key
GraphicsDevice   frame lifecycle, GPU timing, readback
```

The engine has exactly **one drawing primitive**: a fullscreen pass. Every
effect, the test source and the preview all go through it. That is why a second
backend was tractable at all, and it is why the interface must stay small. An
effect that needs more than a fullscreen pass gets a design, not a new RHI
entry point.

Backends are chosen at configure time in CMake. There is no runtime dispatch
and no virtual call in the inner loop beyond the effect itself.

Shaders are written twice, once per language, and live in `shaders/hlsl/` and
`shaders/metal/`. They must produce the same image. Transpiling from a single
source (DXC to SPIR-V to MSL) would remove the duplication and add a toolchain;
with six shaders that trade is not worth making yet. Revisit it at thirty.

A change to `EffectConstants` changes three files: the C++ struct in
`src/gpu/EffectConstants.h`, `shaders/hlsl/common.hlsli` and
`shaders/metal/common.metal`. Size is 96 bytes; a `static_assert` enforces it.

---

## 6. GPU resource model

The effect chain is a **ping-pong** between two off-screen render targets.

```text
source ──► [A] ──► effect 0 ──► [B] ──► effect 1 ──► [A] ──► ... ──► preview
```

Format: `R16G16B16A16_FLOAT` / `MTLPixelFormatRGBA16Float` for all processing
targets. This is deliberate: it gives headroom for accumulating effects and
maps cleanly onto the 10-bit YUV that DeckLink will deliver in M1. The
presentation surface stays 8-bit (`R8G8B8A8_UNORM` / `BGRA8Unorm`).

Two targets are not always enough. Feedback and trail effects need buffers that
survive across frames, so `TargetPool` also hands out **persistent** targets by
key:

```text
FRAME N
   +
FRAME N-1  ◄── TargetPool::persistent("feedback.history")
   ↓
FEEDBACK
```

`TestPatternSource` already uses this: it writes to
`TargetPool::persistent("source.frame")` so the generator's output is not
eaten by the chain's ping-pong.

**This is infrastructure, not M2.** FX-007 (effect graph) and FX-008 (a
feedback *effect*) are still open. Do not treat `persistent()` as those
milestones being done.

Never assume `input → shader → output` is sufficient. Effects that need history
request it; the chain does not need to know.

---

## 7. Effect execution contract

Every effect receives an `EffectContext` and must:

- read only from the source texture it is given;
- write only to the destination texture it is given, plus any persistent
  targets it owns;
- leave no source bound on exit (the chain relies on this to avoid
  read/write hazards when a target is reused);
- never allocate, never block, never touch the filesystem inside `process()`.

Shader compilation, buffer creation and any other allocation happen in
`initialize()`.

`FullscreenPass::draw` unbinds on both backends. An effect that drives the
RHI itself must do the same.

### Parameter automation

Loop automation belongs to each parameter of an effect instance. The chain
advances all parameter clocks once per `process()` from
`EffectContext::deltaTime`, before the enabled-effect check. A bypassed effect
retains a moving clock; selection and order in the UI have no effect on it.
Each loop can be paused independently.

`Parameter::currentValue()` resolves automation without overwriting the manual
value. `ShaderEffect` packs that effective scalar into the existing shared
constant buffer. Evaluation is bounded scalar CPU work, with no allocation,
locking, logging or CPU image processing in steady state; rendering remains
entirely on the GPU. The RHI and both shader layouts stay unchanged.

This is a user-requested extension of the linear effect system. Settings are
session-only. It introduces neither the M2 DAG nor M3 presets nor M5 external
modulation. The full control contract is in [EFFECT_SYSTEM.md](EFFECT_SYSTEM.md).

---

## 8. Timing and measurement

Two clocks are measured, always:

- **CPU frame time** — wall clock between frame starts, `FrameTiming`
  (`std::chrono::steady_clock`), 240-sample ring buffer.
- **GPU time** — the processing pass only, never the UI:
  - Direct3D 11: timestamp queries read back with a three-frame lag and
    `DONOTFLUSH`, so the CPU never stalls on `GetData`.
  - Metal: `GPUEndTime - GPUStartTime` on the processing command buffer,
    collected in a completion handler on a background thread and published
    through atomics.

  Both are asynchronous by construction. A timer that stalls the pipeline to
  report how fast the pipeline is would be worse than no timer at all.

Budget for the primary target (1920×1080 59.94):

```text
Processing budget:        < 16.68 ms per frame
Preferred GPU processing: <  8.00 ms
Latency goal:             <= 3 video frames, excluding ATEM processing
Dropped frames:           0 under normal operation
```

The latency goal is an engineering target, not a commercial promise.

Low-latency presentation choices in M0:

- Direct3D 11: DXGI flip-model swap chain, `SetMaximumFrameLatency(1)`,
  tearing allowed when vsync is off.
- Metal: frame pacing comes from `CAMetalLayer nextDrawable`, which is why the
  macOS event loop is pumped manually instead of being handed to
  `-[NSApplication run]`. Vsync is `displaySyncEnabled`.

`GraphicsDevice::readback` stalls the GPU. It is for `--dump` and diagnostics
only, never the frame loop.

---

## 9. Verifying without a display

`atem_fx --headless` builds the device with no window and no UI, runs the
source and the chain for N frames, prints the timings and can write the final
frame with `--dump`. It exercises the device, the shader compiler, the ping-pong
and every effect, and it works over SSH and in CI.

That path is not a toy: it is how the pipeline was validated in the first
place, and it is the check every change has to pass.

```bash
./build/bin/atem_fx --headless --frames 200
```

The portable parameter automation test additionally runs through CTest with
`BUILD_TESTING=ON`, without a GPU or third-party test framework. It checks the
scalar loop contract; it does not replace the headless rendering gate.

---

## 10. Threading rules

| Thread            | Owns                                  | May block on | Status      |
| ----------------- | ------------------------------------- | ------------ | ----------- |
| UI                | Window messages, ImGui, parameter edits, and in M0 the whole pipeline | anything | **M0: this is the only thread** |
| Processing        | D3D11 immediate context, effect chain | nothing      | M1: split off `App::renderFrame()` |
| Capture           | DeckLink input callback               | nothing      | M1          |
| Output            | DeckLink scheduled playback           | nothing      | M1          |
| ATEM              | Switcher socket                       | network      | M4          |
| MIDI / Audio      | Device callbacks                      | device       | M5          |

Metal GPU timing uses a completion handler on a background thread that writes
atomics. That is not a processing thread.

In M0 the processing work runs on the UI thread. That is a known, temporary
simplification, valid only while there is no external video clock. It is
recorded here so it is removed deliberately in M1, not discovered by accident.

---

## 11. Ownership

`App` owns every rendering subsystem:

```text
App
 ├── Window              (null when headless)
 ├── GraphicsDevice      (MetalDevice or D3D11Device)
 ├── TestPatternSource
 ├── EffectChain
 ├── FrameTiming
 └── UiLayer             (idle when headless)
```

`std::unique_ptr` for ownership, raw pointers or references for borrowing.
`EffectContext` and `UiFrameState` are per-frame snapshots of borrowed
pointers, not owners.

The standalone discovery command owns and releases its SDK/COM resources
within that command. It never creates an `App`.
