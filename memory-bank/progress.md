# Progresso

## O que funciona (M0)

- App, janela Win32/AppKit, loop, shutdown
- Backends Metal e D3D11 completos atrás de `Rhi.h`
- Test pattern GPU (`pattern` 0–2, `speed`, `markers`)
- EffectChain ping-pong, registry, ShaderEffect
- Efeitos: passthrough, rgb_split, pixelate, fm_raster, subpixel, shutter,
  crt, mirror (HLSL+MSL) (`auto_frame` veio depois, na extensão de tracking)
- UI: Source, Effects (add/remove/reorder), Preview, Stats, hot reload;
  seções SOURCE / OUTPUT / EFFECTS / PARAMETERS dobram ao clicar o título
- CLI headless / frames / dump PPM / enable / no-vsync
- Timing CPU (ring 240) e GPU (queries D3D / completion Metal)
- Resolução de shaders + cópia no build

Issues FX-001 … FX-006: done. Mirror saiu no M0 sem issue própria.
`subpixel` entrou depois, como efeito Distort no mesmo contrato do
ShaderEffect, sem issue de milestone. No M4, `--enable subpixel` mediu
~2,9 ms de GPU em 1920×1080, dentro do orçamento de 16,68 ms. Gather
3×3/9×9 ficou de fora: 16 ms e 82 ms respectivamente.
`fm_raster` e `crt` fecham a cadeia visual: um pass cada, CRT sem bloom.
No M4: FM ~1,1 ms, CRT ~1,9 ms, FM+Subpixel+CRT ~2,7 ms.
`shutter` usa history persistente + t1; sozinho ~0,5 ms. Não é FX-008.

## Extensão solicitada: loops por parâmetro

Implementação tecnicamente validada, sem abrir novo milestone:

- Cada parâmetro de cada nó tem loop independente: Sine, Triangle, Ramp Up,
  Ramp Down ou Square; limites, ciclo 0,05–600 s e offset de fase 0–1.
- Pause/Resume e Restart; configuração e curva na UI genérica dos efeitos.
- Valor manual preservado; valor efetivo limitado ao intervalo declarado,
  Int arredondado e Bool convertido por limiar 0,5.
- Avanço uma vez por processamento da chain usando `deltaTime`, mesmo em
  bypass. Selecionar/reordenar não muda os relógios. Sem timers extras.
- Desativar Loop retorna ao manual e suspende seu relógio; reativar retoma.
  Restart preserva offset; reset volta ao default e remove loop.
- Fonte continua com controles manuais; nada é salvo ao encerrar a sessão.
- CTest portátil em `tests/parameter_automation_test.cpp`, sem Catch2.

Não é editor de nós/DAG (M2), presets (M3) ou modulação MIDI/áudio (M5).
Build Release, CTest e gate headless passaram em 2026-09-09; a conferência
manual dos controles na UI ainda está pendente.

## Extensão solicitada: tracking e enquadramento automático

Implementação em validação, sem abrir novo milestone. FX-022.

- `src/tracking/`: `Tracker.h` (interface), `mac/VisionTracker.mm` (Vision,
  corpo com fallback de rosto, ~12 Hz, thread própria), `tracker_stub.cpp`
  (Windows: nullptr), `framing.h/.cpp` (controlador portátil).
- Efeito `auto_frame` + `shaders/{hlsl,metal}/auto_frame` — um recorte só.
- `VideoSource` ganhou `setFrameObserver()` e `mapping()`, ambos opcionais e
  identidade por padrão. `CameraSource` implementa os dois; DeckLink usará o
  mesmo seam no M1, com mapeamento 1:1.
- `EffectContext::tracking` é o snapshot por frame; `App` faz o mapeamento de
  coordenadas da câmera para a canvas.
- CTest `framing` em `tests/framing_test.cpp` (359 verificações): dead zone,
  acquire no assunto, sliders de Size/Headroom depois do lock, smoothing,
  headroom, limite de velocidade, clamp no quadro, hold/return, manual,
  entradas não finitas, recorte 9:16.
- Auto Frame entra **ligado** e primeiro na chain. Na primeira captura o
  recorte vai até o assunto (não espera a dead zone do quadro inteiro).
- Preview dividido SOURCE (câmera + caixa do assunto e do recorte) / PROGRAM
  (saída; em 9:16 a faixa central em aspecto retrato).
- `EffectContext::framing` / `framingActive` / `outputAspect` para o overlay.
  Canvas permanece 1920×1080.

