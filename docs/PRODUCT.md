# CamVJ — Product

**Status: V1 is the product goal. M0 is the working video pipeline, extended
since with camera inputs, subject tracking, display output and PROGRAM safety.
M1 has optional Windows discovery code awaiting Windows validation.**

---

## What it is

A real-time video effects engine that sits beside a Blackmagic ATEM switcher.
A camera is routed out of the ATEM through an AUX, processed on the GPU with
shader effects, and returned to a spare ATEM input as a second, treated version
of the same camera.

```text
CAMERA → ATEM AUX → DeckLink IN → CamVJ → DeckLink OUT → ATEM INPUT 8
```

The operator then cuts between `CAM 3` (clean) and `INPUT 8` (treated) like any
other source.

That round-trip is **not implemented**. DeckLink and the ATEM SDK are Windows
production work (M1 and M4). See [VIDEO_PIPELINE.md](VIDEO_PIPELINE.md) and
[ATEM_INTEGRATION.md](ATEM_INTEGRATION.md).

---

## What it is today (M0 and the extensions on top of it)

A GPU engine with two backends (Metal on macOS, Direct3D 11 on Windows) that:

- takes its picture from a GPU test pattern (colour bars, plasma, grid, and a
  static LED-mapping chart) or from any camera the operating system reports,
  both behind `VideoSource`;
- runs a linear chain of eleven built-in effects — `auto_frame`,
  `passthrough`, `rgb_split`, `pixelate`, `fm_raster`, `subpixel`, `shutter`,
  `frame_delay`, `mirror`, `vhs`, `crt` — each with an HLSL and an MSL shader,
  and with optional loop automation on every parameter;
- keeps a tracked subject framed (macOS Vision detector; Windows runs the same
  effect on manual controls);
- drives PROGRAM through four operator states — FX, Clean, Freeze, Black —
  with a latched Freeze on input loss ([RUNTIME.md](RUNTIME.md#program-safety));
- sends that PROGRAM picture to a display as a borderless full-screen signal,
  and on macOS into an installed camera extension so calls see it as a webcam
  ([VIRTUAL_CAMERA.md](VIRTUAL_CAMERA.md));
- previews source, chain and PROGRAM in Dear ImGui, or dumps a PPM in
  `--headless`;
- reports CPU and GPU frame time against a 16.68 ms budget.

**The show this is built for runs on macOS**, so the tracking and output path
above is production there. Windows remains the production platform for the
round-trip in the diagram: it is the only one with DeckLink and ATEM SDK
support, and its DeckLink code is written but not yet compiled.

What is still missing for V1: SDI capture and playback (M1), presets (M3) and
ATEM control (M4). There is no MIDI and no audio (M5).

---

## Primary target

```text
1920x1080
59.94 fps

Target:
0 dropped frames under normal operation

Processing budget:
< 16.68 ms per frame

Preferred GPU processing:
< 8 ms

Latency target:
as low as technically practical,
initial engineering goal <= 3 video frames
excluding ATEM processing.
```

The latency figure is an engineering goal. It is not a commercial promise.

M0 does not lock the loop to 59.94. The display (or the headless tight loop)
paces frames; the numbers above are the bar the timings are judged against.

---

## Explicitly out of scope for V1

AI, complex layer stacks, particles, NDI, streaming, recording, timeline
editing, eight simultaneous cameras, blend modes, LUTs, OSC, Stream Deck.

---

## V1 is done when

An operator can route a camera through CamVJ and back into the switcher, at
1080p59.94, apply and adjust effects live, recall presets, and press `FX TAKE`
— with stable Program/Preview reporting and no dropped frames.

That requires M1 (I/O), M3 (presets) and M4 (ATEM) on top of the M0 core.

The original conception document is preserved in [VISION.md](VISION.md). It is
not a map of the repository.
