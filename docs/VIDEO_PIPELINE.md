# ATEM FX — Video Pipeline

**M0 is implemented. M1 is in progress.** FX-010 discovery is implemented,
pending Windows build and hardware validation. The capture/processing/output
pipeline below remains design; see [ROADMAP.md](ROADMAP.md).

---

## Part A — M0 (implemented)

The rendering path has no SDI input/output, `VideoFrame` or `FrameQueue`.
Inputs are a GPU generator and the machine's own cameras, both behind
`VideoSource`, so the rest of the engine is real. Standalone DeckLink discovery
does not participate in this path.

```text
VideoSource                shader "test_pattern" or "source_blit"
  TestPatternSource        writes TargetPool::persistent("source.frame")
  CameraSource
        │
        ▼
   EffectChain             ping-pong scratch[0], scratch[1]
        │                  enabled effects only; false process() = skip
        ▼
   lastOutput_             GpuTexture* into the pool
        │
        ├── output:    OutputSurface → borderless window on a chosen display
        ├── windowed:  ImGui preview (nativeTexture) + swap chain present
        └── headless:  optional readback → binary PPM
```

### Formats

| Surface                         | Format                                      |
| ------------------------------- | ------------------------------------------- |
| Processing targets              | RGBA16Float / `R16G16B16A16_FLOAT`          |
| Presentation (swap chain)       | 8-bit UNORM / `BGRA8Unorm`                  |
| Display output (swap chain)     | 8-bit UNORM / `BGRA8Unorm`                  |
| `--dump`                        | 8-bit RGB PPM (P6), top row first           |

Processing resolution is fixed at 1920×1080 in `App::kProcessingWidth/Height`.
The window size does not change it.

### Video inputs

`enumerateVideoSources()` returns the test pattern first — an input list that
can be empty is an input list that needs special cases everywhere — then every
camera the operating system reports: the built-in webcam, USB and Thunderbolt
cameras, and on macOS 14+ an iPhone acting as a Continuity Camera. UVC
cameras that still advertise the pre-macOS 14 `ExternalUnknown` type are
included as well, so a Sony FX30 (or similar) in USB Streaming is not dropped
when `External` exists. Enumeration never opens a device, so it never
triggers a permission prompt; only selecting a camera does.

```
--list-sources     list inputs and exit
--source ID        start on that input
```

A camera is not the project format: webcams deliver 1280×720 or 1920×1080 at
whatever aspect they like. `source_blit` scales the frame into the 1920×1080
raster on the GPU with fit (letterbox), fill (crop) or stretch, plus mirroring
for a self-view camera and a vertical flip for capture stacks that deliver
bottom-up frames, as Media Foundation's RGB32 does. None of that touches the
CPU. On macOS the capture stack also picks 1920×1080 from the device format
list rather than trusting session presets: UVC cameras such as a Sony FX30 in
USB Streaming mode report 1080p30 (`420v`) and will not produce frames if the
session is left on a preset chosen before the input exists.

Camera connect and disconnect are observed on macOS
(`AVCaptureDeviceWasConnectedNotification` /
`AVCaptureDeviceWasDisconnectedNotification`). The frame loop polls a flag
between frames and re-runs enumeration, so a USB camera plugged in while the
app is running appears in SOURCE without a restart. That is not DeckLink
hotplug, which remains M1.

| Capture stack     | Platform | Pixel format | Notes                          |
| ----------------- | -------- | ------------ | ------------------------------ |
| AVFoundation      | macOS    | 32BGRA       | permission gated, asynchronous |
| Media Foundation  | Windows  | RGB32        | bottom-up, flipped on the GPU  |

**Threading.** Frames arrive on the platform's own thread and cross to the
render thread through a single-slot buffer under a mutex. Newest frame wins:
an older queued frame is dropped rather than shown late, the same policy the
bounded frame queue will use for SDI. The two sides swap buffers rather than
copy, so once the frame size settles neither allocates.

Opening a device never happens inside the measured region, and never blocks the
frame loop. On macOS it is not even synchronous — camera access is gated by the
operating system and the answer can arrive long after the request — so
`CameraCapture::poll()` gives that answer somewhere to land on the render
thread, and `-startRunning` runs on a background queue.

**macOS needs an application bundle.** The system denies camera access to a
binary with no `Info.plist` carrying `NSCameraUsageDescription`, so the macOS
build produces `atem_fx.app`. `bin/atem_fx` is a symlink into it.

### Display output