Verificado: build limpo, `ctest` 2/2, `--check-shaders` 11/11, gate headless
200 frames, e o recorte conferido em dump PPM (zoom 2× em x=0,35).
**Não verificado:** detecção com câmera ao vivo (permissão negada para este
binário) e qualquer coisa no Windows.

## Prioridade ativa: macOS primeiro

Decisão de 2026-09-09: priorizar o desenvolvimento/validação macOS antes de
retomar Windows/DeckLink. Plano ativo:
`docs/plans/2026-09-09-macos-first-development.md`.

Próximos gates macOS, em ordem:

1. inspeção manual da UI CamVJ e controles de loop;
2. câmera ao vivo/FX30 via AVFoundation;
3. Vision tracking + Auto Frame com apresentador em movimento;
4. saída para display externo/LED processor;
5. pacote unsigned verificado.

## M1 preservado, mas pausado

- FX-010 implementado: `--list-decklink` enumera e loga nomes de modelo e
  display, capacidades de capture/playback e conexões de vídeo suportadas.
- Comando exclusivo; encerra antes de `App`, GPU ou UI. Flags de render
  combinadas são erro de uso (exit 2).
- SDK externo opcional no Windows; stub no macOS/build sem SDK (exit 1).
- Enumeração sem placas retorna 0; erros COM/driver/enumeração retornam 1.
  Falhas de metadados opcionais geram avisos e preservam os dados restantes.
- Não inicia capture/playback nem hotplug. Capacidade não significa sinal.

**FX-010 ainda não está done:** build com SDK e validação com driver/placa
Windows pendentes.

- FX-011 começou pelo seam seguro: `decklink_capture.h` declara enumeração e
  callback de captura, `decklink_capture_stub.cpp` retorna nenhuma fonte em
  builds sem SDK, e `DeckLinkSource` já adapta frames BGRA ao `VideoSource` sem
  playback, filas ou clock. A implementação Windows (`decklink_capture_win.cpp`)
  ainda não existe; sem ela, `--list-sources` segue sem SDI.

## O que não existe

| Item | Milestone |
| --- | --- |
| Captura DeckLink real/saída DeckLink, filas, clock de vídeo, split de thread | M1 |
| Grafo DAG, efeito de feedback | M2 |
| Presets JSON | M3 |
| `src/atem/`, FX Bus, FX TAKE | M4 |
| MIDI, áudio | M5 |
| Fill/Key, layers, blend | M6 |
| Catch2, CI, spdlog, JSON loader | não issueados |
| Detector de pessoa no Windows | FX-022, decisão pendente |
| Linux / terceiro backend | fora |

## Infra que parece M2/M3 e não é

- `TargetPool::persistent()`
- `EffectRegistry` + UI genérica
- `ParameterSet`
- Loops escalares por parâmetro dos nós da chain linear, sem persistência

Usar isso. Não marcar FX-007/008/009 como feitos.

## Problemas conhecidos / ruído

- 59,94 vs “60 fps” em textos velhos: orçamento é 16,68 ms. Engine não trava taxa.
- `FrameTiming.h` ainda comenta “4 seconds at 60 fps” no tamanho do ring
  (240 amostras) — comentário, não lock.
- Sem GPU no sandbox → headless falha com “No Metal device”. Não é regressão
  do engine.
- Sem `.github/` workflows.
- Identidade CamVJ aplicada na UI ImGui (`src/ui/Theme.*`); SVGs em `assets/files/`.
- Enums de efeito (`Mirror.mode`) ainda são int slider.
- Câmera USB (FX30 UVC) plugada com o app aberto não aparecia até o restart;
  hotplug AVFoundation + formato 1080p explícito (2026-09-08).

## Gate

```bash
./build/bin/atem_fx --headless --frames 200
```

Obrigatório em macOS com GPU antes de chamar qualquer mudança de código de
feita. O gate mais recente passou em 2026-09-09 no Apple M4: CTest 2/2,
`--check-shaders` 11/11 e 200 frames com `auto_frame` default ligado, 2,417 ms
de processamento GPU. A etapa FX-010 também tinha passado em 2026-09-08 fora
do sandbox: Apple M4, 200 frames, 1,131 ms de processamento GPU, build Release
e 20 verificações de CLI; CMake rejeitou DeckLink habilitado em plataforma não
suportada. A validação de DeckLink e dos diagnósticos Unicode no Windows
continua pendente.
