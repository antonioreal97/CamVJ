# ATEM FX — ATEM Integration

**Status: Design — not implemented.** There is no `src/atem/`, no ATEM SDK
link, and no network talk to a switcher. This file records the V1 behaviour
so M0/M1 do not paint over it. **Do not implement any of this until M4 is
the assigned milestone** (`docs/ROADMAP.md`).

Windows only. The Blackmagic ATEM SDK is not part of the macOS build.

The ATEM controls *behaviour*. It is never part of the pixel path. See
[ARCHITECTURE.md](ARCHITECTURE.md) §1.

---

## Why it exists

ATEM FX sits *beside* the mixer, not inside it.

```text
CAMERA → ATEM AUX → DeckLink IN → ATEM FX → DeckLink OUT → ATEM INPUT 8
```

The operator cuts between the clean camera and the treated return the same
way they cut any other source. The switcher stays the switcher.

---

## Client and state (FX-015, FX-016)

Conceptual types — names are indicative, not a promise of files:

```text
ATEM SDK
    │
    ▼
AtemClient          discovery, connection, command send
    │
    ▼
AtemState           ProgramInput, PreviewInput, TransitionState, Keyers
```

The UI should be able to show:

```text
PROGRAM    CAM 1
PREVIEW    CAM 4
```

and react when those change. Discovery logs device name and connection
result. Failures must be visible; do not swallow SDK errors.

The ATEM thread may block on the network. Capture, processing and output
must not.

---

## FX Bus (FX-017, FX-018)

The product differentiator: treat whichever source is on Preview (or a
chosen source) without the operator crawling AUX menus.

```text
PROGRAM     CAM 2
PREVIEW     CAM 3
FX BUS      CAM 3          ← SEND PREVIEW TO FX
```

That action should configure the switcher:

```text
CAM 3
  → ATEM AUX
  → DeckLink IN
  → ATEM FX
  → DeckLink OUT
  → ATEM INPUT 8
```

Result for the operator:

```text
CAM 3 clean     = INPUT 3 (or whatever the camera input is)
CAM 3 treated   = INPUT 8 (the FX return)
```

AUX routing is the ATEM's job. ATEM FX publishes the request; it does not
become a general-purpose switcher controller.

---

## FX TAKE

A single operator action for "put the treated Preview on Program":

```text
1. Read Preview (e.g. CAM 3)
2. Route that source to the FX AUX
3. Wait until the FX return is valid
4. Select the FX input on Preview (or Program, product decision)
5. AUTO / CUT
```

Exact button semantics are a product decision in M4, not something to invent
in M0. Latency of the wait in step 3 is part of the engineering budget
discussion; it is not a commercial promise.

---

## What this is not

- ATEM FX is not a replacement for the ATEM software.
- It does not switch the show.
- It does not own more than the AUX it is fed and the input it returns on.
- It does not talk to the switcher from the GPU thread.

MIDI, audio-reactive, Fill/Key, OSC and Stream Deck are later milestones
(M5/M6) and are out of scope here.

---

## Prerequisite

M1 must already be returning stable SDI. Building an ATEM client against a
test pattern teaches the SDK and teaches the wrong clock. Do not start M4
to "get a head start" during M0.
