# M1 DeckLink Windows Validation and Capture Implementation Plan

> **Status:** Paused as of 2026-09-09. The active sequence is macOS-first; use `docs/plans/2026-09-09-macos-first-development.md` before resuming this Windows DeckLink plan.
>
> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task after the macOS-first plan is complete.

**Goal:** Move CamVJ from DeckLink discovery only toward the first real M1 hardware path: validating FX-010 on Windows, then adding FX-011 capture as a selectable `VideoSource` without capture/playback coupling.

**Architecture:** Keep DeckLink discovery, capture, playback, frame queues and timing as separate seams. FX-011 adds a capture-backed source that feeds the existing GPU pipeline through `VideoSource`, initially with the same CPU BGRA upload path used by cameras; FX-013 can replace the single-slot handoff with the production bounded queue later. No ATEM, MIDI, audio, presets, DAG or feedback work belongs here.

**Tech Stack:** C++20, CMake, Windows/MSVC x64, Blackmagic DeckLink SDK COM API, Direct3D 11 backend, existing `VideoSource` / `TargetPool::uploadTarget` / `source_blit` path.

---

## Constraints from project docs

- Windows is the production platform for DeckLink and ATEM SDK work.
- `--list-decklink` remains a one-shot diagnostic command that exits before `App`, GPU or UI initialization.
- FX-011 capture must not imply FX-012 playback.
- Do not create one class that "does DeckLink".
- The capture callback must not block on UI, disk, network, GPU work or locks that can contend with the frame loop.
- Project canvas remains 1920x1080.
- Hardware paths require useful diagnostic logging.
- Every change still needs the macOS gate: `./build/bin/atem_fx --headless --frames 200`.

---

## Phase 0: preserve current release/docs cleanup

### Task 0.1: Commit the current known-good cleanup

**Objective:** Save the current version/docs/package fix before starting M1 hardware changes.

**Files:**
- Existing modified files only:
  - `CMakeLists.txt`
  - `README.md`
  - `docs/BUILD.md`
  - `docs/ROADMAP.md`
  - `memory-bank/activeContext.md`
  - `memory-bank/progress.md`
  - `scripts/package_macos.sh`

**Steps:**

1. Review the diff:

```bash
git diff --stat
git diff -- CMakeLists.txt README.md docs/BUILD.md docs/ROADMAP.md memory-bank/activeContext.md memory-bank/progress.md scripts/package_macos.sh
```

2. Re-run the fast syntax check for the package script:

```bash
bash -n scripts/package_macos.sh
```

Expected: exit 0.

3. Commit:

```bash
git add CMakeLists.txt README.md docs/BUILD.md docs/ROADMAP.md memory-bank/activeContext.md memory-bank/progress.md scripts/package_macos.sh
git commit -m "chore: align release version and macOS package naming"
```

---

## Phase 1: validate FX-010 on Windows before coding FX-011

### Task 1.1: Windows SDK build without DeckLink enabled

**Objective:** Prove the normal Windows build still works before introducing SDK variables.

**Files:** none.

**Commands on Windows, in x64 Native Tools Command Prompt for VS 2022:**

```bat
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release
build-win\bin\Release\atem_fx.exe --check-shaders
build-win\bin\Release\atem_fx.exe --headless --frames 200
```

**Expected:** build succeeds; shaders compile; headless exits 0. Record GPU adapter and timings.

### Task 1.2: Windows DeckLink SDK build with no hardware required

**Objective:** Prove MIDL generation, COM bindings and SDK include paths work.

**Files:** none.

**Commands:**

```bat
cmake -S . -B build-decklink -G "Visual Studio 17 2022" -A x64 -DATEMFX_ENABLE_DECKLINK=ON -DATEMFX_DECKLINK_SDK_DIR="C:\SDKs\Blackmagic DeckLink SDK"
cmake --build build-decklink --config Release
build-decklink\bin\Release\atem_fx.exe --list-decklink
```

**Expected:**

- With Desktop Video missing or broken: exit 1 with HRESULT and actionable guidance.
- With Desktop Video installed and zero devices: exit 0 and `DeckLink discovery complete: 0 device(s).`
- With hardware: device names, model names, capture/playback capability mask and video connection masks are logged.

### Task 1.3: Record Windows validation results in docs

**Objective:** Keep agents from guessing later.

