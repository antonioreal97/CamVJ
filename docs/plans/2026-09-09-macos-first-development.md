# macOS-First Development Plan

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** Prioritize CamVJ's macOS production/demo path before continuing Windows DeckLink work.

**Architecture:** Keep the already-created DeckLink seams intact, but treat Windows SDK/hardware work as paused until the macOS path is validated. macOS development stays within existing seams: AVFoundation camera input, Vision tracking, Metal processing, display output, generic effect UI and unsigned packaging. Do not add ATEM, MIDI, audio, recording, streaming, graph/DAG, presets or DeckLink playback while executing this plan.

**Tech Stack:** C++20, CMake, Objective-C++/AppKit/AVFoundation/Vision, Metal, Dear ImGui, existing `VideoSource`, `Tracker`, `FramingController`, `OutputWindow` and packaging scripts.

---

## Phase 0: preserve the Windows seam, pause Windows implementation

### Task 0.1: Confirm clean baseline

**Objective:** Start macOS work from the committed state that includes the portable DeckLink capture seam.

**Files:** none.

**Commands:**

```bash
git status --short --branch
git log --oneline -3
```

**Expected:** working tree clean; recent commit includes `feat: add DeckLink capture source seam`.

### Task 0.2: Keep DeckLink code dormant on macOS

**Objective:** Ensure macOS lists no SDI source from the stub and still rejects DeckLink SDK builds.

**Commands:**

```bash
./build/bin/atem_fx --list-sources
cmake -S . -B build-decklink-macos-check -DATEMFX_ENABLE_DECKLINK=ON
```

**Expected:** normal source list has only test pattern and cameras; the CMake command fails with `ATEMFX_ENABLE_DECKLINK requires Windows with MSVC.` Remove `build-decklink-macos-check` afterward.

---

## Phase 1: manual UI validation on macOS

### Task 1.1: Launch the macOS app with a bounded run

**Objective:** Validate that the windowed operator UI starts with the current theme and panel layout.

**Command:**

```bash
./build/bin/atem_fx --frames 600
```

**Expected visual checks:**

- Product name visible as `CamVJ`.
- Studio Black background.
- SOURCE tally uses Split Cyan.
- PROGRAM tally uses Split Magenta.
- LIVE tally uses Tungsten.
- No glow, gradient or shadow treatment.
- Layout is fixed, not dockable.

### Task 1.2: Validate collapsible operator panels

**Objective:** Confirm SOURCE, OUTPUT, EFFECTS and PARAMETERS collapse/expand without breaking the layout.

**Steps:**

1. Click SOURCE header; expect only the title row to remain.
2. Click OUTPUT header; expect only the title row to remain.
3. Click EFFECTS header; expect only the title row to remain.
4. Click PARAMETERS header; expect only the title row to remain.
5. Reopen all panels.

**Expected:** no overlaps, no lost controls, no persistence required after restart.

### Task 1.3: Validate generic effect controls and parameter loops

**Objective:** Confirm all existing effects still use the generic parameter UI and loop controls.

**Effects to inspect:**

- `auto_frame`
- `passthrough`
- `rgb_split`
- `pixelate`
- `fm_raster`
- `subpixel`
- `shutter`
- `mirror`
- `crt`

**Loop modes to inspect on at least one scalar parameter per mode:**

- Sine
- Triangle
- Ramp Up
- Ramp Down
- Square

**Expected:** Pause/Resume freezes/resumes the effective value; Restart resets phase; Reset restores the manual default and disables the loop.

---

## Phase 2: live camera and Auto Frame validation on macOS

### Task 2.1: Verify available camera sources

**Objective:** Confirm the Sony FX30 or other intended UVC camera appears with a stable ID.

**Commands:**

```bash
./build/bin/atem_fx --list-sources
```

**Expected:** source list includes the intended external camera. If the camera is missing, unplug/replug and re-run; AVFoundation hotplug should update between frames when the app is open.

### Task 2.2: Launch with the real camera

**Objective:** Validate AVFoundation format selection and frame delivery.

**Command:**

```bash
./build/bin/atem_fx --source "<camera-id-from-list>"
```

**Expected:** SOURCE shows the camera, PROGRAM shows the processed chain, and status does not remain on `waiting for first frame`.

### Task 2.3: Validate Auto Frame following

**Objective:** Confirm the show-critical behaviour: a walking presenter stays framed for LED wall output.

**Checks:**

- `auto_frame` is first and enabled by default.
- Follow Subject is on.
- SOURCE overlay shows yellow subject box and cyan crop preview.
- PROGRAM output follows the subject; 16:9 mode punches in instead of matching SOURCE exactly.
- Size and Headroom sliders visibly retarget the crop immediately.
- Dead zone absorbs detector jitter, not intentional operator slider moves.
- 9:16 mode shows a fixed centered portrait strip inside the 1920x1080 canvas.

---

## Phase 3: display output validation on macOS

### Task 3.1: Enumerate displays

**Objective:** Confirm an external display/LED processor path is visible.

**Command:**

```bash
./build/bin/atem_fx --list-displays
```

**Expected:** internal display plus the target external display.

### Task 3.2: Run PROGRAM on the external display

**Objective:** Validate current macOS output route: treated picture to HDMI/DisplayPort/display capture path.

**Command:**

```bash
./build/bin/atem_fx --source "<camera-id>" --output <display-id>
```

**Expected:** operator UI remains on the main screen; PROGRAM output appears on the selected display. This is not SDI/DeckLink and not genlocked.

---

## Phase 4: macOS release polish

### Task 4.1: Re-run full macOS gate

**Commands:**

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bin/atem_fx --check-shaders
./build/bin/atem_fx --headless --frames 200
```

**Expected:** build passes; CTest 2/2; shader check 11/11; headless exits 0 under the 16.68 ms frame budget.

### Task 4.2: Build and verify unsigned package

**Commands:**

```bash
./scripts/package_macos.sh -v 1.0.0
hdiutil verify dist/CamVJ-1.0.0-macos-arm64.dmg
```

**Expected:** DMG verifies successfully. Package remains unsigned/not notarized unless explicitly requested later.

### Task 4.3: Record observed macOS results

**Files:**

- Modify: `memory-bank/activeContext.md`
- Modify: `memory-bank/progress.md`
- Modify if behaviour changed: `docs/ROADMAP.md`

**Rule:** only record facts actually observed. Do not mark Windows/DeckLink validation complete from macOS results.

---

## Stop conditions before returning to Windows

Do not resume `decklink_capture_win.cpp` until the macOS path above has real results for:

1. manual UI inspection;
2. live camera / FX30 source selection;
3. Auto Frame with a moving subject;
4. display output to the intended external screen or LED processor input;
5. unsigned package verification.

Windows DeckLink remains important, but it is now sequenced after macOS operator confidence.
