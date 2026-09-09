# Building ATEM FX

**Status: M0 engine implemented; M1 discovery awaiting Windows validation.**
ATEM FX builds on macOS (Metal) and Windows (Direct3D 11). CMake picks the GPU
backend from the host. DeckLink discovery is a separate, opt-in Windows build
feature.

The only required external dependency is Dear ImGui. DeckLink discovery uses
the external Blackmagic DeckLink SDK when enabled (see below). Logging is a small `printf`
wrapper in `src/core/Log.cpp` — spdlog is planned, not linked. Catch2 is not
wired; the parameter automation test is standalone C++ registered with CTest,
with no additional dependency. There is no JSON config file.

---

## macOS

Requirements: macOS 11 or newer, Xcode command line tools, CMake 3.21+.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/bin/atem_fx
```

## Windows

Requirements: Windows 10 1703 or newer, Visual Studio 2022 (Desktop
development with C++) or the Build Tools, CMake 3.21+, a GPU with Direct3D 11
feature level 11_0. The Windows SDK supplies Direct3D, DXGI and the HLSL
compiler.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\bin\Release\atem_fx.exe
```

The executable is a console application on both platforms. That is deliberate
for M0: the diagnostic log is the fastest way to see what the engine is doing.

CMake targets: `imgui` (static), `atem_fx` (the app), `atem_fx_shaders` (copies
`shaders/` next to the binary on every build). Output directory:
`${CMAKE_BINARY_DIR}/bin`.

### DeckLink discovery (FX-010, Windows only)