**Files:**
- Modify: `memory-bank/activeContext.md`
- Modify: `memory-bank/progress.md`
- Modify: `docs/ROADMAP.md`
- Modify if commands change: `docs/BUILD.md`

**Steps:**

1. Add only facts actually observed on Windows.
2. Do not mark FX-010 done unless SDK build, driver failure or zero-device path, and at least one real hardware enumeration have all been verified.
3. Keep `README.md` operator-focused; put detailed diagnostics in `docs/BUILD.md` or memory-bank.

---

## Phase 2: introduce FX-011 types without touching the app path

### Task 2.1: Add a portable DeckLink capture declaration

**Objective:** Define the capture seam independently from discovery and playback.

**Files:**
- Create: `src/decklink/decklink_capture.h`
- Create: `src/decklink/decklink_capture_stub.cpp`

**Implementation shape:**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "video/VideoSource.h"

namespace atemfx {

inline constexpr const char* kDeckLinkSourceIdPrefix = "decklink:";

struct DeckLinkCaptureFrame
{
    const uint8_t* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    std::size_t rowBytes = 0;
    bool bottomUp = false;
};

using DeckLinkCaptureCallback = std::function<void(const DeckLinkCaptureFrame&)>;

class DeckLinkCapture
{
public:
    virtual ~DeckLinkCapture() = default;
    virtual bool start(const std::string& deviceId, DeckLinkCaptureCallback callback, std::string& error) = 0;
    virtual void stop() = 0;
    virtual void poll() {}
    virtual std::string status() const = 0;
};

std::vector<VideoSourceDescriptor> enumerateDeckLinkCaptureSources();
std::unique_ptr<DeckLinkCapture> createDeckLinkCapture();

} // namespace atemfx
```

**Stub behavior:**

- `enumerateDeckLinkCaptureSources()` returns an empty vector.
- `createDeckLinkCapture()` returns `nullptr`.

**Verification:**

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bin/atem_fx --headless --frames 200
```

Expected on macOS/default builds: no new source appears and all gates pass.

### Task 2.2: Wire the new files into CMake without enabling capture yet

**Objective:** Keep all builds compiling while capture is unavailable.

**Files:**
- Modify: `cmake/decklink.cmake`
- Modify if needed: `CMakeLists.txt`

**Approach:**

- When `ATEMFX_ENABLE_DECKLINK=OFF`, include both discovery and capture stubs.
- When enabled, include `decklink_discovery_win.cpp` plus the future `decklink_capture_win.cpp`.
- Do not add `decklink_capture_win.cpp` until Task 4.1.

**Verification:** same macOS gates; Windows default build should still compile.

---

## Phase 3: expose DeckLink capture as a source descriptor

### Task 3.1: Add capture source enumeration to the video source list

**Objective:** Let `--list-sources` show DeckLink capture devices in SDK builds while default/macOS builds remain unchanged.

**Files:**
- Modify: `src/video/VideoDevices.cpp`
- Modify: `src/video/VideoDevices.h` comments if needed
- Test manually: `./build/bin/atem_fx --list-sources`

**Implementation:**

- Include `decklink/decklink_capture.h`.
- Append `enumerateDeckLinkCaptureSources()` after cameras.
- Use category `SDI` for DeckLink capture descriptors.
- Descriptor IDs must start with `decklink:`.

**Verification on macOS:**

```bash
./build/bin/atem_fx --list-sources
```

Expected: only Test Pattern and OS cameras; no DeckLink source in stub builds.

### Task 3.2: Add a DeckLinkSource class shell

**Objective:** Create the `VideoSource` implementation without real SDK capture yet.

**Files:**
- Create: `src/video/DeckLinkSource.h`
- Create: `src/video/DeckLinkSource.cpp`
- Modify: `src/video/VideoDevices.cpp`
- Modify: `CMakeLists.txt`

**Pattern:** copy the structure of `CameraSource`, but:

- source has identity mapping;
- no user `fit` / `mirror` parameters initially;
- status must say `DeckLink capture unavailable in this build` when `createDeckLinkCapture()` returns null;
- upload path can use `source_blit` exactly like cameras for BGRA frames;
- do not add playback, output scheduling, frame queues or clock.

