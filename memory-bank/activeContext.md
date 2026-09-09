# Active context

## Foco

**Prioridade ativa: macOS primeiro.** O usuário decidiu em 2026-09-09
priorizar o desenvolvimento/validação macOS e só depois retomar Windows.
M0 está fechado; o próximo plano ativo é
`docs/plans/2026-09-09-macos-first-development.md`: UI manual, câmera/FX30,
Vision tracking + Auto Frame, saída para display e pacote unsigned.

M1 DeckLink fica preservado, mas pausado: FX-010 discovery foi implementado e
aguarda validação de build/placa Windows; FX-011 tem seam portátil
(`decklink_capture.h` + `DeckLinkSource`) integrado ao `VideoSource`, mas a
captura Windows com SDK/placa ainda não existe. Playback, filas, timing e split
de thread ainda não existem.

Pedido atual (2026-09-08): pacotes de distribuição unsigned —
`scripts/package_macos.sh` → `dist/CamVJ-<ver>-macos-<arch>.dmg` (CamVJ.app +
Applications; arm64 no host de release atual); `scripts/package_windows.ps1`
→ ZIP com CamVJ.exe + shaders. Bundle id / binário macOS inalterados. Sem
codesign/notarização nesta etapa. Validação Windows do script pendente
(precisa de máquina VS).

Pedido anterior (2026-09-08): ícone do app no Dock/Finder — `assets/macos/CamVJ.icns`
gerado de `assets/files/camvj-icon-1024.png`, `CFBundleIconFile` no plist e
cópia para `Contents/Resources` via CMake. Bundle id e nome do binário
inalterados (`fx.atem.engine` / `atem_fx`).

Pedido anterior (2026-09-08): sidebar — SOURCE / OUTPUT / EFFECTS / PARAMETERS
dobram ao clicar o título. Estado de sessão em `theme::panelOpen`; SOURCE e
OUTPUT encolhem para a altura do label para o painel de efeitos herdar o
espaço. Sem persistência (imgui.ini continua desligado). Sem mudança de
pipeline.

Pedido anterior (2026-09-08): `shutter` — rastro temporal em movimento rápido.
`TargetPool::persistent()` + segundo sample (t1) no mesmo fullscreen pass.
Não é o grafo de feedback M2 (FX-008).

Pedido anterior (2026-09-08): `fm_raster` + `crt` fecham a cadeia visual com
`subpixel`. Sem glow/bloom (precisaria downsample), sem áudio, sem feedback.

Pedido anterior (2026-09-08): efeito `subpixel` — células RGB gated por luma
que saem da grade. Um ShaderEffect, sem CRT, FM raster, áudio ou feedback.
Não abre M2/M5.

Pedido anterior (2026-09-08): aplicar a identidade CamVJ e melhorar UX/UI da
superfície de operação (tema ImGui, tally SOURCE/PROGRAM/LIVE, glifo no
header). Não mexe no pipeline de vídeo.

Pedido anterior (2026-09-08): Sony FX30 via USB como webcam. O macOS já via a
ILME-FX30 (UVC 1080p30); o app só enumerava na abertura. Hotplug de câmera
(não DeckLink) e escolha explícita de formato UVC.

Pedido seguinte (2026-09-08): tracking de pessoa/objeto para manter o
enquadramento, para painel de LED em evento, junto com ATEM e o mapeamento
feito no Resolume. Implementado como três partes — `Tracker` (Vision no
macOS), `FramingController` portátil com CTest, e efeito `auto_frame` que
recorta na GPU. Extensão 2026-09-08: formato de saída 16:9 ou 9:16
(letterbox, canvas fixo) e Preview SOURCE/PROGRAM com overlay da área.
**Windows não tem detector** (FX-022): decisão de dependência pendente.
Validação com câmera ao vivo pendente. Ver `docs/TRACKING.md`.

Não implementar ATEM, MIDI, áudio, presets, grafo DAG, feedback effect,
gravação ou streaming agora.

## Decisões ativas

- Single-thread é temporário e está escrito em `App.h` e
  `docs/ARCHITECTURE.md`. Remover de propósito no M1, no seam
  `App::renderFrame()`.
- `TargetPool::persistent()` já existe. Não tratar isso como M2 feito.
- Chain linear + registry + ParameterSet já existem. M2 = grafo; M3 = presets.
- Logger próprio de propósito: spdlog entra quando hardware exigir, não
  “porque o AGENTS.md cita”.
