# File map (repositório real)

Não usar a árvore do `docs/VISION.md`. Isto é o que existe.

```text
CamVJ/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── cmake/
│   └── decklink.cmake           # SDK DeckLink externo opcional, Windows
├── .cursorrules
├── docs/
│   ├── ARCHITECTURE.md
│   ├── ATEM_INTEGRATION.md      # design M4, não código
│   ├── BUILD.md
│   ├── EFFECT_SYSTEM.md
│   ├── TRACKING.md              # tracking + framing (macOS ok, Windows sem detector)
│   ├── PRODUCT.md
│   ├── ROADMAP.md
│   ├── RUNTIME.md
│   ├── VIDEO_PIPELINE.md        # M0 real + discovery M1 + pipeline M1 futuro
│   └── VISION.md                # ensaio histórico
├── memory-bank/                 # este diretório
├── shaders/
│   ├── hlsl/
│   │   ├── common.hlsli
│   │   ├── fullscreen.hlsl      # VS D3D11
│   │   ├── test_pattern.hlsl
│   │   ├── passthrough.hlsl
│   │   ├── rgb_split.hlsl
│   │   ├── pixelate.hlsl
│   │   ├── fm_raster.hlsl       # scanlines moduladas pela luma
│   │   ├── subpixel.hlsl        # grade RGB que flutua
│   │   ├── shutter.hlsl         # rastro temporal
│   │   ├── frame_delay.hlsl     # cópias atrasadas do quadro, eco
│   │   ├── vhs.hlsl             # artefatos de fita: wobble, croma, dropouts
│   │   ├── crt.hlsl             # scanlines + aperture grille, sem bloom
│   │   ├── mirror.hlsl
│   │   ├── crossfade.hlsl       # dissolução FX↔Clean, wet t0 / dry t1
│   │   └── auto_frame.hlsl      # recorte do enquadramento
│   └── metal/
│       ├── common.metal         # prepended em todo fragment
│       ├── test_pattern.metal
│       ├── passthrough.metal
│       ├── rgb_split.metal
│       ├── pixelate.metal
│       ├── fm_raster.metal
│       ├── subpixel.metal
│       ├── shutter.metal
│       ├── frame_delay.metal
│       ├── vhs.metal
│       ├── crt.metal
│       ├── mirror.metal
│       ├── crossfade.metal      # dissolução FX↔Clean, wet t0 / dry t1
│       └── auto_frame.metal
├── src/
│   ├── main.cpp
│   ├── app/          App.h App.cpp
│   ├── core/         Log.h Log.cpp Version.h   # versão vinda do CMake
│   ├── decklink/     decklink_discovery.h
│   │                 decklink_discovery_win.cpp decklink_discovery_stub.cpp
│   ├── platform/     Window.h
│   │   ├── mac/      MacWindow.mm
│   │   └── win32/    Win32Window.cpp Win32MessageHook.h
│   ├── gpu/          Rhi.h Backend.h EffectConstants.h HalfFloat.h ShaderPaths.*
│   │   ├── d3d11/    D3D11Device.h D3D11Backend.cpp
│   │   └── metal/    MetalDevice.h MetalBackend.mm
│   ├── video/        FrameTiming.* TestPatternSource.*
│   │                 source_health.h/.cpp       # Live/Stale/Waiting da fonte
│   │                 program_output.h/.cpp      # FX/Clean/Freeze/Black + latch
│   │                                            # + ProgramTransition (dissolução)
│   │                 virtual_camera.h           # PROGRAM como webcam (interface)
│   │                 virtual_camera_stub.cpp    # Windows: sem câmera virtual
│   │                 mac/virtual_camera_mac.mm  # cliente do sink CoreMediaIO
│   ├── effects/      Effect.h EffectParameters.h EffectRegistry.* EffectChain.*
│   │                 parameter_automation.h/.cpp  # loops escalares por parâmetro
│   │                 ShaderEffect.* BuiltinEffects.*
│   │                 PassthroughEffect.cpp RgbSplitEffect.cpp
│   │                 PixelateEffect.cpp FmRasterEffect.cpp
│   │                 SubpixelEffect.cpp ShutterEffect.cpp
│   │                 FrameDelayEffect.cpp
│   │                 VhsEffect.cpp CrtEffect.cpp MirrorEffect.cpp
│   │                 AutoFrameEffect.cpp        # usa tracking/framing.h
│   ├── tracking/     Tracker.h TrackingSnapshot.h framing.h/.cpp
│   │                 source_mapping.h/.cpp      # canvas↔captura + Pick
│   │                 tracker_stub.cpp           # Windows: sem detector
│   │   └── mac/      VisionTracker.mm           # Vision + object lock, thread própria
│   └── ui/           UiLayer.* Theme.* Panels.h SourcePanel.cpp OutputPanel.cpp
│                     ProgramPanel.cpp EffectsPanel.cpp PreviewPanel.cpp
│                     StatsPanel.cpp
│                     Inspector.h InspectorPanel.cpp  # stats OU parâmetros
│                     ParameterWidgets.cpp
│                     Fonts.h/.cpp               # cascata de fontes + escala de DPI
│       └── backend/  UiLayerMetal.mm UiLayerD3D11.cpp
├── tests/            parameter_automation_test.cpp  # C++ portátil via CTest
│                     framing_test.cpp               # controlador de enquadramento
│                     source_mapping_test.cpp        # clique/overlay do Pick
│                     source_health_test.cpp         # sinal, fps, repeats
│                     program_output_test.cpp        # modos de PROGRAM + perda
└── assets/files/    identidade CamVJ (SVG + IDENTIDADE.md)
```

**Não existem:** `src/atem/`, `src/audio/`, `src/midi/`, `include/`.
`src/decklink/` contém somente discovery; captura, saída, filas e hotplug
não foram implementados.

Namespace: `atemfx`.
