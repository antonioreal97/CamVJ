# Active context

## Foco

Pedido 2026-09-09: **recolher o sidebar** para um rail de títulos. Chevron
no header (à esquerda do glifo CamVJ) encolhe a coluna de 392 px para
40 px; PROGRAM / SOURCE / OUTPUT / EFFECTS viram letras empilhadas. Clique
num título reabre a coluna e abre aquela seção (PROGRAM só reabre — não
dobra). Os monitores SOURCE e PROGRAM herdam a largura. Com o rail fechado,
FX / Clean / Freeze / Black ficam a dois cliques, como combinado — não
foram para o header. Estado de sessão em `theme::sidebarCollapsed()`, sem
imgui.ini. Sem RHI, sem pipeline.

Pedido 2026-09-09: **Test Pattern → LED Mapping (16:9 + 9:16)** para mapear
painéis. Quadro estático na GPU com guias gravadas na imagem (HLSL e MSL):
16:9 ciano no canvas 1920×1080 inteiro e faixa 9:16 magenta centralizada,
607,5×1080, x=656,25..1263,75. Pattern agora é combo com nomes via
`Parameter::makeChoice`, ainda Int escalar. CLI `--pattern led-mapping`.
`VideoSource::bypassEffects()` é false por padrão e true apenas nesse modo;
App passa a política à cadeia, que avança automações e devolve a fonte antes
de rodar qualquer efeito. Assim Auto Frame/retrato/zoom/FX não deformam as
guias nem alteram as configurações salvas na sessão. PROGRAM mantém
Freeze/Black e o framing do quadro segurado. Speed/Markers só alteram os
padrões animados. Não muda RHI, resolução, captura ou milestones.

Validação dessa opção: build macOS, 5 CTests (incluindo 221 verificações de
PROGRAM), 14 shaders Metal compilados e gate headless de 200 frames passaram.
Dump 1920×1080 conferido visualmente e por pixels nas bordas/centro; arquivo
idêntico com a cadeia padrão e com dez efeitos habilitados. Flags inválidas
rejeitadas antes de iniciar GPU. HLSL tem os mesmos helpers do MSL, mas a
execução no Windows continua pendente.

**M0 está fechado; M1 DeckLink está em andamento** (Windows). FX-010
discovery foi implementado e aguarda validação de build e placa Windows.
Captura, playback, filas, timing e split de thread ainda não existem.

Pedido atual (2026-09-09), parte 5: linhas entre os parametros. Uma regua
(`ImGui::Separator`, cor `ImGuiCol_Separator` = `line` do tema) acima de cada
parametro, **nunca acima do primeiro de cada coluna** - duas linhas de
controle sem nada entre elas deixavam a trilha parecer do nome de baixo em vez
do de cima. O painel de loop passou a morar dentro do par de reguas do proprio
parametro, entao sumiu o separador especial que ele desenhava.

Duas contas de altura precisaram acompanhar, senao corta o ultimo controle:
`separatorHeight()` no `InspectorPanel.cpp` e o novo `parameterBlockHeight()`
no `UiLayer.cpp` (que substituiu `parameterRows()` - contar linhas e esquecer
as reguas foi o que cortou o slider de Speed no SOURCE). O SOURCE ja rolava
antes (conteudo 620 contra janela 318, medido com probe temporario); a reserva
do EFFECTS caiu de 240 para 180 porque ela foi dimensionada quando o EFFECTS
ainda carregava a secao PARAMETERS, e esse espaco devolvido e o que paga as
reguas. Speed e Motion Markers voltaram a aparecer.

Pedido atual (2026-09-09), parte 4: UI dos parametros. Cada linha virou uma
tira de rack de duas alturas: nome a esquerda, **valor como `DragFloat`/
`DragInt` na fonte mono** (arrasta para passo fino, Ctrl-clique para digitar)
e o **Loop como botao de glifo** com uma onda senoidal, preenchido de ciano
quando roda. A trilha embaixo carrega so a posicao na faixa mais um **tique no
valor padrao** (`drawDefaultTick`), com `ImGuiSliderFlags_NoInput` porque
Ctrl-clique num slider com formato vazio abriria uma caixa de texto vazia.
Booleano virou uma linha so, com o checkbox na coluna do valor. Casas decimais
saem da faixa (`%.1f` acima de span 20, `%.3f` abaixo de 2), entao
`Hold (s)` le `2.00` e nao `2.000`. Tooltip unificado: id, faixa, padrao e os
dois gestos que nada anuncia. `theme::Glyph::Loop` e um `active` novo em
`glyphButton`. Vale igual no sidebar (parametros de fonte) e no inspector,
porque os dois passam pelo mesmo `drawParameterRow`.

Pedido atual (2026-09-09), parte 3: calibrar melhor o enquadramento do
tracking, com grade de alinhamento que não vai para a saída.