- Headless continua sendo o gate. Há teste C++ portátil de automações via
  CTest, sem Catch2 ou outra dependência nova.
- Cada `Parameter` contém seu `ParameterAutomation`: Sine/Triangle/Ramp Up/
  Ramp Down/Square, intervalo, ciclo 0,05–600 s, fase, pause e restart.
- `value` preserva o ajuste manual; `currentValue()` resolve o loop, limita
  ao intervalo do parâmetro e converte Int/Bool. `ShaderEffect` empacota esse
  valor efetivo no mesmo constant buffer, sem alteração de shaders/RHI.
- `EffectChain::process()` avança todos os relógios uma vez por chamada com
  `deltaTime`, antes do bypass. Seleção e reorder não interferem; pause
  congela; Loop desligado restaura manual e suspende relógio; religar retoma.
- Restart zera o relógio e mantém o offset de fase. Reset restaura o default
  manual e remove a configuração de loop. UI somente nos efeitos, não fonte.
- Loops são só da sessão. Não são DAG M2, presets M3 nem MIDI/áudio M5.
- Tracking é control plane, nunca pipeline: lê os frames que a captura já
  produziu na memória de sistema (`VideoSource::setFrameObserver`), em thread
  própria, ~12 Hz. Sem readback, sem trabalho de GPU, sem custar frame.
- O tracker só copia o frame quando o worker está pronto (`hungry_`): 1 em 5
  a 59,94 fps. Copiar todos custaria ~500 MB/s na thread de captura.
- Quem decide o movimento é `FramingController` (`src/tracking/framing.cpp`),
  aritmética escalar com teste: na primeira captura aponta para o assunto,
  depois dead zone com histerese, ease exponencial, limite de velocidade,
  clamp dentro do quadro, hold e return ao perder.
- `auto_frame` entra ligado e primeiro na chain default, para o PROGRAM ser
  o quadro do LED com o palestrante no centro.
- `EffectContext` ganhou `TrackingSnapshot` e o retângulo de framing — cópia
  por frame. O efeito não sabe se quem dirige é uma pessoa, o operador ou nada.
  `framingActive` é limpo pela App e gravado pelo Auto Frame se ele rodou.
- `auto_frame` sobrescreve `packConstants`: controles de operador viram o
  recorte na fonte e a janela de saída (16:9 cheio ou faixa 9:16 com barras).
  Bool `portrait`. Não é UI custom — a UI segue genérica.
- Preview dividido: SOURCE (câmera + caixas) e PROGRAM (saída). Canvas
  permanece 1920×1080; 9:16 é letterbox no centro para o Resolume recortar.
- Tracker reporta em coordenadas da imagem capturada; `App` mapeia para a
  canvas com `VideoSource::mapping()` (fit/mirror do `source_blit`).
- Recorte custa resolução. `Max Zoom` é o controle que decide quanto.
- Dois shaders por efeito, sempre. Sem transpiler até ~30 efeitos.
- RHI não cresce “de passagem”.
- Discovery é um comando exclusivo `--list-decklink`: enumera/loga e encerra
  antes de `App`, GPU ou UI. Não entra no loop de vídeo.
- O SDK DeckLink é opcional, externo e restrito ao build Windows neste
  projeto: `ATEMFX_ENABLE_DECKLINK=ON` + `ATEMFX_DECKLINK_SDK_DIR`.
- macOS e builds sem SDK usam stub indisponível (exit 1). Enumeração sem
  dispositivos é sucesso (exit 0); falha de COM/driver/enumeração é exit 1.
  Mistura com flags de render é erro de uso (exit 2).
- Conexões e capacidades reportadas são capacidades do dispositivo, não
  detecção de sinal. Metadados incompletos geram avisos.
- README operacional em PT-BR. `docs/` e `AGENTS.md` em inglês.
  Memory-bank em PT-BR. `docs/VISION.md` é histórico, não spec.

## Mudanças recentes

Shutter (2026-09-08): efeito `shutter` — mistura o frame atual com o
anterior. Decay = duração do rastro; Threshold = ignora jitter parado.
Dois draws (mix + blit da history). RHI ganhou `history` opcional em
`FullscreenPass::draw` (t1). Não abre M2.

