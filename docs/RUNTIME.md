# ATEM FX — Runtime

**Status: M0 implemented; M1 discovery added, pending Windows validation.**
This is the call sequence and ownership of the running process. Architecture
principles live in [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Entry

`src/main.cpp`:

1. On Windows, set per-monitor DPI awareness so the preview is not scaled
   twice.
2. Parse CLI, keeping rendering options in `AppOptions` (`src/app/App.h`).
3. If `--list-decklink` was requested, enumerate and log devices, then exit.
   This path creates no `App`, window, GPU device or UI.
4. Otherwise: `App app; app.initialize(options); app.run(); app.shutdown();`

Unknown flags → usage, exit 2. Init failure → exit 1.

### Standalone DeckLink discovery (FX-010)

`--list-decklink` is exclusive: combining it with `--headless`, `--frames`,
`--dump`, `--enable`, `--no-vsync`, `--source`, `--output`, `--check-shaders`,
`--list-sources` or `--list-displays` is a usage error (exit 2).

The Windows SDK implementation reports model/display names, capture/playback
capabilities and supported video connections. These are device capabilities,
not evidence of a connected signal. Missing optional metadata produces a
warning so the remaining device information can still be reported.

| Outcome | Exit status |
| ------- | ----------- |
| Enumeration succeeds, including zero devices | 0 |
| Discovery unavailable in this build (macOS or SDK disabled) | 1 |
| COM, driver or enumeration failure | 1 |
| Incompatible rendering flags or other invalid usage | 2 |

The Windows SDK is optional and selected at build time; see
[BUILD.md](BUILD.md). Discovery neither starts capture/playback nor registers
hotplug monitoring. Its allocations, SDK calls and diagnostic logging occur
outside the per-frame path. Windows build and device validation remain
pending; the macOS stub does not validate the SDK implementation.

### `AppOptions`

```text
headless          no window, no UI
frames            0 = until the window closes; headless default becomes 300
dumpPath          binary PPM of the last processed frame
vsync             default true; --no-vsync clears it
enabledEffects    type ids; empty = built-in default chain
```

CLI flags are listed in [BUILD.md](BUILD.md).

---

## Initialise

`App::initialize`:

```text
createPlatformWindow()               skipped if headless
createGraphicsDevice()               CMake-selected Metal or D3D11
device->initialize(window, 1920, 1080)
updateEffectContext()                snapshot of device pointers + timing
TestPatternSource::initialize()
registerBuiltinEffects()
createDefaultChain()                 auto_frame on unless --enable
UiLayer::initialize()                skipped if headless
FrameTiming::reset()
```

Factories live in `src/gpu/Backend.h`. Exactly one backend is compiled in.

Window starts at 1600×900. Processing stays 1920×1080. A resize callback
updates only the swap chain / drawable.

---

## Run

**Headless:** loop `frames` times: `timing_.beginFrame(); renderFrame();`
then `reportTimings()` and optional `--dump`.

**Windowed:** `Window::runFrameLoop` pumps the platform and calls the same
pair. macOS pumps AppKit manually so Metal can block on `nextDrawable`.
`--frames N` destroys the window after N frames.

---

## One frame

`App::renderFrame()`:

```text
skip if the window is minimised
device->beginFrame()                 acquire drawable; false = skip
updateEffectContext()                time, delta, frame index, RHI pointers,
                                     tracking snapshot mapped to the canvas
optional shader reload               Stats panel flag, not every frame
serviceOutput()                      open/close/re-enumerate displays; Escape

device->beginProcessing()            GPU timer starts
    source = source_.render(ctx)     test_pattern → persistent "source.frame"
    frame = chain_.process(ctx, *)   advance all node loops, render enabled nodes
    lastOutput_ = frame              program; source texture is kept for SOURCE
device->endProcessing()              GPU timer ends (async)

if output surface:
    outputSurface_->present(frame)   own command buffer, ahead of the UI

if window:
    device->beginUi()
    UiLayer::draw(UiFrameState)      borrowed pointers, no ownership
    UiLayer::render()

device->endFrame(vsync)              present; vsync is forced off while an
                                     output surface is live — see below
```

There is no SDI output. The window's presented image is the swap chain with
ImGui on top; the display output, when open, is the chain result alone,
letterboxed onto a borderless full-screen window. While that output is live it
is the pacer, so `endFrame` receives `options_.vsync && !outputSurface_`:
waiting on two unsynchronised display vsyncs in series halves the frame rate. The Preview is split: SOURCE samples the pre-chain texture with tracking
and crop overlays; PROGRAM samples the chain output (UV-cropped to 9:16 when
Auto Frame is in portrait).

`readback` (half-float → RGBA8 → PPM) runs only after the headless loop, never
inside `renderFrame()`.

Inside `EffectChain::process()`, all parameter automations first advance once
by `ctx.deltaTime`, including those belonging to bypassed effects. The chain
then processes enabled effects through scratch targets 0/1.
`ShaderEffect::packConstants()` reads `Parameter::currentValue()`; reads do
not advance the clock or overwrite the manual parameter value. All readers
therefore see the same phase for that processing pass.

Loops use seconds, not frame counts. Their clocks advance only when the chain
is processed, independently of which node is selected or visible. A paused
loop holds its phase, a disabled loop uses the saved manual value, and an
effect bypass leaves its active loops running. No timer thread is added.

`updateEffectContext()` also copies the tracker's latest observation into
`EffectContext::tracking`, mapped from the captured image's coordinates onto
the canvas, and clears `framingActive`. Auto Frame writes the crop rectangle
back into the same struct when it runs, which is what the SOURCE overlay
reads. Detection itself runs on the tracker's own thread from the frames
capture already produced, so nothing in this loop waits for it and a tracker
that stalls, fails or does not exist costs no frame. See
[TRACKING.md](TRACKING.md).

---

## Default chain

`App::createDefaultChain()` always instantiates, in order:

```text
auto_frame    enabled
passthrough   disabled
rgb_split     disabled
pixelate      disabled
fm_raster     disabled
subpixel      disabled
shutter       disabled
mirror        disabled
crt           disabled
```

`--enable a,b,c` does not change which nodes exist. It changes which of those
nine start enabled. Unknown ids in the list simply leave that node off.

---

## UI layout

Fixed panels in `UiLayer::draw()` — not dockable, not saved:

```text
┌─ CamVJ  1920x1080  backend          fps  LIVE/IDLE ───────────────┐
├─ SOURCE (392×216) ─┬─ PREVIEW: SOURCE | PROGRAM (letterboxed) ─────┤
│  input / tracking  │  crop marks; subject tungsten, crop cyan      │
├─ OUTPUT (392×188)  ├───────────────────────────────────────────────┤
│  display / stop    │  STATS (rate, gpu ms, engine, frametime)      │
├─ EFFECTS ──────────┤                                               │
│  add/remove/↑↓     │                                               │
│  ParameterSet      │                                               │
└────────────────────┴───────────────────────────────────────────────┘
```

Left column 392 px. Header 48 px. Stats strip 200 px. Source panel 216 px.
Output panel 188 px. The remaining height is EFFECTS and PREVIEW.

In EFFECTS, each selected node exposes a Loop toggle per parameter and generic
settings for waveform, range, cycle duration and phase offset, with pause,
restart and a curve preview. These controls are generated from `ParameterSet`
and work independently for repeated instances of the same effect. Source
controls remain manual. The effect graph remains linear and loop settings are
not saved between sessions.

ImGui backends: `UiLayerMetal.mm` (OSX + Metal) or `UiLayerD3D11.cpp`
(Win32 + DX11). Win32 input is hooked before the window so ImGui sees it
first (`setWin32MessageHook`).

---

## Ownership

```text
App
 ├── unique_ptr<Window>              null when headless
 ├── unique_ptr<GraphicsDevice>
 ├── unique_ptr<Tracker>             null where the platform has no detector
 ├── TestPatternSource               value
 ├── EffectChain                     value
 ├── FrameTiming                     value
 └── UiLayer                         value, idle when headless
```

Borrowed for the duration of a frame only:

- `EffectContext` — shaders, fullscreen pass, target pool, time, tracking, framing
- `UiFrameState` — chain, source, timing, source and program textures, vsync flag

`lastOutput_` is a raw `GpuTexture*` into a pool-owned target. It is valid
until the next `process()` or shutdown.

---

## File map

```text
src/main.cpp
src/app/App.h App.cpp
src/core/Log.h Log.cpp
src/decklink/decklink_discovery.h
src/decklink/decklink_discovery_win.cpp decklink_discovery_stub.cpp
src/platform/Window.h Display.h OutputWindow.h
src/platform/mac/MacWindow.mm MacDisplay.mm
src/platform/win32/Win32Window.cpp Win32OutputWindow.cpp Win32MessageHook.h
src/gpu/Rhi.h Backend.h EffectConstants.h HalfFloat.h ShaderPaths.h/.cpp
src/gpu/d3d11/D3D11Device.h D3D11Backend.cpp
src/gpu/metal/MetalDevice.h MetalBackend.mm
src/video/FrameTiming.h/.cpp TestPatternSource.h/.cpp
src/video/VideoSource.h VideoDevices.h/.cpp CameraCapture.h CameraSource.h/.cpp
src/video/mac/CameraCaptureAVF.mm  src/video/win32/CameraCaptureMF.cpp
src/tracking/Tracker.h TrackingSnapshot.h framing.h/.cpp tracker_stub.cpp
src/tracking/mac/VisionTracker.mm
src/effects/Effect.h EffectParameters.h EffectRegistry.h/.cpp
src/effects/parameter_automation.h/.cpp
src/effects/EffectChain.h/.cpp ShaderEffect.h/.cpp BuiltinEffects.h/.cpp
src/effects/PassthroughEffect.cpp RgbSplitEffect.cpp
src/effects/PixelateEffect.cpp FmRasterEffect.cpp SubpixelEffect.cpp
src/effects/ShutterEffect.cpp CrtEffect.cpp MirrorEffect.cpp AutoFrameEffect.cpp
src/ui/UiLayer.h/.cpp Theme.h/.cpp Panels.h SourcePanel.cpp OutputPanel.cpp
src/ui/EffectsPanel.cpp PreviewPanel.cpp StatsPanel.cpp ParameterWidgets.cpp
src/ui/backend/UiLayerMetal.mm UiLayerD3D11.cpp

cmake/decklink.cmake
tests/parameter_automation_test.cpp tests/framing_test.cpp

shaders/hlsl/   common.hlsli fullscreen.hlsl test_pattern.hlsl
                passthrough.hlsl rgb_split.hlsl pixelate.hlsl fm_raster.hlsl
                subpixel.hlsl shutter.hlsl crt.hlsl mirror.hlsl auto_frame.hlsl
                source_blit.hlsl
shaders/metal/  common.metal test_pattern.metal
                passthrough.metal rgb_split.metal pixelate.metal fm_raster.metal
                subpixel.metal shutter.metal crt.metal mirror.metal
                auto_frame.metal source_blit.metal
```

A more detailed tree is in `memory-bank/file-map.md`.