**Offset X.** `FramingSettings::subjectOffsetX` novo: onde a pessoa fica na
horizontal, em frações da largura enquadrada a partir do centro (positivo =
direita do centro, então o recorte anda para a esquerda). É o espaço de olhar
que faltava — antes o assunto ficava soldado ao meio da imagem que sai.
Entrou no `settingsChanged` do `retarget`, então o slider fura a dead zone e
mira na hora, como Subject Size e Headroom. Sem Offset Y: brigaria com o
Headroom pelo mesmo eixo. Parâmetro `offset_x` no `AutoFrameEffect`, faixa
-0.40..0.40. Dois CTests novos em `tests/framing_test.cpp` (363 checks).

**Grade de alinhamento.** Terços + cruz no centro, desenhados por
`drawAlignmentGrid` no draw list da própria UI em cima do `ImGui::Image` — a
textura que vai para `OutputSurface::present()` e para a webcam nunca é
tocada, então a grade **não pode** chegar ao telão. Não é shader, não é nó da
cadeia e não é parâmetro do efeito (parâmetro que não muda pixel viraria
mentira no primeiro preset). Aparece só enquanto os parâmetros de um nó
`EffectRole::Framing` estão abertos (`ui::adjustingFraming()`), com checkbox
**Grid** no cabeçalho do inspector. No SOURCE ela é desenhada **dentro do
retângulo do recorte** (o que se compõe é a imagem que sai, não o sensor); no
FX e no PROGRAM, sobre a imagem. Verificado com padrão de teste em retrato: a
grade cai dentro da tira 9:16 e some quando nada está inspecionado.

