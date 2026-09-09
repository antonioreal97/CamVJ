# CamVJ — Subject Tracking

**Status: implemented on macOS; Windows has no detector.** The framing
controller and the Auto Frame effect are portable and run on both backends.
What Windows lacks is the part that finds the subject — see
[The Windows gap](#the-windows-gap). Live validation against a camera is
pending: it needs camera permission, which the headless gate cannot grant.

**The show runs on macOS** (decided 2026-09-08). Vision is therefore the
production detector for this feature, not a demonstration of one, and the
Windows gap is deferred rather than blocking. That is a departure from
`AGENTS.md` section 1, where Windows is the production platform because
DeckLink and the ATEM SDK live there — it holds for the SDI pipeline and does
not hold for this feature. Anything that follows from a macOS show — how video
reaches the wall without a DeckLink output — is not solved here.

This is not on the milestone ladder in [ROADMAP.md](ROADMAP.md). It was
requested on 2026-09-08 as a bounded extension, on the same terms as parameter
loops: it uses the existing effect abstraction, adds no graph, no preset
storage and no external control source, and does not change what M1 owes.

---

## What it does

Keeps a person or object framed while they move, so a camera pointed at a
stage can feed a LED wall without an operator riding the shot.

```text
        CONTROL PLANE                         PIXEL PIPELINE
        ─────────────                         ──────────────

  capture frames (system memory)
            │
            ▼
        Tracker            own thread, ~12 Hz
            │
            ▼
      TrackingSnapshot ──► EffectContext ──►  Auto Frame ──► crop on the GPU
                            (once per frame)      │
                                                  ▼
                                          FramingController
                                          (dead zone, ease, limits)
```

Three parts, and only the last one touches a pixel:

| Part | Where | What it decides |
| ---- | ----- | --------------- |
| `Tracker` | `src/tracking/Tracker.h` | where the subject *is* |
| `FramingController` | `src/tracking/framing.h` | where the camera *should look* |
| `AutoFrameEffect` | `src/effects/AutoFrameEffect.cpp` | the crop, on the GPU |

The split is the whole design. Detection is slow, intermittent and sometimes
wrong; framing has to be smooth, bounded and predictable; cropping has to
happen every frame in well under a millisecond. Anything that mixes those
three ends up dropping frames to ask a neural network a question.

---

## Why tracking reads capture, not the GPU

`AGENTS.md` forbids readback in the frame loop, and it is right to: a readback
stalls the GPU. But detection needs pixels on the CPU.

The pixels are already there. Capture hands over 8-bit BGRA in system memory
before anything is uploaded, so the tracker taps the frame at that point
through `VideoSource::setFrameObserver`. Nothing is read back, nothing is
copied twice, and the tracker can fall arbitrarily far behind without costing
a frame — the picture keeps running and the framing eases out.

The observer costs the capture thread almost nothing. The tracker only copies
a frame when its worker is ready for one: at 59.94 fps and twelve detections
per second that is one frame in five, and every other call returns after one
atomic load. Copying all of them would cost around 500 MB/s of memory
bandwidth on the one thread that must never be late.

A source with no CPU frames — the GPU test pattern — simply ignores the
observer, and tracking reports "no frames from this input".

---

## Coordinates

The tracker reports in the **captured image's** normalized space, origin top
left. The chain works on the **canvas** — always 1920×1080. A camera that is
not 16:9, or is mirrored for a self-view, sits inside the canvas through
`source_blit`, so `App::updateEffectContext()` maps the observation through
`VideoSource::mapping()` via `src/tracking/source_mapping.h` before it reaches
the chain. An observation that maps into the letterbox bars is discarded: it
is not on the wall.

An SDI input at project resolution maps one to one, so this is invisible in
the M1 pipeline. It matters for the camera inputs the feature is developed
against.

`CameraSource::mapping()` repeats the fit arithmetic that lives in
`source_blit`. The two must stay in step; the shader is the original. A Pick
click is the inverse of that map, so overlay and click cannot drift apart.
Changing input clears the lock: a box on one sensor is meaningless on the next.

---

## Picking a subject

The tracker **chooses for itself until the first Pick**, and only until then.
A camera pointed at one presenter has to frame them with nobody touching the
machine: waiting for an operator published `valid = false` every cycle, which
left `FramingController` with no subject and the crop standing still.

The self-chosen subject is **sticky**. Biggest box on the first frame that has
people in it, then whoever is nearest to the subject already being followed, so
a second person crossing the shot does not take the frame. That is what makes
auto-selection aimable rather than a coin toss re-thrown sixty times a second.
Nobody in shot forgets the choice, so the next person to walk in is judged on
their own merits.

Meanwhile SOURCE keeps showing every person Vision can see as rack-grey boxes
(key-light on hover), and the SOURCE panel lists them left to right as
Person 1, Person 2, …. The self-chosen subject wears tungsten like any other,
and the status line reads `following  ·  N in shot  ·  click SOURCE to choose`:
the operator can always overrule it.

Click a box, a list row, empty SOURCE (0.16 × 0.24 box around the point), or
drag a rectangle — person or object. That region becomes a
`VNTrackObjectRequest`. Tungsten appears on the followed subject.

To switch after a lock, **Pick subject**. Cancel leaves the current lock
alone. A stray click on SOURCE will not steal the shot while locked.

If the lock is lost, `TrackingSnapshot::valid` goes false and framing
Hold / Return run as before. The tracker does **not** jump to another
person. Status reads `lost lock  ·  waiting`. The same `VNTrackObjectRequest`
keeps the last box so Vision can re-acquire that target.

Windows has no tracker, so none of this UI appears.

---

## The framing controller

`src/tracking/framing.cpp`. Pure scalar arithmetic — no GPU, no platform, no
allocation, no clock of its own — and covered by `tests/framing_test.cpp`
through CTest. It is the part that decides whether the result is watchable, so
it is the part that gets tested rather than eyeballed.

Sending detections straight to a crop rectangle produces the seasick picture
that gets auto-framing banned from live shows. What prevents that:

| Setting | Default | What it does |
| ------- | ------- | ------------ |
| Follow Subject | on | Off leaves a plain crop-and-pan on the manual controls |
| Portrait (9:16) | off | Off is 16:9 filling the canvas. On is a 9:16 crop letterboxed into 1920×1080 |
| Subject Size | 0.55 | Subject height as a fraction of the framed height. Bigger is tighter |
| Headroom | 0.12 | Space above the subject, fraction of the framed height. The vertical half of the composition: raise it and the subject moves down the frame |
| Offset X | 0.00 | Where the subject sits horizontally, in fractions of the framed width from centre. Positive puts them right of centre. The looking room a presenter facing across the stage needs |
| Dead Zone | 0.10 | Drift allowed before the framing moves, in fractions of the half extent |
| Smoothing (s) | 0.60 | Time constant of the ease toward the target |
| Max Speed | 0.50 | Ceiling on framing speed, frame widths per second |
| Hold (s) | 2.0 | How long the framing holds after the subject is lost |
| Return (s) | 3.0 | Time constant of the ease back to the home crop of this format |
| Max Zoom | 1.8 | How far following may punch in |
| Manual Zoom / X / Y | 1.0 / 0.5 / 0.5 | Used while Follow is off |

**Offset X moves the frame, not the subject.** To place the subject right of
centre the crop sits left of them, and the crop still never leaves the source
frame - so a large offset near an edge is absorbed by that clamp rather than
producing black bars. It is the horizontal counterpart of Headroom, which is
why there is no Offset Y: the two would fight over the same axis.

### The alignment grid

Composing by eye against a moving picture is guesswork, so the monitors can
carry thirds and a centre cross while the framing is being set:

- It appears **only while a framing node's parameters are open** - that is the
  only place the framing can be adjusted - and disappears when that panel
  closes. `ui::adjustingFraming()` in `src/ui/Inspector.h` is the whole
  condition; the **Grid** checkbox in the panel header turns it off while it
  is open.
- On the SOURCE monitor it is drawn **inside the framing rectangle**, because
  what the operator is composing is the picture that leaves, not the whole
  sensor. Without an active framing it falls back to the whole image. On the
  FX and PROGRAM monitors it covers the picture, which already is the output
  window.
- It is **preview only, by construction**. `drawAlignmentGrid()` writes to the
  interface's own ImGui draw list on top of an `ImGui::Image`; the texture
  handed to `OutputSurface::present()` and to the virtual webcam is the one
  the chain produced and is never touched. There is no shader, no chain node
  and no parameter behind it - a grid that could reach the wall is a grid
  nobody would dare turn on during a show. That is also why it is not an Auto
  Frame parameter: a parameter that changes no pixel would be a lie in the
  first preset that saved one.

Behaviour worth knowing:

- **A fresh lock aims at the subject.** The dead zone does not apply to the
  first frame that sees someone: the crop eases onto the presenter so they
  land in the centre of the LED, instead of punching in on the empty middle
  of the stage. After that the dead zone holds still through small detector
  twitch, and a walking presenter who leaves it is followed until they settle.
  Moving **Subject Size**, **Headroom**, **Offset X**, **Max Zoom** or
  **Portrait** also bypasses the dead zone, so those sliders retarget
  immediately. Manual Zoom /
  X / Y only apply while Follow Subject is off.
- **The dead zone has hysteresis.** Once the subject leaves it the framing
  commits to the move and keeps going until the subject is back near the
  centre, instead of stopping at the boundary and re-triggering on the next
  twitch.
- **Zoom has its own dead zone**, measured against the target rather than the
  current value. Detector boxes breathe by a few percent every cycle; without
  it the picture pulses.
- **The speed limit applies to the move, not to each axis**, so a diagonal
  correction is not allowed to travel faster than a straight one.
- **The crop never leaves the frame.** A subject walking towards the edge
  stops the framing and drifts off centre, which is what a real camera does.
  The alternative is black bars on the wall.
- **A long frame does not teleport the framing.** `deltaTime` is clamped, so a
  host stall slows the move down instead of cutting.
- **Losing the subject is not an emergency.** The framing holds for `Hold`
  seconds — a presenter turning away is not a reason to move — and only then
  eases back out over `Return` to the home crop of the output format (the
  whole frame for 16:9, the centred 9:16 strip for portrait).
- **Max Zoom governs following, not the operator.** Turning Manual Zoom up
  past it is an explicit decision and is obeyed.
- A bypassed Auto Frame node freezes where it stood and eases on from there
  when it comes back, rather than jumping to wherever the subject went.

---

## Output format: 16:9 or 9:16

The canvas stays 1920×1080. LED walls are often mounted either landscape or
portrait, so Auto Frame has a **Portrait (9:16)** switch rather than a second
project format.

- **16:9 (default).** The crop is a 16:9 window on the camera. A close
  subject (webcam, expanded face box) still punches in — zoom 1 would fill
  the canvas, lock the centre, and make PROGRAM identical to SOURCE. That is
  why tracking used to look dead in landscape while 9:16 still followed.
- **9:16.** The crop is a vertical window on the 16:9 camera. The shader
  writes that window into a **centred strip** of the 1920×1080 output
  (~608×1080) and paints the sides black. Pixels stay square. Resolume (or
  the wall processor) crops that strip. The strip's position in the output
  does not move — only the window on the camera does — so a fixed mapping
  keeps hitting the subject.

Zoom of 1 in portrait is that full-height strip, not the whole 16:9 frame.
Returning after a lost subject goes back there too.

The Preview is split so this is visible: **SOURCE** is the full camera with
the subject box (yellow) and the crop (cyan); **PROGRAM** is the chain
output. In 9:16 the Program pane UV-crops the centre strip and letterboxes
it at 9:16 so the operator sees the wall, not a thin band of pixels.

Auto Frame ships **enabled** and first in the default chain, with Follow
Subject on: a live camera is supposed to put the presenter in the centre of
PROGRAM — that is the picture for the LED. Bypass the node, or turn Follow
off, to send the wide shot instead.

---

## The cost in resolution

Cropping throws pixels away. At 1920×1080 in, Max Zoom 1.8 sends about
1067×600 real pixels to a 1920×1080 output, upscaled. On a LED wall, where the
processor is often scaling again, that is visible.

This is a property of the technique, not of the implementation. The way out is
resolution to spare — a 4K camera cropped to 1080 gives a 2× punch-in for
free — which is an M1 capture-format question, not a tracking one. Until then,
`Max Zoom` is the control that decides how much softness is acceptable, and
the default is deliberately conservative.

---

## macOS: Vision

`src/tracking/mac/VisionTracker.mm`. `VNDetectHumanRectanglesRequest` with
`upperBodyOnly` off, falling back to `VNDetectFaceRectanglesRequest` in the
same pass when no body is found — a seated presenter, or a shot too tight for
the body detector. A face box is expanded to roughly head and shoulders so the
zoom does not jump when detection changes its mind about which it found; that
is an estimate, not a measurement.

Vision is a system framework, so it costs no third-party dependency. It runs
on a worker thread at twelve detections per second, which is plenty: framing
moves over hundreds of milliseconds and the smoothing does the rest. Frames
are handed to Vision at capture resolution and it does its own scaling, on
hardware — downscaling them on the CPU first would be exactly the expensive
CPU image processing `AGENTS.md` rules out.

With several people in shot the tracker follows the one it chose (biggest
first, then nearest to that one) and still publishes every box for the operator
to click. After a Pick, `VNTrackObjectRequest` follows that region instead,
`locked_` makes the automatic path a no-op for the rest of the session, and a
lost lock waits rather than switching. A Pick or a camera change also forgets
the automatic choice: neither says anything about who the tracker had picked
for itself.

Vision reports rectangles with the origin at the bottom left. Everything else
in this engine uses the top left.

---

## The Windows gap

Windows is the production platform and it has no detector. `createSubjectTracker()`
returns null there, `startTracking()` logs it, and the Source panel says so.
Auto Frame still runs on its manual controls, and nothing else changes.

There is no Windows equivalent of Vision. The options, none of them free:

| Option | Cost |
| ------ | ---- |
| ONNX Runtime + a small person model | A real third-party dependency and a model file to ship and license |
| Windows ML / DirectML | No extra runtime, but a model file and a narrower API |
| OpenCV DNN | A large dependency for one function |
| No detection: operator-driven framing only | Free, and much less useful |

Each needs justification in `docs/` under `AGENTS.md` section 3, and the
choice belongs to whoever owns the shipping product. Until it is made, subject
tracking is a macOS development and demonstration feature — which is exactly
what macOS is for in this project — and Windows gets the crop without the
sensor.

---

## Verifying

```bash
./build/bin/atem_fx --headless --frames 200 --enable auto_frame   # the gate
ctest --test-dir build -R 'framing|source_mapping'                # controller + click map
./build/bin/atem_fx --check-shaders                               # both shaders
```

The headless gate cannot see a subject: it has no camera, and camera access is
a permission the operator grants. What it does prove is that the effect, the
controller and both shaders run in the pipeline and that an absent tracker is
a normal state.

Tracking against a live camera is verified by running the application with
camera permission granted and selecting a camera input. Auto Frame is on, and
PROGRAM should centre the person **without any Pick**: SOURCE shows the grey
person boxes plus tungsten on the one being followed, and PROGRAM is punched in
on them. Walking should keep them in the middle of PROGRAM. Then click a box
(or drag a region) and PROGRAM should move to that subject instead.
The status line reports `no frames from this input`,
`following  ·  N in shot  ·  click SOURCE to choose`,
`N in shot  ·  click SOURCE to choose` (nobody being followed yet), `locked …`,
or `lost lock  ·  waiting`. A second person walking in must not steal the
subject, before or after a Pick.

The camera is only granted to the **bundle**, so run
`open build/bin/atem_fx.app --args --source <id>`; the raw
`./build/bin/atem_fx` path is refused by TCC and reports
`camera access denied`.
Portrait (9:16) should show black bars on the 1920×1080 program picture and
a vertical Program pane.