The processed frame goes to a borderless, full-screen window on a display the
operator picks. Downstream of the cable — an LED processor, a projector, a
switcher's HDMI input — it is a video signal like any other: no title bar, no
cursor, no user interface.

```
--list-displays    list displays and exit
--output ID        start sending to that display
```

Three pieces, split the way the input side is split:

| Piece | Where | What it knows |
| ----- | ----- | ------------- |
| `enumerateDisplays()` | `platform/Display.h` | what displays exist, in pixels |
| `OutputWindow` | `platform/OutputWindow.h` | how to put a borderless window on one |
| `OutputSurface` | `gpu/Rhi.h` | how to make a swap chain and present into it |

`OutputWindow` is deliberately not a `Window`: it owns no event loop. The main
window's loop pumps the process's events and both windows live inside it. The
surface always dies before the window, because it renders into the window's
layer.

The output pass is **not** an effect and does not go through `ShaderLibrary`.
The library is compiled for the processing format; a drawable is BGRA8. Each
backend owns a small self-contained shader for this, compiled from a string
rather than from `shaders/`, so that `--check-shaders` keeps meaning "every
effect compiles".

Scaling is fit (letterbox), the same arithmetic as `source_blit`, and it is not
configurable. Downstream is a fixed raster and a stretched picture is a fault
nobody further down the chain can correct.

**The output display becomes the clock.** While a surface is live the preview
window presents without waiting for its own display: two unsynchronised 60 Hz
vsyncs waited on in series produce 30 fps. `App::renderFrame` passes
`options_.vsync && !outputSurface_` to `endFrame`, and the OUTPUT panel says so
rather than leaving the vsync checkbox reading as a control that does nothing.

Measured on an M4 driving a 1920×1080 144 Hz panel, test pattern and one
effect, six runs of 600 frames: 112–116 fps at 8.6–8.9 ms per CPU frame, of
which GPU processing is under 1 ms. The rest is waiting for a drawable, which
is the point.

The Metal surface keeps three drawables in flight. Two was tried first, to save
a frame of latency, and measured 106–111 fps against 115–143 — slower, and
never locking to the panel's full rate. A wall shows jitter; it does not show
one frame of latency. `MetalOutputSurface::initialize` carries the numbers and
the warning that single runs put the two in the wrong order.

**The output does not depend on the preview.** `App::renderFrame` runs the
source, the chain and the output present even when the window has no drawable:
minimised, or — the case that actually bites — completely covered by the output
window, because the operator's own window was on the display they sent output
to. macOS stops vending drawables to a window nobody can see, and the first
version of this returned early there, taking the wall down with the preview.
It showed up as a run of 600 frames in 2.4 ms with nothing presented anywhere.

Escape closes the output. That exists because output can be sent to the display
holding the user interface, and a borderless window above the menu bar is not
something an operator can click their way out of. The key sets a flag; the
application closes the surface and the window between frames, like every other
device operation.

This is a *display* output, not SDI. It is not genlocked, the operating system
paces it, and it is not DeckLink playback. For a LED wall behind a processor
that is the normal way to work. Returning a clean signal to an ATEM input is
still what M1 owes.

Windows has the same three pieces (`Win32OutputWindow`, `D3D11OutputSurface`)
written against the same contract. **Neither has been compiled or run**; macOS
is the file that has been exercised. Treat them like FX-010: implemented,
pending a Windows build.

### Test pattern

`src/video/TestPatternSource.cpp`. Parameters:

| id        | Type | Range     | Default |
| --------- | ---- | --------- | ------- |
| `pattern` | int  | 0–2       | 0       |
| `speed`   | float| 0–4       | 1.0     |
| `markers` | bool |           | true    |

It is a *source*, not an effect. Downstream code (chain, preview, dump) cannot
tell. That is the M1 seam: replace this class with a capture-fed texture of
the same shape.

### Clock

M0 has no video clock. The UI thread, the display output when one is open, or
the headless `for` loop decides when a frame happens. A display is not a video
clock: it is a pacer the operating system owns and the engine cannot steer. `FrameTiming` records wall-clock deltas; GPU time covers
`beginProcessing`…`endProcessing` only.

59.94 fps / 16.68 ms is the **budget**, not a genlock. See
[ARCHITECTURE.md](ARCHITECTURE.md) §8.

### Readback

`GraphicsDevice::readback` converts the half-float target to 8-bit RGBA and
stalls the GPU. `App::dumpLastFrame` strips alpha and writes PPM. Never call
this from the live frame loop.

