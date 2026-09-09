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
UiLayer (se window)
FrameTiming::reset
```

Default chain, nesta ordem: auto_frame on, passthrough off, rgb_split off,
pixelate off, fm_raster off, subpixel off, shutter off, frame_delay off,
mirror off, vhs off, crt off. `--enable a,b,c` só muda o enabled desses onze.

## Frame

```text
skip se minimizado
beginFrame                    // false = skip
updateEffectContext
[rescan câmeras se pedido ou hotplug USB]
[reload shaders se pedido]
beginProcessing
    source.render             // persistent "source.frame"
    chain.process             // ping-pong scratch, só enabled
endProcessing
beginUi + draw + render       // só com janela
endFrame(vsync)
```

Headless: `for` N frames (default 300), `reportTimings`, `--dump` via
`readback` (stalla GPU — só depois do loop).

## UI

Header | Program | Source | Output | Effects | Preview [SOURCE|FX] | PROGRAM |
Inspector embaixo do preview. Não é dock. ImGui OSX+Metal ou Win32+DX11.

Monitor da esquerda é barramento: `SOURCE` (pré-cadeia, overlays e Pick) ou
`FX` (`chainPreview`, a imagem da cadeia antes da política de PROGRAM). O
painel embaixo do preview é a faixa de stats por padrão e os parâmetros do
efeito selecionado quando há um — em até quatro colunas, no máximo 45% do
corpo. `ui::inspectedEffect()` decide (`src/ui/Inspector.h`).

## Donos

`App` owns Window, Device, Source, Chain, Timing, Ui.
`lastOutput_` é ponteiro para textura do pool, válido até o próximo process
ou shutdown.

Detalhe canônico em inglês: `docs/RUNTIME.md`.