FM Raster + CRT (2026-09-08): `fm_raster` (scanlines FM pela luma) entra
antes do `subpixel`; `crt` (scanlines + grille + CA leve, sem bloom) entra
por último. Ambos um pass. Sem áudio, feedback ou downsample.

Subpixel (2026-09-08): efeito `subpixel` — grade gated por luma, sprites
R/G/B que orbitam dentro da célula. Um pass, Point sampler, 8 parâmetros.

UI CamVJ (2026-09-08): paleta e glifo em `src/ui/Theme.*`, header com tally
LIVE, SOURCE ciano / PROGRAM magenta, overlays Tungsten/ciano. Sem mudança
de pipeline, RHI ou efeitos. Bundle ID macOS permanece `fx.atem.engine` para
não invalidar a permissão de câmera.

Automações: núcleo portátil em `src/effects/parameter_automation.h/.cpp`,
estado agregado a `Parameter`, avanço centralizado na chain e controles
genéricos por parâmetro. Cada nó mantém seus próprios loops, inclusive ao ser
reordenado ou bypassado. Fase double com wrap, sem contador crescente, alloc,
lock ou log na avaliação por frame. Teste
`tests/parameter_automation_test.cpp`, target `parameter_automation_test`,
CTest `parameter_automation`, habilitado por `BUILD_TESTING` (default ON).
Validação técnica aprovada em 2026-09-09 (build, CTest, shaders e gate
headless); inspeção manual da UI ainda pendente.

FX-010: declaração portátil em `src/decklink/decklink_discovery.h`,
implementação Windows e stub sem SDK; integração opcional do SDK em
`cmake/decklink.cmake`; dispatch de `--list-decklink` no startup.
Documentação e memory-bank distinguem discovery existente de pipeline M1
futuro. Nenhum capture, playback ou hotplug foi adicionado.

FX-011 seam inicial (2026-09-09): `src/decklink/decklink_capture.h` define a
API capture-only e `decklink_capture_stub.cpp` mantém builds sem SDK sem fontes
SDI. `src/video/DeckLinkSource.*` ocupa o seam `VideoSource` usando o mesmo
upload BGRA + `source_blit` das câmeras, com mapping identidade e single-slot
temporário. A implementação real `decklink_capture_win.cpp` ainda falta e
depende de validação Windows/DeckLink.

Validação final em 2026-09-08: build Release macOS passou; 20 verificações
de CLI passaram, inclusive conflitos com os novos comandos de fontes e
`--check-shaders`, sem iniciar a GPU. CMake rejeitou corretamente habilitar
DeckLink no macOS. Gate final fora do sandbox: 200 frames em Apple M4,
1,131 ms de processamento GPU. Isso valida o motor atual, não o backend
DeckLink Windows. O logger agora preserva nomes Unicode no console Windows
e mantém UTF-8 em saída redirecionada; a execução Windows segue pendente.

## Próximos passos de produto

0. Tracking: o show roda no **macOS** (decidido em 2026-09-08). Vision é o
   detector de produção desta feature; o detector do Windows fica adiado, não
   bloqueia. Formato 16:9 / 9:16 (letterbox no canvas 1920×1080) e Preview
   SOURCE/PROGRAM estão implementados. Abertos: (a) validar com câmera real e
   Auto Frame ligado — caixas no SOURCE, PROGRAM 16:9 vs 9:16; (b) existe
   saída para display HDMI/DisplayPort no macOS, mas saída limpa SDI/DeckLink
   para voltar à ATEM não está resolvida nem escopada.
1. Conferir manualmente os controles de loop na UI; build, CTest, shaders e
   gate macOS de 200 frames já passaram em 2026-09-09.
2. Validar FX-010 com build Windows e SDK real: diagnóstico sem driver,
   enumeração sem dispositivos e metadados de uma placa. Não marcar done
   antes dessa validação.
3. FX-011 capture, FX-012 playback, FX-013 filas, FX-014 timing — issues
   separadas, não uma classe “faz DeckLink”.
4. Tirar o processamento da UI thread usando o snapshot `EffectContext`.

## Considerações

Máquinas sem GPU (sandbox de agente) não rodam o binário Metal. O gate
headless precisa da GPU do host; testes de discovery no stub não substituem
validação Windows com SDK, driver e placa.