---

## Part B — M1 discovery (FX-010)

`--list-decklink` enumerates and logs device model/display names, capture and
playback capabilities, and supported video connections. It exits before
`App`, GPU and UI initialization. It reports capabilities, not whether a
video cable or valid signal is present.

The implementation is isolated in `src/decklink/`: a portable declaration,
an optional Windows SDK implementation and a stub for builds without it.
Enable the Windows SDK with `ATEMFX_ENABLE_DECKLINK=ON` and point
`ATEMFX_DECKLINK_SDK_DIR` at an external SDK; see [BUILD.md](BUILD.md).

Successful enumeration with no devices returns 0. Unavailable discovery
(including macOS/default builds), COM initialization failure, missing driver
or enumeration failure returns 1. Partial device metadata produces warnings.
Combining discovery with rendering flags or the separate `--list-sources`
command is invalid usage (exit 2).

Discovery is implemented but awaits Windows build and device validation.
It does not implement capture, playback, hotplug monitoring, frame queues or
new processing threads. FX-010 is not yet marked done.

FX-011 has a portable seam in place: `src/decklink/decklink_capture.h` declares
capture-only enumeration and a callback interface, `decklink_capture_stub.cpp`
keeps non-SDK builds empty, and `DeckLinkSource` can already occupy the
`VideoSource` slot without changing the effect chain. The Windows SDK-backed
capture implementation is still missing, so no SDI source appears until that
file is added and validated on hardware.

---

## Part C — M1 video pipeline (design — not implemented)

The rest of this section is the contract for the remaining M1 work.

### Goal

```text
SDI IN → DeckLink capture → GPU texture → EffectChain → GPU texture
       → DeckLink playback → SDI OUT
```

at 1920×1080 59.94, zero dropped frames under normal operation, processing
on a thread that is not the UI.

Windows only. The DeckLink SDK has no macOS equivalent in this project.

### Target graph

```text
DeckLink Capture Thread          owns the input callback; must not block
        │
        ▼
   Frame Queue                   bounded, lock-free, drops oldest
        │
        ▼
GPU Processing Thread            owns the D3D11 immediate context + chain
        │                        App::renderFrame() processing half
        ▼
   Output Queue                  bounded, lock-free
        │
        ▼
DeckLink Playback Thread         scheduled output; must not block
```

UI, and later ATEM/MIDI/audio, stay off this path. They publish a parameter
snapshot. They never wait on Capture, Process or Output.

### Seams already in M0

These exist so M1 does not rewrite the engine:

| Seam | Why it is the split point |
| ---- | ------------------------- |
| `TestPatternSource` | Swap for a capture-backed source that still returns a `GpuTexture*` at project resolution. |
| `EffectContext` | Per-frame snapshot. Cross the thread boundary by value, not by sharing `App`. |
| `App::renderFrame()` processing block (`beginProcessing` … `endProcessing`) | This is the work that moves to the GPU thread. Presentation/UI stays on the window thread. |
| `TargetPool` + RGBA16Float | Headroom for 10-bit YUV upload; persistent keys for history. |
| Headless path | Keep it. Hardware tests are extra, not a replacement. |

`DeckLinkSource` intentionally mirrors `CameraSource` only as far as the current
M0 seam allows: newest-frame handoff, CPU BGRA upload through `source_blit`, and
identity source mapping for a 1920x1080 SDI frame. The single-slot handoff is a
temporary bridge, not FX-013's production queue.

### Queues (FX-013)

Bounded. Lock-free (or equivalent wait-free for the producer). On overflow
**drop oldest**, never block the capture callback. Count drops and log them
outside the hot path.

Do not put `std::mutex` on the capture or output callback.

### Timing (FX-014)

The DeckLink hardware clock becomes the authority. Processing must keep up
with that clock, not with the display. Vsync on the preview window must not
stall output.

Latency goal remains ≤ 3 video frames excluding ATEM processing. Still an
engineering target, not a commercial promise.

### Capture / playback (FX-011, FX-012)

Discovery, capture and playback are separate issues. FX-010 only discovers
capabilities; it does not prove that a device can run the target video mode.
Capture does not imply playback. Do not fold them into one class that "does
DeckLink".

Diagnostic logging is mandatory on every hardware path (`AGENTS.md`).

### What M1 does not include

ATEM control, MIDI, audio, presets, effect graph, Linux, a third GPU backend.
The UI can stay as it is: preview of the processed texture plus stats.
