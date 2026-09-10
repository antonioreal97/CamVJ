# CamVJ — Runtime

**Status: M0 implemented; M1 discovery added, pending Windows validation.**
This is the call sequence and ownership of the running process. Architecture
principles live in [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Entry

`src/main.cpp`:

1. On Windows, set per-monitor DPI awareness so the preview is not scaled
   twice.
2. Parse CLI, keeping rendering options in `AppOptions` (`src/app/App.h`).
   `--help` and `--version` are answered inside the parse loop and exit 0
   before any other argument is read, so neither can be refused for the
   company it keeps. `--version` prints `CamVJ <version> (<backend>)` from
   `atemfx::kVersion` (`src/core/Version.h`, stamped by CMake) and
   `backendName()`, both compile-time constants: it opens no device.
3. If `--list-decklink` was requested, enumerate and log devices, then exit.
   This path creates no `App`, window, GPU device or UI.
4. Otherwise: `App app; app.initialize(options); app.run(); app.shutdown();`

Unknown flags → usage, exit 2. Init failure → exit 1.

### Standalone DeckLink discovery (FX-010)

`--list-decklink` is exclusive: combining it with `--headless`, `--frames`,
`--dump`, `--enable`, `--no-vsync`, `--source`, `--pattern`, `--output`, `--program`,
`--check-shaders`, `--list-sources` or `--list-displays` is a usage error
(exit 2).

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
sourceId          video input id; empty = test pattern
testPattern       initial internal pattern; -1 = source default; --pattern selects it
outputDisplayId   display id; empty = no output window
webcam            send PROGRAM to call applications as a camera (macOS)
programMode       PROGRAM state at startup: fx, clean, freeze or black
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
serviceOutput()                      open/close/re-enumerate displays; OS changes and Escape
serviceWebcam()                      start/stop/failure even without preview
device->beginFrame()                 acquire preview drawable unless minimised;
                                     Metal also skips fully occluded windows
skip processing only if no preview, display output or webcam client
updateEffectContext()                time, delta, frame index, RHI pointers,
                                     tracking snapshot mapped to the canvas
optional shader reload               Stats panel flag, not every frame

device->beginProcessing()            GPU timer starts
    source = source_.render(ctx)     test_pattern → persistent "source.frame"
    health = source_.health()        Live / Stale / Waiting / Generated
    mix = programTransition_.update() FX/Clean dissolve, read before the latch
    frame = chain_.process(ctx, *)   advance all node loops, render enabled
                                     nodes; Framing roles at every mix amount
    frame = programOutput_.render()  FX / Clean / Freeze / Black, the dissolve
                                     between whole images, and the Freeze latch
                                     when the input is not healthy
    lastOutput_ = frame              program; source texture is kept for SOURCE
    webcam_->submit(frame)           optional; one pass into a CVPixelBuffer,
                                     handed to the camera extension when the
                                     GPU says so. See VIRTUAL_CAMERA.md
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

Output servicing precedes preview acquisition: a hidden operator window cannot
prevent display-loss detection, Escape, webcam startup or webcam cleanup.
If preview acquisition fails, an existing display output or webcam client
still runs capture, the chain and PROGRAM. Metal checks the operator window's
[occlusion state](https://developer.apple.com/documentation/appkit/nswindow/occlusionstate-swift.property)
before asking for a drawable, avoiding an acquisition timeout for a covered
window. When neither display paces the windowed loop, `App::run()` yields until
16.683 ms from the tick's start, outside processing; this prevents a hidden
webcam or idle window from flooding the GPU or busy-spinning. Headless remains
unpaced. This fallback is a software cadence, not a hardware video clock.

`consumeDisplayChanges()` consumes a platform notification flag without
enumerating on every frame. `serviceOutput()` reconciles both the active and
pending route by display ID through `platform/display_routing.*`, so an index
from yesterday's list never selects a different screen. Loss of the selected
display, or a change to its raster, refresh rate or desktop geometry, closes
the surface before its window and cancels pending routing. The diagnostic
persists and reconnection only refreshes the list: sending requires an explicit
selection. PROGRAM mode and any webcam feed remain independent of that route.
An unrelated display change preserves an unchanged active route. These safety
checks run under Operation lock as well.

There is no SDI output. The window's presented image is the swap chain with
ImGui on top; the display output, when open, is the chain result alone,
letterboxed onto a borderless full-screen window. While that output is live it
is the pacer, so `endFrame` receives `options_.vsync && !outputSurface_`:
waiting on two unsynchronised display vsyncs in series halves the frame rate. The Preview is split into two monitors. The right one is PROGRAM: what the
output surface receives, UV-cropped to 9:16 when Auto Frame is in portrait.
The left one is a two-position bus:

| Bus      | Shows                                                          |
| -------- | -------------------------------------------------------------- |
| `SOURCE` | the pre-chain texture, with tracking and crop overlays and the subject picker |
| `FX`     | `UiFrameState::chainPreview` - the chain's own image, with the same 9:16 crop and no overlays |

`chainPreview` is the chain result captured before `ProgramOutput::render()`,
along with the framing that produced it, because that call may hold the picture
(`Freeze`), replace it (`Black`) and restore the *held* framing over the live
one. It costs no extra GPU work: it is the image the chain already produced
this frame. That is what makes the FX bus a preview bus - under `Freeze` or
`Black` the chain is still working on the next shot, and this is the only place
that picture can be seen. Under `Clean` the look is ramped out of the chain
itself, so the FX bus honestly shows no look and the monitor labels the mix.

While a framing node's parameters are open, both monitors carry a thirds grid
and a centre cross for composing the shot - inside the framing rectangle on
SOURCE, over the picture on FX and PROGRAM. It is drawn on the interface's own
draw list on top of an `ImGui::Image`, so the texture reaching
`OutputSurface::present()` and the webcam never carries it. See
[TRACKING.md](TRACKING.md).

A live subject pick owns the left monitor and forces it to `SOURCE`: an
operator told to click SOURCE has to be looking at it. It is a live pick when
the operator pressed Pick subject, or when there are candidates in shot and
none is locked yet.

`readback` (half-float → RGBA8 → PPM) runs only after the headless loop, never
inside `renderFrame()`.

Test Pattern's **LED Mapping (16:9 + 9:16)** selection is a static GPU
calibration chart. It sets `VideoSource::bypassEffects()` for that selection
only (all other inputs default to false). App passes that policy into
`EffectChain::process()`, which returns the original source after advancing
the node clocks, before any effect or dissolve work. This preserves the full
canvas and exact guide dimensions even with portrait framing or a visual look
enabled; it does not modify effect settings. The frame's reset framing metadata
keeps the live previews on the full canvas. `ProgramOutput` still applies
Freeze/Black and restores held framing as usual. Speed and Motion Markers do
not alter the calibration chart. Selecting another pattern or a camera resumes
the configured chain.

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

## PROGRAM safety

`ProgramOutput` (`src/video/program_output.h`) sits between the chain and the
output surface. It is an output policy, not an effect: black and hold must also
work when the source has disappeared and there is no chain input at all, so it
owns its own persistent targets rather than borrowing the chain's scratch.

Four modes, all reachable from one control in the PROGRAM panel — the top of
the left column, above SOURCE, and the only panel with no fold, because
recovery cannot depend on a disclosure triangle:

| Mode     | PROGRAM shows                    | Chain           |
| -------- | -------------------------------- | --------------- |
| `FX`     | the full chain                   | all enabled     |
| `Clean`  | the shot without visual effects  | `EffectRole::Framing` only |
| `Freeze` | the last good PROGRAM frame      | keeps running   |
| `Black`  | opaque black                     | keeps running   |

Every one of them is reached by a dissolve, never a cut — see *Transitions*
below.

`Clean` is not a bypass of everything. `EffectChain::process(ctx, in, mix)` at
`mix == 0` skips `EffectRole::Visual` and keeps `EffectRole::Framing`, so Auto
Frame still crops and the 9:16 output window is still there: the operator
removes the look without losing the shot or changing what the LED panel is fed.
Every node's automation clock still advances in `Clean`, `Freeze` and `Black` —
the chain is being kept warm behind a safe picture, not paused.

### Transitions

No button cuts. All four ramp over `Dissolve::kDefaultSeconds` — 0.35 s, eased
at both ends — because an unannounced cut is what a fault looks like from the
audience. `Dissolve` (`src/video/program_output.h`) is the shared ramp: it
clamps rather than overshooting when a frame arrives late, eases because the
ends are where the eye is looking, and reverses by keeping the picture on air
and only turning the direction of travel round.

The four buttons are two different transitions, because they are two different
kinds of change:

**FX ↔ Clean changes the picture, so it mixes inside the chain.**
`ProgramTransition` ramps and the chain is handed the result as `effectMix`.
Between the ends every enabled `EffectRole::Visual` node renders into
`chain.wet` and the `crossfade` shader lays it back over the picture that node
received, so the look dissolves out instead of being cut out.
`EffectRole::Framing` is never mixed: the shot must not drift or half-crop
while the look fades.

**Freeze and Black replace the picture, so they mix at the output.**
`ProgramOutput` dissolves between whole images, and the sources are live rather
than snapshots: leaving FX for Freeze, the chain goes on producing frames into
`program.live.*` and the audience watches a moving picture fade into a still
of the moment the button was pressed. Effects and Clean are one source here,
not two — dissolving them at both levels would fade the same change twice.

Rules that fall out of it:

- **The ends are free.** Settled FX, Freeze and Black each cost no mix pass at
  all, at either level. The extra work — one pass per visual node in the chain,
  one for the output — is paid only while a transition is running.
- **The latched still stops moving exactly while a dissolve reads it**, which
  is the same invariant as everywhere else here: never write the texture the
  current pass samples. Any other time it tracks the chain, so a camera that
  dies on the way back from Black freezes on the picture that was just live
  rather than on one from before the fade.
- **Re-aiming mid-dissolve starts from what is on air.** Straight back the way
  it came reverses the ramp; anywhere else copies the composite to
  `program.composite` and dissolves from that, because starting from either
  original source would snap the picture back to one the operator has left.
- **Freeze and Black hold the chain mix.** The look is not what the audience is
  watching, so `ProgramTransition::update` only advances in `FX` and `Clean`
  and an operator who cut away mid-mix comes back to the picture they left.
- **Safety cuts.** The input-loss latch is a fault, not a gesture: the last
  good picture has to be on the wall that frame, not in 0.35 s. So
  `ProgramOutput` snaps when it latches `Freeze` itself, and dissolves only
  when an operator changes the mode.
- **A missing `crossfade` shader costs the transition, never the frame.** Both
  levels resolve their mix resources before doing any work and fall back to a
  cut — which is what the buttons did before.

Start-up snaps: `--program clean` is already Clean on frame one, because there
is no previous picture to dissolve from.

`Freeze` and `Black` do not stop the pipeline. Capture, tracking and the chain
keep preparing the next shot while a stable image is on the wall, and the
output surface keeps presenting every frame: the LED panel never sees a dead
signal, only a still one.

### Input loss

An input is healthy when it is `Live` or `Generated` and the device has not
disappeared (`SourceHealth`, `src/video/source_health.h`; `Stale` is 0.5 s
without a new frame). When a live mode meets an unhealthy input and a valid
image exists, `ProgramOutput` latches `Freeze` and reports the mode change
back to `App`.

Three rules follow from that latch:

- **Loss never selects an input.** A camera that disappears leaves PROGRAM
  holding its own last frame. The test pattern is a deliberate operator
  choice, never a fallback — nothing may route it to the wall on its own.
- **A returning camera does not take itself live.** The latch is cleared only
  by an explicit mode change, so a device that reconnects mid-song cannot push
  a cold, unframed shot to PROGRAM. The status line says what to do: select
  the input, then `FX` or `Clean`.
- **Loss cannot override the operator.** An explicit `Black` stays `Black`
  when the input dies.

Before the first valid frame there is nothing to hold, so every mode shows
opaque black — `Freeze` never invents an image it does not have.

The held image also carries its framing: `context.framing`, `framingActive`
and `outputAspect` are restored from the frozen frame, so the PROGRAM preview
and its crop overlay describe the picture actually on the wall while live
tracking continues behind it.

`--program fx|clean|freeze|black` starts on a deliberate state, which is how a
show opens on black before anything goes live.

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
vhs           disabled
crt           disabled
```

`--enable a,b,c` does not change which nodes exist. It changes which of those
ten start enabled. Unknown ids in the list simply leave that node off.

---

## UI layout

Fixed panels in `UiLayer::draw()` — not dockable, not saved. The left column
is 392 px, or a 40 px rail of stacked titles when the operator collapses it
(chevron next to the brand in the header). The two preview monitors split
whatever width remains, so collapsing the column is how SOURCE and PROGRAM
grow. Clicking a rail title reopens the column and opens that section;
PROGRAM has no fold, so its title only expands. FX / Clean / Freeze / Black
are then one more click away. Session-only, like the section folds.

```text
┌─ [«] CamVJ  1920x1080  backend     PROGRAM FX  fps  SEND ─────┐
├─ PROGRAM (FX/Clean/Freeze/Black) ─┬─ PREVIEW: [SOURCE|FX] | PROGRAM ─┤
├─ SOURCE (input / tracking)        │  crop marks; subject tungsten     │
├─ OUTPUT (display / webcam)        ├───────────────────────────────────┤
├─ EFFECTS (add / remove / ↑↓)      │  INSPECTOR: stats, or the         │
└───────────────────────────────────┴── selected effect's parameters ───┘
```

Header 48 px. PROGRAM is always its caption plus the mode row — what goes to
air stays one reach away while the column is open. SOURCE is sized from its
own content but capped so EFFECTS always keeps 180 px - its rack label, the
Add effect button and a few chain rows. That reserve was 240 while EFFECTS
also carried the PARAMETERS section; the section moved to the inspector, so
the space went back to SOURCE, which is what pays for the rules between its
parameters. The remaining height is EFFECTS and PREVIEW.

### The inspector

The wide panel under the preview has two faces, and `ui::inspectedEffect()`
(`src/ui/Inspector.h`) decides which:

- **nothing inspected** - the stats strip, 204 px: input health, render rate,
  gpu milliseconds, output state and the frame-time plot. This is the default;
  it is what the panel shows when the operator is not working on an effect.
- **an effect inspected** - that effect's `ParameterSet`, dealt across up to
  four columns, sized from the tallest column and capped at 45% of the body so
  the preview it is being judged on stays legible.

Selecting an effect and inspecting it are one act: clicking a row in EFFECTS
opens it here, clicking it again closes it, and so does the panel's own close
control. Folding EFFECTS away, or opening SOURCE or OUTPUT, closes it too -
the sidebar section the operator moves to is the one that owns the screen.
`UiLayer::syncInspectorToSections()` compares the fold state against the
previous frame's to see those edges; `ui::validateInspector()` runs first,
before the layout pass reads the pointer, because the chain can lose an effect
between frames.

Parameters left the sidebar because a 392 px column had to carry the chain list
and thirteen sliders at once. They are the same widgets: `drawParameters()` and
`drawParametersColumns()` share one per-parameter renderer, so a source
parameter in the sidebar and an effect parameter in the inspector read
identically.

### One parameter

`drawParameterRow()` lays each one out as a rack strip, two lines high:

```text
Subject Size            0.850   ~     <- name, value control, loop toggle
[----------|-------------##---]       <- track, with a tick at the default
```

- The **value is a `DragFloat`/`DragInt` in the mono face**, not text and not
  the number ImGui centres inside a slider. A 250 unit track spends one pixel
  on 1/250th of the range, which is not a calibration control; this one is
  dragged for fine steps and Ctrl-clicked to type an exact value. Decimals
  come from the range - `%.1f` above a span of 20, `%.3f` below 2 - so
  `Hold (s)` reads `2.00` instead of `2.000`.
- The **track carries position and nothing else**, plus `drawDefaultTick()`:
  a hairline where the shipped default sits, so "how far have I strayed" and
  the right-click that snaps back both have a visible target. It is drawn with
  `ImGuiSliderFlags_NoInput`, because a Ctrl-click on a slider with an empty
  format string opens an edit box with nothing in it.
- A **boolean is one line**: its checkbox sits in the value slot, aligned with
  every number above and below it.
- A **choice** (`Parameter::makeChoice`) puts a combo on the second line and
  leaves the value slot to the loop toggle - the combo already says what the
  value is.
- **Loop is a glyph button** carrying one cycle of a sine, filled cyan while
  it runs. It used to be a checkbox labelled "Loop" pushed to the far right of
  a stretched label column, which read as unrelated to the parameter it
  belonged to. While a loop runs, both controls show what the loop is doing
  rather than the manual value underneath it.
- A **rule sits between parameters**, never above the first one in a column.
  Two lines of controls with nothing between them let a track read as
  belonging to the name below it rather than the name above; the rule closes
  each parameter off so the pair that moves together is visibly one thing. A
  loop panel lives inside its parameter's pair of rules rather than adding one
  of its own. `parameterBlockHeight()` in `UiLayer.cpp` and
  `separatorHeight()` in `InspectorPanel.cpp` both count them - the sizing
  passes run before the panels draw, and a rule nobody counted is a slider
  clipped off the bottom.
- The **tooltip carries what the row cannot**: id, range, default, and the two
  gestures nothing advertises - Ctrl-click to type, right-click to reset. Each parameter exposes a Loop toggle and generic settings for
waveform, range, cycle duration and phase offset, with pause, restart and a
curve preview, generated from `ParameterSet` and independent for repeated
instances of the same effect. Source controls remain manual. The effect graph
remains linear and loop settings are not saved between sessions.

ImGui backends: `UiLayerMetal.mm` (OSX + Metal) or `UiLayerD3D11.cpp`
(Win32 + DX11). Win32 input is hooked before the window so ImGui sees it
first (`setWin32MessageHook`).

---

## Ownership

```text
App
 ├── unique_ptr<Window>              null when headless
 ├── unique_ptr<GraphicsDevice>
 ├── unique_ptr<VideoSource>         TestPatternSource or CameraSource
 ├── unique_ptr<OutputWindow>        null until a display is picked
 ├── unique_ptr<OutputSurface>       lives and dies with that window, released first
 ├── unique_ptr<VirtualCameraOutput> null until PROGRAM is sent to a webcam
 ├── unique_ptr<Tracker>             null where the platform has no detector
 ├── EffectChain                     value
 ├── ProgramOutput                   value, plus ProgramTransition
 ├── SourceHealth                    value
 ├── FrameTiming                     value
 └── UiLayer                         value, idle when headless
```

The output surface renders into the output window's layer, so it is destroyed
before the window. The virtual camera worker owns camera transport, so a
stopping output stays alive until it has released the device.

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
src/core/Log.h Log.cpp Version.h
src/decklink/decklink_discovery.h decklink_capture.h
src/decklink/decklink_discovery_win.cpp decklink_discovery_stub.cpp
src/decklink/decklink_capture_stub.cpp
src/platform/Window.h Display.h OutputWindow.h
src/platform/display_routing.h/.cpp
src/platform/mac/MacWindow.mm MacDisplay.mm
src/platform/win32/Win32Window.cpp Win32OutputWindow.cpp Win32MessageHook.h
src/gpu/Rhi.h Backend.h EffectConstants.h HalfFloat.h ShaderPaths.h/.cpp
src/gpu/d3d11/D3D11Device.h D3D11Backend.cpp
src/gpu/metal/MetalDevice.h MetalBackend.mm
src/video/FrameTiming.h/.cpp TestPatternSource.h/.cpp
src/video/VideoSource.h VideoDevices.h/.cpp CameraCapture.h CameraSource.h/.cpp
src/video/DeckLinkSource.h/.cpp
src/video/source_health.h/.cpp program_output.h/.cpp
src/video/virtual_camera.h virtual_camera_stub.cpp
src/video/mac/CameraCaptureAVF.mm virtual_camera_mac.mm
src/video/win32/CameraCaptureMF.cpp
src/tracking/Tracker.h TrackingSnapshot.h framing.h/.cpp tracker_stub.cpp
src/tracking/source_mapping.h/.cpp
src/tracking/mac/VisionTracker.mm
src/effects/Effect.h EffectParameters.h EffectRegistry.h/.cpp
src/effects/parameter_automation.h/.cpp
src/effects/EffectChain.h/.cpp ShaderEffect.h/.cpp BuiltinEffects.h/.cpp
src/effects/PassthroughEffect.cpp RgbSplitEffect.cpp
src/effects/PixelateEffect.cpp FmRasterEffect.cpp SubpixelEffect.cpp
src/effects/ShutterEffect.cpp FrameDelayEffect.cpp VhsEffect.cpp
src/effects/CrtEffect.cpp MirrorEffect.cpp AutoFrameEffect.cpp
src/ui/UiLayer.h/.cpp Theme.h/.cpp Fonts.h/.cpp Panels.h
src/ui/SourcePanel.cpp OutputPanel.cpp
src/ui/EffectsPanel.cpp PreviewPanel.cpp StatsPanel.cpp ParameterWidgets.cpp
src/ui/Inspector.h InspectorPanel.cpp ProgramPanel.cpp
src/ui/backend/UiLayerMetal.mm UiLayerD3D11.cpp

cmake/decklink.cmake
tests/parameter_automation_test.cpp tests/framing_test.cpp
tests/source_mapping_test.cpp tests/source_health_test.cpp
tests/program_output_test.cpp
tests/display_routing_test.cpp tests/app_output_lifecycle_test.cpp
tests/display_changes_mac_test.mm

shaders/hlsl/   common.hlsli fullscreen.hlsl test_pattern.hlsl
                passthrough.hlsl rgb_split.hlsl pixelate.hlsl fm_raster.hlsl
                subpixel.hlsl shutter.hlsl frame_delay.hlsl vhs.hlsl crt.hlsl
                mirror.hlsl auto_frame.hlsl crossfade.hlsl source_blit.hlsl
shaders/metal/  common.metal test_pattern.metal
                passthrough.metal rgb_split.metal pixelate.metal fm_raster.metal
                subpixel.metal shutter.metal frame_delay.metal vhs.metal
                crt.metal mirror.metal auto_frame.metal crossfade.metal
                source_blit.metal
```

A more detailed tree is in `memory-bank/file-map.md`.
