# ATEM FX — Engineering Rules

ATEM FX is a real-time video effects engine for live production with Blackmagic
ATEM switchers.

Primary target: **1920x1080, 59.94 fps, zero dropped frames.**

This file is the constitution of the project. `docs/ARCHITECTURE.md` defines the
structure; `docs/RUNTIME.md` the frame loop; `docs/VIDEO_PIPELINE.md` the M0
pipeline and the M1 contract. When a task description conflicts with these
documents, **the documents win** — raise the conflict instead of silently
deviating.

`README.md` is operator onboarding. It is not a spec. The original product
essay is `docs/VISION.md` and must not be read as a file map.

Agent working memory (Portuguese) lives in `memory-bank/`. Keep it in sync
when the architecture or milestone status changes.

---

## 1. Platforms and technology

| Area          | Technology                              | In the tree today      |
| ------------- | --------------------------------------- | ---------------------- |
| Language      | C++20 (Objective-C++ for macOS glue)    | yes                    |
| Build         | CMake                                   | yes                    |
| Windowing     | Win32 / AppKit                          | yes                    |
| GPU           | Direct3D 11 / Metal                     | yes                    |
| Shaders       | HLSL (SM 5.0) / MSL                     | yes                    |
| UI            | Dear ImGui                              | yes (only third-party) |
| Logs          | spdlog                                  | **planned** — `src/core/Log.cpp` is a printf wrapper with spdlog-shaped macros |
| Tests         | Standalone C++ checks / Catch2 planned   | parameter automation checks via CTest; `--headless` remains the rendering gate |
| Config        | JSON                                    | **planned** (M3 presets) |
| SDI           | Blackmagic DeckLink SDK (M1+)           | optional Windows discovery; capture/playback **planned** |
| ATEM control  | Blackmagic ATEM SDK (M4+)               | **planned**            |
| Audio         | WASAPI / CoreAudio (M5+)                | **planned**            |
| MIDI          | RtMidi (M5+)                            | **planned**            |
| Profiling     | PIX / Xcode GPU capture                 | tools, not a dependency |

**Windows is the production platform.** It is the only one with DeckLink and
ATEM SDK support, so it is the platform the product ships on. macOS is a
first-class development and demonstration target: it builds, runs and is
verifiable, which matters because the whole M0 core was written and validated
there.

Two backends, one interface. Exactly one is compiled into a build; the
selection happens in CMake, never at runtime.

```text
                 src/gpu/Rhi.h          src/platform/Window.h
                       │                        │
        ┌──────────────┴──────────┐   ┌─────────┴─────────┐
   gpu/d3d11              gpu/metal   platform/win32   platform/mac
```

### Backend rules

- Portable code — `app/`, `effects/`, `video/`, `ui/` except its backend file
  — must never name a graphics or windowing type. `ID3D11*`, `id<MTL*>`, `HWND`
  and `NSView` do not appear above the seam.
- A feature that needs a wider RHI needs a design discussion, not a new entry
  point added in passing. The interface is small on purpose.
- Every effect ships **both** shaders, HLSL and MSL, and they must produce the
  same image. A backend-only effect is not finished.
- A change to `EffectConstants` changes three files: the C++ struct,
  `shaders/hlsl/common.hlsli` and `shaders/metal/common.metal`.
- Do not add a third backend speculatively.

## 2. Priorities, in order

1. Video stability
2. Low latency
3. No dropped frames
4. Thread safety
5. GPU-first processing
6. Maintainable architecture

When two priorities conflict, the lower number wins. A feature that is elegant
but risks a dropped frame does not ship.

---

## 3. Hard rules

- Never perform expensive image processing on the CPU when the operation can
  reasonably be performed by the GPU.
- Video capture, GPU rendering and video output must remain decoupled modules.
- Real-time video threads must never be blocked by UI work, network calls or
  disk IO.
- Never block the GPU thread waiting on a query result. Poll asynchronously.
- No allocation, no locking and no logging inside the per-frame hot path once
  steady state is reached.
- Do not add external dependencies without documenting why in `docs/`.
- Every change must at least pass `atem_fx --headless --frames 200` on macOS
  before it is called done. It needs no display and no hardware.
- Do not redesign unrelated parts of the architecture while implementing an
  isolated feature.
- All new effects must use the `Effect` abstraction. No `if (rgbSplit)` chains
  anywhere in the pipeline.
