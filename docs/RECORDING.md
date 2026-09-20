# CamVJ — recording PROGRAM

**Status: implemented on macOS (FX-027), pending validation on the show
machine. Windows is not implemented.**

The operator wants a copy of what the audience saw, at a quality that
survives an edit afterwards. This is that path. It is a third consumer of the
same PROGRAM picture, beside the wall ([RUNTIME.md](RUNTIME.md)) and the
webcam ([VIRTUAL_CAMERA.md](VIRTUAL_CAMERA.md)), and it is normally used at the
same time as both.

Recording was listed as out of scope for V1 in [PRODUCT.md](PRODUCT.md). This
is a deliberate, bounded exception, in the same spirit as FX-026: one file
format, one video track, no audio, no timeline, no streaming.

---

## What the file is

| Property        | Value |
| --------------- | ----- |
| Container       | QuickTime `.mov`, written in fragments |
| Codec           | Apple ProRes 422 HQ |
| Raster          | 1920×1080, the engine canvas — never resampled |
| Bit depth       | 10-bit 4:2:2 Y'CbCr (8-bit BGRA fallback, see below) |
| Colour          | Rec.709 primaries, transfer and matrix, video range, tagged in the file |
| Frame rate      | Variable: each frame is stamped when PROGRAM produced it |
| Audio           | None |
| Data rate       | about 440 Mb/s at 1080p59.94 — **~200 GB per hour** |

**Why ProRes 422 HQ.** The engine processes in RGBA16Float; an 8-bit
delivery codec would throw away exactly the gradients effects like CRT, VHS
and vignettes produce. ProRes 422 HQ is visually lossless, edits without
transcoding in every NLE, and Apple silicon encodes it in hardware, so the
recording costs almost nothing on the GPU or CPU the show is using.

**Why 10-bit is done on the GPU.** ProRes 422 stores 10-bit 4:2:2 Y'CbCr.
The recorder converts PROGRAM into exactly that layout (`'x422'`, 10-bit
bi-planar, video range) in two render passes on the processing command
buffer, so the encoder receives what it stores and never converts or dithers.
If Metal cannot map that buffer layout on a given machine, the recorder falls
back to 8-bit BGRA, logs it, and the OUTPUT panel says `8-bit` next to the
REC time. The file is still ProRes 422 HQ.

**Variable frame rate.** CamVJ has no video clock yet (M1). The loop is paced
by the display, so frames are timestamped with the host clock when PROGRAM
was produced, on a 60000 timescale. Premiere, Resolve and Final Cut read this
correctly. If an edit needs constant 59.94, conform in the NLE.

**Crash safety.** The movie is written with 2-second fragments. A crash, a
force-quit or a pulled cable leaves a file that plays up to the last flushed
fragment instead of an unreadable one.

---

## Operator use

1. OUTPUT panel > **RECORD** > **Record PROGRAM**, or start with `--record`.
2. The group label shows `REC`; below it the elapsed time, the size of the
   file, the disk time left at the observed rate, and frames written/dropped.
3. **Stop recording** closes the file. The panel shows `Saved <path>`.

Files land in `~/Movies/CamVJ` as `CamVJ YYYY-MM-DD HH-MM-SS.mov`.
`--record-dir PATH` changes the folder (an external SSD is the right place for
a long show). Two takes in the same second get ` 2`, ` 3`: a recording never
replaces another one.

Like the webcam button, the control is disabled while the console is locked.

Quitting mid-take closes the file before the application exits (bounded to
20 seconds, so a dead drive cannot hang the quit).

A take is refused before it starts with less than 2 GB free. During a take,
a full disk or a removed drive stops the take with an error on the panel; the
fragments already written stay playable.

---

## The frame path

`App::renderFrame` offers PROGRAM to the recorder right after the webcam,
inside the processing bracket:

```text
chain_.process()           the look
overlaySystem_.composite() managed graphics after the chain
programOutput_.render()    FX / Clean / Freeze / Black
webcam_->submit(frame)
recorder_->submit(frame)   ← here
device_->endProcessing()
```

So the file carries PROGRAM — overlays, Clean, Freeze and Black exactly as the
wall showed them — not the preview bus.

`submit` only encodes two draws into the processing command buffer and adds a
completion handler. Everything slow happens on one serial dispatch queue:

| Where                   | What |
| ----------------------- | ---- |
| Frame loop (`submit`)   | take a pixel buffer from a bounded pool, encode the Y and CbCr passes, stamp the time |
| GPU completion handler  | release the plane textures, hand the pixel buffer to the queue |
| Serial queue            | compile the shaders, probe the pixel format, create the movie, append frames, poll disk usage, close the movie |

The pool holds 8 buffers. If the encoder or the disk falls behind, the pool
runs dry and frames are counted as **dropped** — PROGRAM never waits. Dropped
frames are on the panel and in the headless report.

Starting, stopping and collecting the recorder happen in
`App::serviceRecorder`, between frames, like every other device change.

---

## Code

| File | Role |
| ---- | ---- |
| `src/video/program_recorder.h` | seam, stats, portable policy (file naming, time, disk arithmetic) |
| `src/video/program_recorder.cpp` | portable policy |
| `src/video/mac/program_recorder_mac.mm` | AVAssetWriter + Metal conversion |
| `src/video/program_recorder_stub.cpp` | Windows: reports unsupported |
| `tests/program_recorder_test.cpp` | CTest `program_recorder` |

---

## Validation still owed

Written without a macOS toolchain at hand; the portable policy is covered by
CTest, the Objective-C++ path is not yet built. Before trusting it at a show:

1. Build on macOS; CTest must include `program_recorder`.
2. Headless gate: `atem_fx --headless --frames 600 --record --record-dir /tmp/camvj-rec`
   exits 0 and reports `record ... written`.
3. `ffprobe` the file: `prores` profile `hq`, `yuv422p10le`, `bt709`, 1920×1080.
4. Open it in Resolve or Premiere; compare a gradient against the wall.
5. A 30-minute take with the wall and the webcam running: dropped frames stay
   at or near zero and PROGRAM timings do not move.
6. Kill the process mid-take; the file must still open.

## Not in scope

Audio, a choice of codec, H.264/HEVC delivery files, ISO recording of the
source or the preview bus, scheduled or triggered recording, and Windows
(Media Foundation `IMFSinkWriter`, later).
