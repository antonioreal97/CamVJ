# ATEM FX — Roadmap

**59.94 fps is the engineering target everywhere.** Older notes said "60 fps"
for M0 acceptance; the budget in code and in `App::reportTimings()` is
16.68 ms (59.94). The engine does not lock the frame rate.

M1 is the real test of the project. It is worth more than half the application.
M1 onwards is Windows-only work: DeckLink and the ATEM SDK have no macOS
equivalent. macOS remains the development and demonstration target.

---

## Milestones

| Milestone | Result                                         | Status |
| --------- | ---------------------------------------------- | ------ |
| M0        | GPU engine running with a test source          | **done** |
| M1        | DeckLink IN → GPU → DeckLink OUT               | **in progress** |
| M2        | Effect *graph* (DAG) and a feedback effect     |        |
| M3        | Presets (JSON) on top of the existing UI       |        |
| M4        | ATEM integration                               |        |
| M5        | MIDI + audio reactive                          |        |
| M6        | Fill/Key and advanced features                 |        |

### What M0 actually delivered

- Application skeleton, window, frame loop, headless self-test.
- Dual GPU backends behind `src/gpu/Rhi.h`: **Metal (macOS)** and
  **Direct3D 11 (Windows)**.
- GPU test pattern source at 1920×1080 (not MP4).
- Linear `EffectChain` + `EffectRegistry` + generic `ParameterSet` UI.
- Effects: Passthrough, RGB Split, Pixelate, FM Raster, Subpixel, Shutter,
  CRT, Mirror and Auto Frame — each with HLSL and MSL.
- ImGui panels: Source, Effects (add/remove/reorder), Preview, Stats.
- CPU and GPU timing. Measured on macOS, Apple M4, 1920×1080: 3000 frames at
  134 fps with vsync, 0.7 ms of GPU processing against a 16.68 ms budget.

M0 acceptance in one sentence: the application renders a test source
continuously at 1920×1080 and applies RGB Split and Pixelate in real time on
the GPU, with FPS and frame-time measurement visible (or printed in
`--headless`).

### M1 progress and next validation

FX-010 adds standalone `--list-decklink` discovery with an optional Windows
DeckLink SDK build and an unavailable stub elsewhere. It reports device
names, capture/playback capabilities and supported video connections, then
exits before rendering initialization. It does not detect a live signal or
start capture, playback or hotplug monitoring.

The implementation is **pending Windows build and hardware validation**;
FX-010 is not done. Verify the SDK build, driver failure diagnostics, zero
devices and the metadata reported by a real device. The macOS headless gate
continues to protect the M0 rendering path; it cannot validate DeckLink.

The remaining M1 work is FX-011 capture, FX-012 playback, FX-013 frame queues
and FX-014 video timing, including separation of processing from the UI.

### Extension to the current effect system

Requested on 2026-09-08: optional looping automation for each parameter of
each existing effect node. The extension adds Sine, Triangle, Ramp Up,
Ramp Down and Square, value bounds, cycle duration, phase, pause and restart
through the generic parameter UI. Each node owns its loops independently;
they continue through effect bypass and reordering. Settings last for the
session only.

Build, CTest and the macOS headless gate passed on 2026-09-09; manual UI
inspection is still pending. This bounded extension does not open the M2 graph,
M3 presets or M5 MIDI/audio work, and does not change the M1 DeckLink validation
requirements. Scalar automation has a standalone C++ test registered through
CTest; the macOS headless gate remains required.

### Subject tracking and auto framing

Requested on 2026-09-08, after parameter loops: keep a person or object framed
while they move, so a camera on a stage can feed a LED wall without an
operator riding the shot. Design and behaviour in [TRACKING.md](TRACKING.md).

Delivered as three parts, only one of which is an effect: a `Tracker`
interface with a Vision implementation on macOS, a portable `FramingController`
with its own CTest, and an `auto_frame` effect that crops on the GPU. Tracking
reads the frames capture already produced in system memory, on its own thread,
so it adds no GPU work and no readback and cannot cost a video frame.