- Every hardware integration must provide useful diagnostic logging.
- Prefer small classes and explicit ownership. `std::unique_ptr` for ownership,
  raw pointers or references for borrowing.

---

## 4. Adding an effect

Adding an effect must never require changing the renderer, the effect chain or
the UI. The complete cost of a new effect is:

1. one HLSL file in `shaders/hlsl/` and one MSL file in `shaders/metal/`;
2. one `.cpp` in `src/effects/` declaring its parameters;
3. one registration line in `src/effects/BuiltinEffects.cpp`.

If a task requires more than that, the abstraction is wrong — fix the
abstraction, and say so.

The UI renders parameters generically from the effect's `ParameterSet`. An
effect that needs bespoke UI code is a design smell.

See `docs/EFFECT_SYSTEM.md`.

---

## 5. Scope discipline

Each milestone has a closed scope. Do not implement audio, MIDI, ATEM,
DeckLink, recording, streaming, layers, blend modes or presets before the
milestone that owns them (`docs/ROADMAP.md`).

Do not create `main.cpp` with 5000 lines. Follow the tree in
`docs/ARCHITECTURE.md` and the file map in `docs/RUNTIME.md`.

---

## 6. Code style

- `snake_case` for files matching the module they implement, `PascalCase` for
  types, `camelCase` for functions and variables, trailing `_` for private
  members.
- Headers self-contained; include what you use.
- No exceptions across module boundaries in the video path — return status and
  log.
- Comments explain *why*, not *what*. A comment that restates the code is noise.

---

## Learned User Preferences

- Chat in Portuguese (Brazil). Keep `docs/` and `AGENTS.md` in English; `memory-bank/` and `README.md` in PT-BR.
- Tracking exists to put a walking presenter on LED walls: PROGRAM is the picture for the panel and must keep the subject centered, with a visible crop preview.
- LED walls are 16:9 landscape or 9:16 portrait. Keep the engine canvas at 1920×1080 and letterbox 9:16 rather than switching project resolution.
- Auto Frame sliders must visibly retarget the crop; the dead zone is for detector jitter, not operator changes. Landscape 16:9 must still follow (punch in) so PROGRAM is not identical to SOURCE.
- Visible product name is `CamVJ` (mixed case). Theme lives in `src/ui/Theme.*`. Tungsten (`#FF9B3D`) only for the locked subject and the LIVE tally. SOURCE is Split Cyan, PROGRAM is Split Magenta. Studio Black background. No gradient, shadow, or glow. Fixed layout, not dockable.

## Learned Workspace Facts

- macOS Vision is the production person detector for tracking; the Windows detector is deferred and does not block this feature.
- There is no video output on macOS (no DeckLink playback). Routing the treated picture to ATEM or Resolume is unsolved and unscoped.
- Default chain: `auto_frame` first and enabled, then passthrough / rgb_split / pixelate / fm_raster / subpixel / shutter / mirror / crt off, with Follow Subject on.
- Subpixel draws luma-gated RGB sprites inside each cell. A neighbourhood gather missed the 1080p60 budget (~16–82 ms); the shipped pass is ~3 ms on M4.
- Shutter keeps the last output in `TargetPool::persistent()` and samples it as t1. That optional history argument on `FullscreenPass::draw` is still one primitive, not the M2 graph.
- Preview is split SOURCE (pre-chain camera plus yellow subject and cyan crop overlays) and PROGRAM (chain output; 9:16 UV-crops the centre strip).
- 9:16 output is a centred letterbox strip on the 1920×1080 canvas; the strip stays fixed so Resolume can crop a static mapping.
- Only camera input feeds the tracker. Test Pattern has no CPU BGRA and reports `no frames from this input`.
- Framing lives in `src/tracking/framing.cpp` with CTest in `tests/framing_test.cpp`. Auto Frame packs the source crop plus output window into the shader without widening `EffectConstants`.
- Development camera is a Sony ILME-FX30 in USB Streaming (UVC 1080p30). macOS discovery must include `ExternalUnknown`; session presets often claim 1080p then deliver no frames — pick 1920×1080 from the device format list. Camera hotplug is AVFoundation notifications polled between frames, not DeckLink.
- The binary stays `atem_fx` and the bundle id `fx.atem.engine` so macOS camera permission is not invalidated. Brand assets and rules live in `assets/files/IDENTIDADE.md`.