The SDK is needed to enumerate Blackmagic hardware through its COM API. It is
not bundled, downloaded or required for the default build. Obtain the
DeckLink SDK and a compatible Desktop Video driver from
[Blackmagic Developer Support](https://www.blackmagicdesign.com/developer/products/capture-and-playback).
Extract the SDK outside the repository and install Desktop Video on the
Windows machine used for discovery.

Use **x64 Native Tools Command Prompt for VS 2022** for both commands below.
The Windows SDK must supply `midl.exe`; its preprocessing also requires the
MSVC compiler and SDK include environment.

```bat
cmake -S . -B build-decklink -G "Visual Studio 17 2022" -A x64 -DATEMFX_ENABLE_DECKLINK=ON -DATEMFX_DECKLINK_SDK_DIR="C:/SDKs/Blackmagic DeckLink SDK"
cmake --build build-decklink --config Release
build-decklink\bin\Release\atem_fx.exe --list-decklink
```

`ATEMFX_DECKLINK_SDK_DIR` names the extracted root containing
`Win/include/DeckLinkAPI.idl` and `DeckLinkAPIVersion.h`. CMake generates COM
bindings with MIDL into the build tree and links the Windows system libraries
`ole32` and `oleaut32`. `ATEMFX_MIDL_EXECUTABLE` can select a specific MIDL
executable; the developer shell environment is still required. The enabled
integration currently supports MSVC x64. See the
[Blackmagic Windows SDK FAQ](https://www.blackmagicdesign.com/developer/support/faq/desktop-video-developer-support-faqs)
for the IDL generation and COM apartment requirements.

`--list-decklink` completes before creating `App`, the GPU device or a window.
It logs the SDK/runtime API versions, device display and model names, active
profile capture/playback capabilities, and supported video connection types.
Connection types are capabilities, not cable or signal detection. Enumeration
indices are temporary, not saved device identities. Attributes that cannot be
read produce warnings and remain unavailable; they are not reported as zero.
Unknown connection bits remain visible in the numeric mask.
The diagnostic logger writes Unicode to Windows consoles and UTF-8 when
redirected to a file, preserving non-ASCII device names without changing the
console code page. See Microsoft's
[console output guidance](https://learn.microsoft.com/en-us/windows/console/writeconsole).

| Result | Exit code |
| --- | --- |
| Enumeration completed, including zero devices | 0 |
| Unsupported/disabled build, COM/driver failure, or enumeration failure | 1 |
| Discovery combined with rendering options, or invalid CLI | 2 |

Missing optional metadata does not fail an otherwise complete enumeration.
The `--list-` options are separate commands: `--list-decklink`,
`--list-sources` and `--list-displays` cannot be combined with each other or
with rendering options such as `--source`, `--output` or `--check-shaders`. `--help` takes precedence when encountered. Discovery does not open capture
or playback streams, register hotplug callbacks, or alter device profiles.

Validation status: the default macOS build and CLI are testable here. Windows
compilation, MIDL generation and hardware discovery must still be verified on
a Windows workstation before FX-010 is marked done. On that workstation:

1. Build with discovery both disabled and enabled.
2. Verify a missing driver produces exit 1 with a useful HRESULT and guidance.
3. With Desktop Video installed, verify no hardware produces an empty list
   (exit 0 when the iterator is available).
4. With hardware, compare names and capabilities against Desktop Video Setup;
   check capture-only/output-only and multi-device configurations when available.
   Verify accented device names in the console and in redirected UTF-8 logs.
5. Run the headless GPU check as well. Listing devices does not validate SDI
   throughput, 1080p59.94 mode support or dropped-frame behaviour.

---

## The self-test

```bash
./build/bin/atem_fx --headless --frames 300 --dump frame.ppm
```

No window, no UI, no display required — it works over SSH and in CI. It builds
the real device, compiles every shader, runs the source and the effect chain
for 300 frames, prints the timings and writes the final frame as a binary PPM.

Every change must at least pass this before it is called done:

```bash
./build/bin/atem_fx --headless --frames 200
```

### Parameter automation check

`BUILD_TESTING=ON` (the default) builds `parameter_automation_test` from
`tests/parameter_automation_test.cpp` and registers it in CTest as
`parameter_automation`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

For a Visual Studio build, build with `--config Release` and use
`ctest --test-dir build -C Release --output-on-failure`.
The test needs no display or GPU and no Catch2 installation. It checks loop
evaluation and parameter semantics; it does not replace the 200-frame
headless gate. Loop controls are configured in the effect UI and last for the
current session; no automation CLI flags or preset storage are provided.

### CLI

```text
  --headless          run with no window or UI
  --frames N          stop after N frames (headless default: 300)
  --dump PATH         write the final frame as a binary PPM
  --enable a,b,c      start with exactly these effects enabled
  --no-vsync          present without waiting for the display
  --source ID         start on this video input (see --list-sources)
  --output ID         send the processed frame to this display
  --list-sources      list the available video inputs and exit
  --list-displays     list the available displays and exit
  --check-shaders     compile every shader and exit (opens no device)
  --list-decklink     list DeckLink devices and exit (Windows SDK build)
  --help              show this message
```

`--enable` exists because the self-test has no UI to click. Type ids are the
registry ids: `auto_frame`, `passthrough`, `rgb_split`, `pixelate`, `fm_raster`,
`subpixel`, `shutter`, `crt`, `mirror`.

```bash
./build/bin/atem_fx --headless --frames 60 --enable rgb_split,pixelate --dump chain.ppm
```

With a window, `--frames N` still works: the app closes after N frames.

Unknown flags print usage and exit with code 2. Init failure exits with 1.

---

## Video inputs

The internal test pattern plus every camera the operating system reports: the
built-in webcam, USB and Thunderbolt cameras, and on macOS 14+ an iPhone acting
as a Continuity Camera.

```bash
./build/bin/atem_fx --list-sources
```

```text
CATEGORY    NAME                              ID
Internal    Test Pattern                      test_pattern
Built-in    FaceTime HD Camera                camera:6C707041-05AC-0010-0001-000000000001
USB         Logitech BRIO                     camera:57696C7B-569A-49F3-A06C-3A3300000001
```

Enumeration never opens a device, so it is safe anywhere and never triggers a
permission prompt. Pass an id to start on that input, or pick it from the SOURCE
panel while running:

```bash
./build/bin/atem_fx --source camera:6C707041-05AC-0010-0001-000000000001
```

A camera that will not open does not take the application down: the message
appears in the SOURCE panel and the current input keeps running.

### Display output

Where the processed frame goes. To whatever is on the other end of the cable —
an LED processor, a projector, a switcher input — it is a video signal: a
borderless full-screen window with no title bar, no cursor and no interface.

```bash
./build/bin/atem_fx --list-displays
```

```text
ID         NAME                           RESOLUTION   REFRESH
1          Built-in Retina Display        3024x1964    120.00 Hz  (primary)
2          LG ULTRAGEAR                   1920x1080    144.00 Hz
```

Enumeration opens no window and changes no display configuration.

```bash
./build/bin/atem_fx --output 2
```

Or pick the display in the OUTPUT panel while running. **Stop output**, the
**No output** entry and the **Escape** key all end it. Escape matters when the
output lands on the display holding the interface: the window covers the stop
button too.

Processing stays 1920×1080 and the picture is fitted with black bars on a
display of another shape, never stretched.

While an output is live it paces the engine and the preview window stops
waiting for its own display; the vsync checkbox has no effect until the output
closes. `--output` needs a window and is ignored under `--headless`.

An unknown display id warns and starts with no output rather than refusing to
start: losing the wall is bad, having nothing at all is worse.

### Camera permission

**macOS.** The first time a camera is selected the system asks for permission,
and the answer can arrive long after the request — until then the panel reads
`waiting for camera permission`. If it was denied earlier, grant it in
**System Settings › Privacy & Security › Camera**.

This is why the macOS build produces `atem_fx.app` rather than a bare
executable: macOS denies camera access to a binary with no `Info.plist`
carrying `NSCameraUsageDescription`. `bin/atem_fx` is a symlink into the bundle,
so every command in this document works unchanged.

**Windows.** There is no prompt; the device either opens or fails. If it fails
with access denied, check **Settings › Privacy & security › Camera**.

### Shader check

```bash
./build/bin/atem_fx --check-shaders
```

Compiles every shader in the backend's directory — not only the ones the
current chain uses, because an effect nobody has enabled yet is exactly the one
whose syntax error goes unnoticed. Opens no device and needs no display, so it
belongs in CI next to the self-test.

---

## Dear ImGui

The only required external dependency. By default CMake fetches it from GitHub, pinned
to the tag in `ATEMFX_IMGUI_TAG` (currently `v1.91.9`). To build without
network access, point CMake at a local checkout:

```bash
cmake -S . -B build -DATEMFX_IMGUI_DIR=/path/to/imgui
```

---

## Shaders

Shaders are compiled at runtime, from `shaders/hlsl` or `shaders/metal`
depending on the backend. They are located, in order, from:

1. `ATEMFX_SHADER_DIR` if set;
2. a `shaders` directory beside the executable, or up to five levels above it;
3. the source tree, baked in at configure time (`ATEMFX_SHADER_SOURCE_DIR`).

The build copies `shaders/` next to the executable on every build, and that
copy wins. To hot-reload the real sources while working on effects, point the
environment variable at the repository:

```bash
ATEMFX_SHADER_DIR=$PWD/shaders ./build/bin/atem_fx
```

Then use **Reload shaders** in the stats panel. A shader that fails to compile
keeps its previous version and reports the error in the panel, so a typo never
blacks out the output.

D3D11 compiles `fullscreen.hlsl` as the shared vertex shader; each effect file
is the pixel shader. Metal prepends `common.metal` (which includes the vertex
stage) onto every fragment file at compile time.

---

## Fonts

The UI uses two faces: a sans for anything read as a word and a mono for
anything read as a measurement — panel headers, the stats strip, the header's
technical line. Neither is committed. Each is located, in order, from:

1. `ATEMFX_FONT_UI` / `ATEMFX_FONT_MONO` if set, as a path to a `.ttf`/`.ttc`;
2. `assets/fonts` beside the executable, in the bundle's `Resources`, or up to
   five levels above the executable — where the brand faces go once they are
   licensed and committed (`BigShouldersDisplay-Regular.ttf`,
   `GeistMono-Regular.ttf`);
3. the platform's own fonts — SF and SF Mono on macOS, Segoe UI and Consolas
   on Windows;
4. ImGui's built-in bitmap font, so a machine with none of the above still
   renders.

```bash
ATEMFX_FONT_UI=/path/to/Display.ttf ./build/bin/atem_fx
```

The atlas is rasterised at the density of the display the window is on, not at
ImGui's coordinate unit, and rebuilt when the window moves to a display of a
different density. That is what keeps text sharp on a Retina panel: an atlas
built one-to-one with ImGui units would be drawn at twice its size.

---

## Debugging

On macOS, run with Metal's validation layers when touching the backend:

```bash
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/bin/atem_fx --frames 300
```

On Windows, a Debug build enables the D3D11 debug layer automatically when it
is installed, and falls back cleanly when it is not.

There is no CI workflow in the repository yet. The headless binary is the
intended check; a machine without a GPU (some sandboxes) cannot run it.

---

## Distribution packages

Unsigned packages for handing builds to other machines. There is no Apple
Developer ID / notarization and no Windows Authenticode yet — recipients will
see Gatekeeper or SmartScreen warnings. Code signing is a later step, not part
of these scripts.

DeckLink stays **off** in the default package (no SDK bundled). Brand fonts are
optional and fall back to system faces.

### macOS — DMG

Requirements for recipients: macOS 11+, Metal GPU.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
./scripts/package_macos.sh
```

Produces `dist/CamVJ-<version>-macos-<arch>.dmg` with `CamVJ.app` and an
Applications symlink (drag-to-install). On the current release host this is
`dist/CamVJ-1.0.0-macos-arm64.dmg`; Intel Macs can build a local `macos-x64`
package, but no universal binary is produced by the script. Inside the bundle
the executable remains `atem_fx` and the bundle id remains `fx.atem.engine` so
an existing camera TCC grant is not invalidated. Shaders live in
`Contents/Resources/shaders`.

First open on another Mac: right-click the app → **Open** (Gatekeeper). Grant
camera access when prompted, or later under **System Settings › Privacy &
Security › Camera**.

Options: `-B BUILD_DIR`, `-v VERSION`, `--skip-smoke`.

### Windows — ZIP

Requirements for recipients: Windows 10 1703+, Direct3D 11 feature level 11_0.
Build and package **on Windows** (Visual Studio 2022 x64); the ZIP cannot be
produced from a macOS host.

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows.ps1
```

Produces `dist/CamVJ-<version>-windows-x64.zip` containing `CamVJ/` with
`CamVJ.exe` (renamed from `atem_fx.exe`) and `shaders/` beside it. Unzip
anywhere and run `CamVJ.exe`. SmartScreen may warn on first launch — choose
**More info** → **Run anyway** when you trust the build.

Options: `-BuildDir`, `-Version`, `-SkipSmoke`.

---

## Linux

Not supported. It would need implementations of `src/gpu/Rhi.h` and
`src/platform/Window.h` — Vulkan and a window backend — and nothing else would
have to change. CMake fails at configure time on anything that is not Windows
or Apple.