Pedido atual (2026-09-09), parte 2: o enquadramento tinha parado de seguir.
Não era a mudança de UI — `VisionTracker::analyze` chamava
`publish(nullptr, false)` enquanto ninguém tivesse sido escolhido, então
`TrackingSnapshot::valid` era `false` todo ciclo, o `FramingController` ficava
sem assunto e o recorte não saía do lugar. Isso contrariava a regra do
`AGENTS.md` ("Vision auto-selects only until the first Pick") e o comentário
de `TrackingSnapshot::locked` ("False means the tracker is still
auto-choosing"), mas estava documentado em `docs/TRACKING.md` — conflito
resolvido a favor do comportamento que o operador pediu, e a doc foi alinhada.

Agora o tracker escolhe sozinho até o primeiro Pick, e a escolha é
**pegajosa**: maior caixa no primeiro quadro com gente, depois a mais próxima
de quem já está sendo seguido (`VisionTracker::chooseAutomatically`), para que
alguém atravessando o quadro não roube o plano — era isso que fazia o
auto-pick antigo parecer que não dava para mirar. Um Pick ou troca de câmera
esquece a escolha automática (`applyPendingTarget`); depois de um Pick,
`locked_` anula o caminho automático de vez. Status novo:
`following  ·  N in shot  ·  click SOURCE to choose`. `pickOwnsMonitor` no
`PreviewPanel.cpp` foi estreitado: só toma o monitor num Pick explícito ou
quando há gente em quadro e ninguém sendo seguido, senão o barramento FX
ficaria travado o show inteiro. Verificado ao vivo com a câmera.

Pedido atual (2026-09-09), parte 1: ver o efeito antes de mandar para o
output, e tirar os parâmetros do sidebar.

**Barramento de preview.** O monitor da esquerda virou seletor de duas
posições: `SOURCE` (câmera pré-cadeia, com as caixas do assunto e o Pick) e
`FX` (`UiFrameState::chainPreview` — a imagem da própria cadeia). O
`chainPreview` é capturado em `App::renderFrame` *antes* de
`ProgramOutput::render`, junto com o `framing` que o produziu, porque essa
chamada pode segurar a imagem (Freeze), trocar por preto (Black) e devolver o
enquadramento *segurado* por cima do vivo. Custo de GPU: zero — é a imagem que
a cadeia já produziu neste quadro. Com PROGRAM em Freeze ou Black a cadeia
continua preparando o próximo plano, e esse era o único quadro que o operador
não conseguia ver. Em `Clean` o look é rampado para fora da própria cadeia,
então o barramento FX mostra honestamente que não há look, e o monitor escreve
a mix.

**Inspector.** Os parâmetros saíram do sidebar — uma coluna de 392 px tinha
que carregar a lista da cadeia e treze sliders ao mesmo tempo. O painel largo
embaixo do preview tem duas caras: a faixa de stats (padrão) e os parâmetros
do efeito selecionado, distribuídos em até quatro colunas. Selecionar e
inspecionar são o mesmo ato: clicar a linha abre, clicar de novo fecha, e o X
do painel também. Dobrar EFFECTS ou abrir SOURCE/OUTPUT volta para os números
(`UiLayer::syncInspectorToSections` compara com o estado do quadro anterior).
`ui::validateInspector` roda antes do layout ler o ponteiro, porque a cadeia
pode perder um efeito entre quadros. `drawParameters` e
`drawParametersColumns` compartilham um renderer por parâmetro, então sidebar
e inspector não divergem. `theme::PanelSection::Parameters` deixou de existir.
Arquivos novos: `src/ui/Inspector.h`, `src/ui/InspectorPanel.cpp`.

Verificação ao vivo da câmera: `open build/bin/atem_fx.app --args --source
camera:<id>` — pelo bundle o TCC concede a câmera; o caminho
`./build/bin/atem_fx` cru é negado.

Pedido atual (2026-09-08): efeito `frame_delay` (Frame Delay) — cópias
atrasadas do quadro, o eco que um corpo em movimento deixa para trás. Não é
um delay multi-tap: o pass amostra duas texturas, então as cópias moram num
rastro só. A cada `spacing` quadros o quadro vivo é carimbado no rastro e o
que já estava lá desbota por um decay derivado de `copies` — um passo atrás
inteiro, dois passos atrás desbotado uma vez, e assim por diante. Mesma soma
de um delay multi-tap, a dois passes por quadro em vez de um pass e um alvo
1080p por cópia. Dois alvos persistentes em ping-pong (33 MB), porque um pass
não lê e escreve a mesma textura. `blend` escolhe quem vence na sobreposição
(Lighten/Screen para corpo claro em palco escuro, Darken para o inverso),
`key` decide o que deixa cópia, `freeze` congela o rastro sem limpá-lo. Nó
bypassado re-semeia ao voltar: ligar o efeito começa um eco novo, não solta
no ar o que ficou de antes. Sem mudança de RHI, chain ou UI — um shader por
backend, um `.cpp`, uma linha de registro. Medido em headless no M4: ~0,3 ms
de GPU sobre o passthrough.

Pedido anterior (2026-09-08): segurança da imagem — PROGRAM com quatro estados
(FX / Clean / Freeze / Black) num controle só, sempre visível no painel
OUTPUT, e política definida para queda de câmera. `ProgramOutput` em
`src/video/program_output.*` é política de saída, não efeito: tem alvos
persistentes próprios porque preto e hold precisam funcionar sem entrada
nenhuma. Saúde da fonte em `src/video/source_health.*`. CLI `--program`.
Dois CTests novos. Sem mudança de RHI, shader ou Auto Frame.

Pedido anterior (2026-09-08): seleção do assunto no tracking — modo Pick no
SOURCE (clique, lista, drag de região), `VNTrackObjectRequest` no macOS,
lost-lock sem trocar de pessoa. Helper `source_mapping` com CTest. Windows
continua sem detector. Sem mudança de RHI, shader ou Auto Frame.

Pedido anterior (2026-09-08): pacotes de distribuição unsigned —
`scripts/package_macos.sh` → `dist/CamVJ-<ver>-macos.dmg` (CamVJ.app +
Applications); `scripts/package_windows.ps1` → ZIP com CamVJ.exe + shaders.
Bundle id / binário macOS inalterados. Sem codesign/notarização nesta etapa.
Validação Windows do script pendente (precisa de máquina VS).

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
  canvas com `source_mapping` + `VideoSource::mapping()` (fit/mirror do
  `source_blit`). O clique do Pick usa o inverso do mesmo helper.
- Recorte custa resolução. `Max Zoom` é o controle que decide quanto.
- **Clean não é bypass geral**: `EffectChain::process` pula `EffectRole::Visual`
  e mantém `EffectRole::Framing`. O operador tira o look sem perder o plano
  nem mudar o formato 9:16 que o processador de LED recebe.
- Freeze e Black não param a máquina: captura, tracking e cadeia continuam
  preparando o próximo plano, e a saída segue apresentando todo frame — o
  telão vê imagem parada, nunca sinal morto. As automações continuam andando.
- Queda de entrada trava (*latch*) Freeze quando existe imagem válida. Três
  regras: perda **nunca** seleciona entrada (padrão de teste é escolha
  deliberada, jamais plano B); câmera que volta não sobe sozinha (só troca de
  modo explícita libera); e perda não sobrepõe um Black do operador.
- Sem primeiro quadro válido todo modo mostra preto — Freeze não inventa
  imagem que não tem. O quadro segurado carrega o próprio framing
  (`framing`/`framingActive`/`outputAspect`) para o preview PROGRAM descrever
  o que está no telão enquanto o tracking segue vivo atrás.
- Sem entrada rodando, o rescan mantém "nada selecionado": a combo não pode
  sugerir Test Pattern enquanto o PROGRAM segura preto.
- Pick no SOURCE escolhe o alvo **antes** de seguir: sem auto-lock na
  primeira pessoa. Clique/lista/drag no monitor; PROGRAM fica no plano
  aberto até o operador escolher. `VNTrackObjectRequest` depois do lock.
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
   SOURCE/PROGRAM estão implementados. Pick subject escolhe o alvo; falta
   validar com câmera real (duas pessoas, drag num objeto, lock perdido sem
   pular). Abertos: (a) validar com câmera real e Auto Frame ligado — caixas
   no SOURCE, PROGRAM 16:9 vs 9:16, Pick; (b) **não existe saída de vídeo
   no macOS** — levar a imagem até a ATEM ou o Resolume não está resolvido
   nem escopado.
1. Concluir testes de automação e gate macOS de 200 frames; conferir controles
   de loop na UI antes de chamar a extensão de concluída.
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
