# Runtime (espelho PT de docs/RUNTIME.md)

## Boot

`src/main.cpp`: DPI (Win32) → parse CLI → dispatch de comando.
Render: `App::initialize` → `run` → `shutdown`. Flag desconhecida: exit 2.
Init falhou: exit 1.

Discovery: `--list-decklink` enumera/loga e encerra antes de criar `App`,
GPU ou UI. É exclusivo: combinar com flags de render dá exit 2.
Enumeração bem-sucedida, incluindo zero dispositivos, retorna 0. macOS,
build sem SDK ou erro COM/driver/enumeração retorna 1. Metadados incompletos
geram avisos. Recursos SDK/COM pertencem ao comando, não a `App`.

São reportados nomes, capacidades capture/playback e conexões de vídeo
suportadas. Não implica sinal conectado, capture/playback iniciado ou
monitoramento hotplug. Implementação Windows ainda aguarda validação.

## Initialize

```text
Window (se !headless) 1600×900
GraphicsDevice->initialize(window, 1920, 1080)
EffectContext snapshot
TestPatternSource
registerBuiltinEffects
createDefaultChain
PresetStore + BootState
OverlayLibrary::scan
OverlaySystem::initialize       // rings fixos + decoder
UiLayer (se window)
FrameTiming::reset
```

Default chain, nesta ordem: auto_frame on, passthrough off, rgb_split off,
pixelate off, fm_raster off, subpixel off, shutter off, frame_delay off,
mirror off, vhs off, crt off. `--enable a,b,c` só muda o enabled desses onze.

## Frame

```text
serviceOutput / serviceWebcam / servicePresets / serviceOverlays
beginFrame                    // preview pode falhar; output/webcam ainda rodam
updateEffectContext
[rescan câmeras se pedido ou hotplug USB]
[reload shaders se pedido]
beginProcessing
    source.render             // persistent "source.frame"
    chain.process             // ping-pong scratch, só enabled
    overlaySystem.service     // clocks + fila decodificada, sem esperar
    overlaySystem.composite   // até 4 layers GPU; depois da chain
    programOutput.render      // FX/Clean/Freeze/Black
    webcam.submit             // PROGRAM
endProcessing
outputSurface.present         // PROGRAM, se ativo
beginUi + draw + render       // só com janela
endFrame(vsync)
```

Headless: `for` N frames (default 300), `reportTimings`, `--dump` via
`readback` (stalla GPU — só depois do loop).

## UI

Header | Program | Source | Output | Presets | Overlays | Effects |
Preview [SOURCE|FX] | PROGRAM |
Inspector embaixo do preview. Não é dock. ImGui OSX+Metal ou Win32+DX11.

Monitor da esquerda é barramento: `SOURCE` (pré-cadeia, overlays de tracking
e Pick) ou `FX` (`chainPreview`, chain + overlays gráficos antes da política de
PROGRAM). O painel embaixo do preview é stats por padrão, parâmetros do efeito,
controles de uma layer ou library de overlays. `InspectorKind` decide.

OVERLAYS fica entre PRESETS e EFFECTS. Import abre picker nativo e prepara a
biblioteca em background; importar não coloca no ar. Até quatro layers, ordem
front-to-back na UI; 16:9/9:16 seguem `EffectContext::outputAspect`.

## Donos

`App` owns Window, Device, Source, Chain, OverlayLibrary/OverlaySystem/picker,
PresetStore, ProgramOutput, outputs, Tracker, Timing e Ui.
`lastOutput_` é ponteiro para textura do pool, válido até o próximo process
ou shutdown.

Detalhe canônico em inglês: `docs/RUNTIME.md`.