**Windows has no detector** (FX-022). The effect and the controller are
portable and run there on manual controls; the sensor is missing and choosing
its replacement is a dependency decision, not an implementation task. It is
deferred, not blocking: the show this was requested for runs on macOS, so
Vision is the production detector here.

Live camera validation is the open item, and it is the important one — it
needs camera permission, which the headless gate cannot grant.

The output half of that gap is now closed for a display: see **Display output**
below. Returning a clean signal to an ATEM input is not, and is still M1.

Like parameter loops, this is a bounded extension of the existing linear chain.
It does not open the M2 graph, M3 presets or M5 modulation work, and it does
not change what M1 owes.

### Display output

Requested on 2026-09-08, after subject tracking: send the processed frame to a
display, so the engine can feed a LED processor the way a Resolume machine
does — one signal out, mapped on the other side.

Delivered as three pieces along the existing platform/RHI seam:
`enumerateDisplays()` and `OutputWindow` in `src/platform/`, `OutputSurface`
created by the graphics device. An OUTPUT panel under SOURCE, plus
`--list-displays` and `--output ID`. Behaviour and measurements in
[VIDEO_PIPELINE.md](VIDEO_PIPELINE.md).

Validated on macOS against a 1920×1080 144 Hz panel: 112–116 fps at 8.6–8.9 ms
per CPU frame, under 1 ms of it GPU work. The Windows pieces are written
against the same contract and have not been compiled — the same state FX-010
is in.

This is a display output. It is not genlocked, the operating system paces it,
and it does not make M1 smaller: SDI in, SDI out and the frame queues are
untouched, and a clean return to an ATEM input still needs DeckLink playback.

### What M2 is *not*

M0 already has a linear chain, a registry, generic parameters, and
`TargetPool::persistent()`. M2 is the **graph** (branching / DAG, FX-007) and
a real **feedback effect** (FX-008). Do not start M2 work because the
infrastructure looks ready.

### What M3 is *not*

The UI already renders any effect from `ParameterSet`. M3 is **presets**
(save/load JSON, recall buttons). FX-009.

---

## Backlog

| Issue  | Function              | Milestone | Status |
| ------ | --------------------- | --------- | ------ |
| FX-001 | App skeleton          | M0        | done   |
| FX-002 | GPU backends (D3D11 + Metal) | M0 | done   |
| FX-003 | Video texture         | M0        | done   |
| FX-004 | Effect interface      | M0        | done   |
| FX-005 | RGB Split             | M0        | done   |
| FX-006 | Pixelate              | M0        | done   |
| FX-007 | Effect Graph          | M2        |        |
| FX-008 | Feedback buffer / effect | M2     |        |
| FX-009 | Presets               | M3        |        |
| FX-010 | DeckLink discovery    | M1        | implemented; Windows build/device validation pending |
| FX-011 | DeckLink capture      | M1        |        |
| FX-012 | DeckLink playback     | M1        |        |
| FX-013 | Frame queues          | M1        |        |
| FX-014 | Video timing          | M1        |        |
| FX-015 | ATEM discovery        | M4        |        |
| FX-016 | Program/Preview state | M4        |        |
| FX-017 | AUX control           | M4        |        |
| FX-018 | FX Bus                | M4        |        |
| FX-019 | MIDI                  | M5        |        |
| FX-020 | Audio analysis        | M5        |        |
| FX-021 | Camera inputs         | M0        | done   |
| FX-022 | Subject tracking      | —         | macOS implemented, pending live camera validation; Windows detector deferred (show runs on macOS) |
| FX-023 | Display output        | —         | macOS implemented and measured; Windows pieces pending a build |

FX-006's sibling Mirror shipped in M0 as well; it was never given its own
issue number.

---

## Open gaps that are not issues yet

- Catch2 — not wired. Parameter automation and the framing controller have
  standalone C++ tests through CTest; headless remains the required rendering
  check.
- Windows subject detection — no equivalent of Vision, and every candidate is
  a third-party dependency or a shipped model file. Options and their costs
  are in `docs/TRACKING.md`.
- spdlog — call shape in `Log.h` matches; implementation is `printf`.
- JSON config — no schema, no loader.
- CI — no `.github/` workflow.
