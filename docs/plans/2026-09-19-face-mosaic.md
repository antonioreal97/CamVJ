# Face Mosaic — Incremental Plan

**Requested 2026-09-19.** A live-camera VJ effect: detect every face in the
audience, keep an identity on each, and copy them across the frame as
independent tiles that can be scaled, turned, delayed and recombined.

**Status:** Phase 1 implemented and verified headless on macOS (Metal). Not
yet seen with a live camera and a real crowd. The D3D11 half is written and
not compiled.

## Decisions (user, 2026-09-19)

1. **Tiles are drawn by a new RHI primitive, `SpritePass`** — up to 64
   instanced quads, premultiplied source-over, in one draw. A fullscreen pass
   per tile would cost ~0.15 ms each at 1080p; the sprite batch costs 0.54 ms
   GPU for 64 tiles on an Apple M4. This is the one RHI widening, and it is
   documented in [EFFECT_SYSTEM.md](../EFFECT_SYSTEM.md#sprites).
2. **Scope:** phases 1, 2, 4 and 5 now, as a bounded extension like subject
   tracking. **Feedback (phase 3) waits for M2 (FX-008). Presets wait for M3.**
   Layers and blend modes stay out of scope; NDI/RTMP/SRT/WebRTC are outside
   V1 — the effect is a chain node and knows nothing about outputs.
3. **Detector:** Apple Vision on macOS, no new dependency. Windows has no
   face detector (same gap as FX-022); the effect runs there and sees an empty
   crowd.

## Where it sits in the engine

```text
capture (CPU BGRA) ──► FaceSensor (own thread, Vision, 15 Hz)
                          └─ FaceTrackManager (IoU + distance, ids)
                                 │ FacesSnapshot, by copy, once per frame
App::updateEffectContext ── maps capture → canvas ──► EffectContext.faces
                                 │
chain:  auto_frame ─► face_mosaic ─► other effects ─► ProgramOutput
                        ├─ background: fullscreen pass (face_mosaic shader)
                        └─ tiles: FaceTileManager → SpritePass (face_tile shader)
```

- The sensor runs only while an enabled node declares
  `Effect::inputs() & kEffectInputFaces`; App never names the effect.
- Tiles sample the chain's input — the live picture — so a copy moves with its
  person between detections. Only metadata crosses from CPU to GPU.
- When Auto Frame ran first, face boxes are carried through its crop; in 9:16
  tiles are placed inside the portrait window, never in the bars.

## Phases

| Phase | Content | Status |
| --- | --- | --- |
| 1 MVP | face sensor, tracking ids, UV crop, tiles, random placement, sprite compositor | **done** (headless; live pending) |
| 2 Temporal | frame history ring (persistent targets, advanced on new camera frames), per-tile delay, lifetime, spawn interval | next |
| 3 Feedback | recursive feedback, zoom, rotation | **M2 (FX-008)** — not now |
| 4 Visual | tint / colour in the tile shader; glow, RGB split, grain, glitch reuse existing chain nodes (`rgb_split`, `vhs`, `crt`, `pixelate`) | after 2 |
| 5 Optimisation | instancing and pooling already in phase 1; profile with a live crowd | after 2 |

Remaining positioning modes (Grid, Around Source, Center Attraction, Chaotic)
and motion (velocity, drift, jitter) belong with phase 2, where tiles gain a
life of their own. Presets (FACE WALL, RED VJ, CHAOS, CLEAN MOSAIC) are M3.

### Phase 2 memory note

A 1920x1080 RGBA16F history frame is ~16.6 MB. The camera delivers 30 fps, so
600 ms of delay is 18 distinct frames (~300 MB), not 36. The ring advances on
new capture frames, not render frames; the slot count is a fixed cap chosen at
`initialize()`.

## Verification (phase 1, 2026-09-19, Apple M4)

- Build clean, no warnings. CTest 9/9, including `face_tracks` (19 checks)
  and `face_tiles` (18 checks). `--check-shaders` 16/16. Headless 200 exit 0.
- Sprite pass proven with a temporary synthetic-face probe (reverted): 3 faces
  x 4 copies render as opaque, rounded, feathered crops of the right source
  regions, with and without Auto Frame in front.
- Cost with 16 faces x 4 copies = 64 tiles: `face_mosaic` alone 0.54 ms GPU;
  full chain of 11 enabled nodes 4.05 ms GPU vs 3.93 ms without it.

**Not verified:** live camera with real faces (Vision detection quality,
identity stability in a crowd, 30 fps capture), anything on Windows/D3D11.