**Verification:**

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bin/atem_fx --headless --frames 200
```

---

## Phase 4: implement Windows DeckLink capture

### Task 4.1: Enumerate capture-capable DeckLink devices for `--list-sources`

**Objective:** Convert FX-010 discovery evidence into selectable SDI inputs.

**Files:**
- Create: `src/decklink/decklink_capture_win.cpp`
- Modify: `cmake/decklink.cmake`

**Requirements:**

- Initialize COM using the same MTA pattern as discovery.
- Iterate `IDeckLinkIterator`.
- Query `IDeckLinkProfileAttributes`.
- Include only devices whose `BMDDeckLinkVideoIOSupport` includes `bmdDeviceSupportsCapture`.
- Stable-enough descriptor id for M1: `decklink:<index>` is acceptable only as a temporary id if docs say index order is not persistent. Prefer a persistent attribute if the SDK exposes one reliably on the validated hardware.
- Category: `SDI`.
- Display name: DeckLink display name, fallback to model name, fallback to `DeckLink <index>`.
- Log incomplete metadata as warnings outside any hot path.

**Windows verification:**

```bat
build-decklink\bin\Release\atem_fx.exe --list-sources
```

Expected: Test Pattern, cameras, and capture-capable DeckLink devices.

### Task 4.2: Implement capture start/stop callback

**Objective:** Receive frames from the DeckLink SDK and hand them to `DeckLinkSource` without touching the GPU in the callback.

**Files:**
- Modify: `src/decklink/decklink_capture_win.cpp`

**Requirements:**

- Implement `IDeckLinkInputCallback` in a small COM object.
- Enable only the target M1 format at first: 1920x1080 59.94. Use the SDK display mode that exactly represents 1080p59.94; do not silently fall back to 60.00.
- Request an 8-bit BGRA-compatible frame format if supported. If SDK/hardware cannot deliver BGRA directly, stop and record the required conversion as a separate design task; do not add CPU YUV conversion in the callback as a quick fix.
- In the callback:
  - validate frame pointer and dimensions;
  - read bytes pointer and row bytes;
  - call the provided callback immediately;
  - return without logging per frame.
- Count no-signal / format-changed / dropped callback conditions with atomics; expose them in `status()`.
- `stop()` must disable callbacks and stop streams before releasing COM objects.

**Windows verification:**

```bat
build-decklink\bin\Release\atem_fx.exe --source decklink:<id> --frames 300
```

Expected: app receives frames and status shows resolution/frame count. No playback yet.

---

## Phase 5: document and gate FX-011

### Task 5.1: Update docs with exact capture status

**Objective:** Make M1 state explicit after capture works.

**Files:**
- Modify: `docs/VIDEO_PIPELINE.md`
- Modify: `docs/BUILD.md`
- Modify: `docs/ROADMAP.md`
- Modify: `memory-bank/activeContext.md`
- Modify: `memory-bank/progress.md`

**Rules:**

- Mark FX-011 done only after real hardware capture at 1920x1080 59.94 is observed.
- Explicitly say FX-012 playback, FX-013 frame queues and FX-014 video timing are still open.
- Record exact hardware model, Desktop Video version and SDK version used for validation.
- Do not record temporary commit SHAs or PR numbers in memory-bank.

### Task 5.2: Final verification gate

**Objective:** Prove the change does not regress macOS and works on Windows hardware.

**macOS commands:**

```bash
cmake --build build -j8
ctest --test-dir build --output-on-failure
./build/bin/atem_fx --check-shaders
./build/bin/atem_fx --headless --frames 200
```

**Windows commands:**

```bat
cmake --build build-decklink --config Release
ctest --test-dir build-decklink -C Release --output-on-failure
build-decklink\bin\Release\atem_fx.exe --check-shaders
build-decklink\bin\Release\atem_fx.exe --list-decklink
build-decklink\bin\Release\atem_fx.exe --list-sources
build-decklink\bin\Release\atem_fx.exe --source decklink:<id> --frames 300
```

**Pass condition:** all commands exit 0, with real DeckLink frames received at the target mode and no dropped-frame evidence in the capture status.

---

## Stop conditions

Stop and discuss before proceeding if any of these happen:

- The DeckLink device cannot output BGRA or another GPU-uploadable format without CPU conversion.
- The SDK callback requires a blocking lock to hand frames to the render loop.
- The source implementation needs changes to `EffectChain`, effect shaders or the RHI beyond existing upload support.
- Windows timing shows the UI/display clock is controlling capture in a way that should belong to FX-014.
- Capture requires playback assumptions.

