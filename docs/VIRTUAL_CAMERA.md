# CamVJ — PROGRAM as a webcam

**Status: implemented on macOS 13+, via an installed camera extension.
Windows is not implemented.**

The operator wants the treated picture inside Zoom, Meet, Teams or Discord,
recognised by the system as a camera. This is that path. It is separate from
the display output in [RUNTIME.md](RUNTIME.md): the wall and a call are two
consumers of the same PROGRAM picture and are normally used together.

---

## Why this does not install anything

macOS 12.3 deprecated CoreMediaIO DAL plug-ins — the old way an application
published a fake camera by dropping a bundle into
`/Library/CoreMediaIO/Plug-Ins/DAL/`. macOS 13 replaced them with **Camera
Extensions**: a system extension, approved once by the user in System
Settings, that runs as its own sandboxed process and vends a CoreMediaIO
device to every application on the machine.

Shipping our own extension is the product answer, and it is not free:

| Requirement | Where CamVJ stands |
| ----------- | ------------------ |
| Developer ID signature on app and extension | not signed; the DMG is unsigned |
| Notarisation | not notarised |
| `com.apple.developer.system-extension.install` entitlement | needs a paid Apple Developer account |
| App installed in `/Applications` | packaging already does this |
| Otherwise: SIP disabled plus `systemextensionsctl developer on` | not acceptable on a show machine |

Until that is worth paying for, the engine does the other half of the job. A
camera extension device carries **two** streams: a *source* stream that call
applications read, and a *sink* stream that any process may push frames into.
That is how an application feeds its own extension, and the extension OBS
Studio installs authorises every client that asks. So CamVJ connects to the
sink of the already installed extension as an ordinary CoreMediaIO client.

**The consequence to state plainly:** the device is named by the extension
that owns it, so a call application lists **OBS Virtual Camera**, not CamVJ.
The picture is CamVJ's PROGRAM; the name on the menu is not.

---

## Operator setup, once per machine

1. Install OBS Studio and open it once. It asks to install its camera
   extension; allow it.
2. Open **System Settings > General > Login Items & Extensions > Camera
   Extensions** and enable **OBS Virtual Camera**.
3. Quit OBS. It does not have to run: the extension is a separate process and
   the device stays on the machine.
4. In CamVJ, open the **OUTPUT** panel and press **Start webcam**, or start
   with `--webcam`.
5. In the call application, pick **OBS Virtual Camera** as the camera.

`systemextensionsctl list` reports the state. `activated enabled` is ready;
`activated waiting for user` means step 2 has not been done and the device
does not exist yet.

Do not run OBS's own virtual camera at the same time. Both would be pushing
into one sink, and the sink has one client.

---

## The frame path

`App::renderFrame` offers PROGRAM to the webcam inside the processing
bracket, after `ProgramOutput` and before `endProcessing`:

```text
chain_.process()          the look
programOutput_.render()   FX / Clean / Freeze / Black
webcam_->submit(frame)    ← here
device_->endProcessing()
```

So the webcam carries PROGRAM, not the chain output: Clean, Freeze and Black
reach a call exactly the way they reach the wall, including the Freeze latch
on input loss.

`submit` does no more than encode one fullscreen pass onto the command buffer
the chain already owns — RGBA16Float into a BGRA8 `CVPixelBuffer` from a
pool, letterboxed by the same fit arithmetic the display output uses. The
frame is only handed to CoreMediaIO from the command buffer's completion
handler, which runs off the frame loop. Nothing in the path blocks on the
consumer.

Two bounds keep a slow or absent consumer from becoming a video fault:

- the pixel buffer pool has an allocation threshold, so exhausting it fails
  the frame instead of growing;
- `CMSimpleQueueEnqueue` fails when the extension's queue is full, and the
  frame is dropped there.

Both count as `skipped` and are on the OUTPUT panel as *dropped*. A rising
dropped count is the only symptom this path produces; it never stalls the
chain and never delays the wall.

Cost on an M4, 1920×1080: the extra pass is inside `gpu process`, so the
Stats panel already accounts for it.

---

## Limits

- **1920×1080 only.** The extension declares one format, which happens to be
  the engine canvas, so PROGRAM arrives without a resample. A 9:16 show is
  the centred letterbox strip on that canvas, exactly as on the wall.
- **One sender.** Starting fails while another application is feeding the
  same sink, and says so.
- **The device is named after its owner.** See above.
- **macOS 13+.** Below that the panel reports the feature as unsupported;
  the CLI flag warns and exits non-zero.
- **Windows: not implemented.** A virtual camera there is a DirectShow filter
  or a Media Foundation Virtual Camera, and both are registered by an
  installer rather than connected to at runtime.
  `src/video/virtual_camera_stub.cpp` reports the feature as unsupported.

---

## Files

```text
src/video/virtual_camera.h          the interface, portable
src/video/mac/virtual_camera_mac.mm CoreMediaIO sink client + Metal conversion
src/video/virtual_camera_stub.cpp   every other platform
src/app/App.cpp                     App::serviceWebcam, submit in renderFrame
src/ui/OutputPanel.cpp              the WEBCAM control
```

Ownership follows the display output: the application asks between frames,
never inside one. A worker thread owns the extension connection, because
`CMIODeviceStartStream` talks to another process and can take long enough to
be seen as a dropped frame. A stopping output is kept alive until its worker
reports `Stopped`, so no frame is still travelling when the stream closes.

Webcam lifecycle and PROGRAM processing continue when the operator window is
minimised, hidden or has no preview drawable, even with no display output.
When no display is pacing the windowed loop, a 16.683 ms software cadence keeps
the hidden sender from flooding the GPU queue. This does not change the stream
format or provide genlock. Headless retains its unpaced diagnostic loop.

---

## Verifying

```bash
# Reports the state of every installed camera extension.
systemextensionsctl list

# Fails with an actionable message when the extension is not enabled,
# and exits 1 so a script notices.
./build/bin/atem_fx --headless --frames 200 --webcam
```

With the extension enabled, the run logs `sending PROGRAM to the installed
camera extension` and the panel's frame counter climbs. A consumer confirms
the rest: open Photo Booth or the call application and choose the device.

---

## If this should become a CamVJ camera

The interface does not change. `createVirtualCameraOutput` would return an
implementation that talks to an extension shipped inside `CamVJ.app`, and the
work is packaging, not video: a `CMIOExtension` target, the system extension
entitlement, a Developer ID signature and notarisation, and an install
request on first launch. Everything above the seam — the panel, the CLI flag,
the PROGRAM tap, the bounds — stays as it is.
