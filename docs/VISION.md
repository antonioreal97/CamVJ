# ATEM FX — Original conception (historical)

**This is not a map of the repository.** It is the original product essay that
used to live in `README.md` (from the first heading onward), kept so the
rationale is not lost.

What is true in the tree today: M0 GPU engine, Metal + Direct3D 11, four
effects, ImGui, headless self-test. No DeckLink, no ATEM, no Glitch/VHS/Trails,
no presets.

Canonical documents:

- [PRODUCT.md](PRODUCT.md) — product goal vs M0
- [ARCHITECTURE.md](ARCHITECTURE.md) — structure that exists
- [RUNTIME.md](RUNTIME.md) — frame loop and file map
- [VIDEO_PIPELINE.md](VIDEO_PIPELINE.md) — M0 pipeline + M1 design
- [ATEM_INTEGRATION.md](ATEM_INTEGRATION.md) — M4 design
- [ROADMAP.md](ROADMAP.md) — milestone status
- [../AGENTS.md](../AGENTS.md) — engineering rules

The tree, stack and M0 prompt below are aspirational or obsolete. They
proposed a Win32/D3D11-only start; the engine shipped with Metal as well.
They proposed MP4 input; M0 uses a GPU test pattern. They list
`src/decklink/` and `src/atem/`; those directories do not exist.

---

Eu faria o projeto com o nome provisório **ATEM FX** e trataria o Cursor e o Codex como dois engenheiros trabalhando sobre a mesma especificação. O erro seria começar pedindo “crie um Resolume”. É grande demais. O certo é construir um **motor de vídeo em tempo real com integração ATEM**, incrementalmente.

## 1. Definição do produto

A primeira versão deve fazer somente isto:

```text
SDI IN
  ↓
DeckLink
  ↓
ATEM FX
  ↓
GPU
  ↓
1–3 shaders
  ↓
DeckLink
  ↓
SDI OUT
  ↓
ATEM INPUT
```

Com interface:

```text
┌──────────────────────────────────────────────────────┐
│ ATEM FX                              1080p59.94 ●    │
├──────────────┬───────────────────────────────────────┤
│ SOURCE       │                                       │
│              │              PREVIEW                  │
│ DeckLink 1   │                                       │
│              │                                       │
├──────────────┼───────────────────────────────────────┤
│ EFFECTS      │ PARAMETERS                            │
│              │                                       │
│ RGB Split    │ Amount ──────────●──                  │
│ Glitch       │ Speed  ──────●──────                  │
│ Trails       │ Mix    ────────●────                  │
│ Pixelate     │                                       │
│ Mirror       │                                       │
│ VHS          │                                       │
├──────────────┴───────────────────────────────────────┤
│ [1 CLEAN] [2 GLITCH] [3 TRAILS] [4 VHS] [5 CUSTOM] │
└──────────────────────────────────────────────────────┘
```

Nada de IA, layers complexos, partículas, NDI, streaming, editor de timeline ou oito câmeras na V1.

---

# 2. Stack que eu escolheria

Para esse projeto específico:

| Área          | Tecnologia                   |
| ------------- | ---------------------------- |
| Linguagem     | **C++20**                    |
| Build         | **CMake**                    |
| Windows       | Win32                        |
| GPU           | **Direct3D 11** inicialmente |
| Shaders       | HLSL                         |
| Interface     | **Dear ImGui**               |
| SDI           | Blackmagic DeckLink SDK      |
| Controle ATEM | Blackmagic ATEM SDK          |
| Áudio         | WASAPI                       |
| MIDI          | RtMidi                       |
| Configuração  | JSON                         |
| Logs          | spdlog                       |
| Testes        | Catch2                       |
| Profiling     | PIX / métricas internas      |

Eu escolheria **Direct3D 11 em vez de Vulkan** para a primeira versão.

Vulkan seria arquitetonicamente elegante, mas adicionaria muita complexidade que não agrega valor ao MVP.

Também **não usaria Electron, React ou Python no motor de vídeo**.

Podemos ter outras tecnologias posteriormente para controle remoto, mas o pipeline crítico deve ficar em C++.

---

# 3. Estrutura do repositório

Eu mandaria Cursor/Codex trabalharem em algo assim:

```text
atem-fx/
│
├── AGENTS.md
├── README.md
├── CMakeLists.txt
│
├── docs/
│   ├── PRODUCT.md
│   ├── ARCHITECTURE.md
│   ├── VIDEO_PIPELINE.md
│   ├── ATEM_INTEGRATION.md
│   ├── EFFECT_SYSTEM.md
│   └── ROADMAP.md
│
├── src/
│   ├── app/
│   │   ├── App.cpp
│   │   └── App.h
│   │
│   ├── video/
│   │   ├── VideoFrame.h
│   │   ├── VideoPipeline.cpp
│   │   ├── FrameQueue.cpp
│   │   └── FrameTiming.cpp
│   │
│   ├── decklink/
│   │   ├── DeckLinkInput.cpp
│   │   ├── DeckLinkOutput.cpp
│   │   └── DeckLinkDeviceManager.cpp
│   │
│   ├── gpu/
│   │   ├── Renderer.cpp
│   │   ├── Texture.cpp
│   │   ├── Shader.cpp
│   │   └── EffectGraph.cpp
│   │
│   ├── effects/
│   │   ├── RgbSplit.cpp
│   │   ├── Pixelate.cpp
│   │   ├── Mirror.cpp
│   │   ├── Glitch.cpp
│   │   └── Feedback.cpp
│   │
│   ├── atem/
│   │   ├── AtemClient.cpp
│   │   └── AtemState.cpp
│   │
│   ├── audio/
│   │
│   ├── midi/
│   │
│   └── ui/
│       ├── MainWindow.cpp
│       ├── PreviewPanel.cpp
│       ├── EffectsPanel.cpp
│       └── PresetPanel.cpp
│
├── shaders/
│   ├── passthrough.hlsl
│   ├── rgb_split.hlsl
│   ├── pixelate.hlsl
│   ├── mirror.hlsl
│   └── glitch.hlsl
│
├── tests/
│
└── assets/
```

Isso é importante porque impede os agentes de criarem um monolito do tipo:

```text
main.cpp
5000 linhas
```

---

# 4. O princípio arquitetônico mais importante

Separe completamente:

```text
VIDEO INPUT
     │
     ▼
VIDEO ENGINE
     │
     ▼
EFFECT GRAPH
     │
     ▼
VIDEO OUTPUT
```

de:

```text
ATEM CONTROL
UI
MIDI
PRESETS
AUDIO
```

Ou seja:

```text
               ┌──────── UI
               │
               ├──────── MIDI
               │
               ├──────── ATEM SDK
               │
               ▼
           PARAMETERS
               │
               ▼

DeckLink → GPU → EFFECT GRAPH → GPU → DeckLink
```

A ATEM **controla o comportamento**, mas não faz parte do pipeline interno de pixels.

Isso vai evitar muita dor depois.

---

# 5. Roadmap

Eu dividiria o desenvolvimento em seis marcos.

| Marco | Resultado                                |
| ----- | ---------------------------------------- |
| M0    | Motor GPU funcionando com vídeo de teste |
| M1    | DeckLink IN → GPU → DeckLink OUT         |
| M2    | Sistema modular de efeitos               |
| M3    | Interface + presets                      |
| M4    | Integração ATEM                          |
| M5    | MIDI + audio reactive                    |
| M6    | Fill/Key e recursos avançados            |

### M0 — Video Engine

Ainda sem ATEM e sem DeckLink.

Usar um vídeo MP4 ou até padrão de teste:

```text
MP4
 ↓
Texture
 ↓
Shader
 ↓
Preview
```

Implementar somente:

```text
Passthrough
RGB Split
Pixelate
Mirror
```

Meta:

**1080p60 estável.**

---

# 6. M1 é o verdadeiro teste do projeto

Depois:

```text
SDI
 ↓
DeckLink Input
 ↓
VideoFrame
 ↓
GPU Texture
 ↓
Shader
 ↓
GPU Texture
 ↓
DeckLink Output
 ↓
SDI
```

Neste ponto não precisamos nem de uma interface bonita.

Algo assim basta:

```text
ATEM FX ENGINE

DeckLink input: OK
DeckLink output: OK

1920x1080
59.94 fps

Frames:
Input     293829
Processed 293829
Output    293829
Dropped   0

GPU: 3.2ms
```

Esse marco vale mais do que metade do aplicativo.

---

# 7. Meta técnica importante

Eu colocaria no `PRODUCT.md`:

```text
Primary target

1920x1080
59.94 fps

Target:
0 dropped frames under normal operation

Processing budget:
< 16.68 ms per frame

Preferred GPU processing:
< 8 ms

Latency target:
as low as technically practical,
initial engineering goal <= 3 video frames
excluding ATEM processing.
```

Não transforme `3 frames` em promessa comercial.

É meta de engenharia.

---

# 8. Effect Graph

Essa parte será o coração do nosso “Resolume”.

Não faça:

```cpp
if (rgbSplit) ...
if (glitch) ...
if (pixelate) ...
```

Faça um sistema:

```text
INPUT
 ↓
EFFECT NODE
 ↓
EFFECT NODE
 ↓
EFFECT NODE
 ↓
OUTPUT
```

Por exemplo:

```text
Camera
 ↓
RGB Split
amount = .15
 ↓
Glitch
amount = .4
 ↓
Feedback
mix = .2
 ↓
Output
```

Cada efeito deve possuir algo conceitualmente como:

```cpp
Effect
{
    name
    enabled

    shader
    parameters

    initialize()
    process()
    shutdown()
}
```

Assim futuramente podemos ter:

```text
30 efeitos
```

sem reconstruir o engine.

---

# 9. Feedback precisa ser previsto desde cedo

Esse efeito:

```text
FRAME N
   +
FRAME N-1
   ↓
FEEDBACK
```

exige buffers persistentes.

Por isso nosso engine deve aceitar:

```text
Texture A
Texture B
History Texture
```

Não faça toda a arquitetura supondo que:

```text
input → shader → output
```

é sempre suficiente.

---

# 10. Depois entra a ATEM

Quando M0–M3 estiverem sólidos:

```text
ATEM SDK
 ↓
AtemClient
 ↓
AtemState

ProgramInput
PreviewInput
TransitionState
Keyers
```

O nosso programa poderia mostrar:

```text
PROGRAM
CAM 1

PREVIEW
CAM 4
```

E reagir a essas informações.

---

# 11. Aqui nasce o FX BUS

Essa é uma das funções que eu acho que pode diferenciar bastante o projeto.

Interface:

```text
PROGRAM
CAM 2

PREVIEW
CAM 3

FX BUS
CAM 3
```

Botão:

```text
SEND PREVIEW TO FX
```

O sistema então configuraria o AUX da ATEM:

```text
CAM 3
 ↓
ATEM AUX
 ↓
DeckLink IN
 ↓
ATEM FX
 ↓
DeckLink OUT
 ↓
ATEM INPUT 8
```

Resultado:

```text
CAM 3

normal:
INPUT 3

processada:
INPUT 8
```

---

# 12. Depois podemos criar FX TAKE

Imagine:

```text
[ FX TAKE ]
```

Ao apertar:

```text
1. Detectar Preview = CAM 3

2. AUX → CAM 3

3. esperar retorno válido

4. selecionar FX INPUT

5. AUTO
```

Isso seria espetacular para live.

---

# 13. Presets

Formato simples:

```json
{
  "name": "Cyber Glitch",
  "effects": [
    {
      "type": "rgb_split",
      "enabled": true,
      "amount": 0.23
    },
    {
      "type": "glitch",
      "enabled": true,
      "amount": 0.42,
      "speed": 1.4
    }
  ]
}
```

A UI carregaria:

```text
1 CLEAN
2 RGB
3 GLITCH
4 DREAM
5 VHS
6 CHAOS
```

---

# 14. Como eu dividiria Cursor e Codex

Aqui eu faria uma distinção importante.

### Cursor

Usaria principalmente para desenvolvimento **interativo**:

```text
UI
shaders
debug
hardware
profiling
experimentos
mudanças pequenas
```

Quando você estiver com a DeckLink instalada e precisar dizer:

> está dando tela preta

ou:

> agora tem imagem mas está piscando

o Cursor será extremamente útil porque você estará trabalhando junto dele no código.

### Codex

Eu usaria para tarefas fechadas como:

```text
implementar uma classe
refatorar um módulo
criar testes
analisar thread safety
revisar memory lifetime
documentar
implementar um novo shader
```

Principalmente tarefas que tenham critério claro de conclusão.

---

# 15. Regra fundamental para os dois

Crie:

```text
AGENTS.md
```

na raiz.

Ele vira a constituição do projeto.

Eu colocaria algo parecido com:

```md
# ATEM FX Engineering Rules

ATEM FX is a Windows real-time video effects engine.

Primary target:
1920x1080 59.94fps.

Technology:
C++20
CMake
Direct3D 11
Dear ImGui
DeckLink SDK
ATEM SDK

Priorities:

1. Video stability
2. Low latency
3. No dropped frames
4. Thread safety
5. GPU-first processing
6. Maintainable architecture

Never perform expensive image processing on the CPU when
the operation can reasonably be performed by the GPU.

Video capture, GPU rendering and video output must remain
decoupled modules.

Do not add external dependencies without documenting why.

Do not redesign unrelated parts of the architecture while
implementing an isolated feature.

All new effects must use the Effect abstraction.

Every hardware integration must provide useful diagnostic
logging.

Prefer small classes and explicit ownership.

Real-time video threads must never be blocked by UI work,
network calls or disk IO.
```

Eu também replicaria essas regras nas **Cursor Rules**.

---

# 16. Um arquivo ainda mais importante

Crie:

```text
docs/ARCHITECTURE.md
```

Começando com:

```text
DeckLink Capture Thread
        │
        ▼
Frame Queue
        │
        ▼
GPU Processing Thread
        │
        ▼
Effect Graph
        │
        ▼
Output Queue
        │
        ▼
DeckLink Playback
```

E separadamente:

```text
UI Thread
ATEM Thread
MIDI Thread
Audio Analysis Thread
```

Nenhum deles deve poder travar:

```text
Capture
Processing
Output
```

---

# 17. Não deixe o agente começar pelo hardware

Eu começaria com este primeiro prompt no Cursor/Codex:

Estamos iniciando um projeto chamado ATEM FX.

O objetivo é desenvolver um aplicativo Windows de processamento de vídeo em tempo real voltado para transmissões ao vivo com switchers Blackmagic ATEM.

O software deverá futuramente receber vídeo através de hardware Blackmagic DeckLink, processar os frames na GPU usando efeitos gráficos baseados em shaders e devolver o vídeo processado através de uma saída DeckLink.

A integração com o ATEM SDK será adicionada posteriormente para controlar roteamento, AUX, Program, Preview e automações de efeitos.

Não implemente DeckLink nem ATEM nesta primeira etapa.

Primeiro precisamos construir e validar o núcleo gráfico do sistema.

Stack inicial:

C++20
CMake
Win32
Direct3D 11
Dear ImGui
HLSL

Implemente uma aplicação mínima contendo:

uma janela principal;
inicialização correta do Direct3D 11;
render loop;
preview de uma textura de vídeo ou padrão animado de teste;
sistema inicial de shaders;
efeito passthrough;
efeito RGB Split;
efeito Pixelate;
interface ImGui para ativar/desativar efeitos e modificar seus parâmetros;
contador de FPS;
medição de frame time.

Projete desde o início uma abstração Effect que permita adicionar novos efeitos posteriormente sem alterar o pipeline principal.

Não crie ainda funcionalidades de áudio, MIDI, ATEM, DeckLink, gravação ou streaming.

Antes de escrever código, leia AGENTS.md e docs/ARCHITECTURE.md.

Caso a arquitetura proposta entre em conflito com esses documentos, os documentos têm precedência.

Prioridades:

estabilidade;
arquitetura modular;
processamento GPU;
baixa latência;
código legível.

O primeiro milestone estará concluído quando a aplicação puder renderizar continuamente uma fonte de teste em 1920x1080 a 60 fps e aplicar RGB Split e Pixelate em tempo real através da GPU.

Esse seria literalmente o **primeiro trabalho**.

---

# 18. E depois um prompt por issue

Evite:

> Continue desenvolvendo o ATEM FX.

Prefira:

```text
TASK ATEMFX-014

Implement DeckLink device enumeration.

Scope:
DeckLinkDeviceManager only.

Requirements:
- enumerate installed devices
- expose device name
- expose available input/output connectors
- log enumeration results
- return errors without crashing

Do not:
- implement capture
- implement playback
- modify renderer

Acceptance:
application lists available DeckLink devices.
```

Isso muda completamente a qualidade do código gerado por agentes.

---

# 19. Eu criaria um backlog assim

| Issue  | Função                |
| ------ | --------------------- |
| FX-001 | App skeleton          |
| FX-002 | D3D renderer          |
| FX-003 | Video texture         |
| FX-004 | Effect interface      |
| FX-005 | RGB Split             |
| FX-006 | Pixelate              |
| FX-007 | Effect Graph          |
| FX-008 | Feedback buffer       |
| FX-009 | Presets               |
| FX-010 | DeckLink discovery    |
| FX-011 | DeckLink capture      |
| FX-012 | DeckLink playback     |
| FX-013 | Frame queues          |
| FX-014 | Video timing          |
| FX-015 | ATEM discovery        |
| FX-016 | Program/Preview state |
| FX-017 | AUX control           |
| FX-018 | FX Bus                |
| FX-019 | MIDI                  |
| FX-020 | Audio analysis        |

Essa sequência também evita que Cursor/Codex façam coisas na ordem errada.

---

# 20. A V1 que eu buscaria

Quando pudermos fazer:

```text
CAMERA
  ↓
ATEM AUX
  ↓
DeckLink
  ↓
ATEM FX

[RGB]
[GLITCH]
[TRAIL]
[VHS]
[PIXEL]
[MIRROR]

  ↓
DeckLink
  ↓
ATEM INPUT 8
```

com:

```text
1080p59.94
estável

PREVIEW
PROGRAM

presets

FX TAKE
```

eu consideraria que já temos um **produto de verdade**, não apenas um experimento.

Depois dessa base podemos entrar no que seria o **ATEM FX 2**:

```text
multi-layer
blend modes
audio reactive
MIDI
LUT
particles
generative visuals
Fill + Key
AI segmentation
person/background FX
transitions
OSC
Stream Deck
remote control
```

Mas eu deliberadamente deixaria tudo isso fora da primeira versão.

### O próximo passo que eu faria

Criaria agora **quatro documentos completos para colocar diretamente no repositório**:

```text
AGENTS.md
PRODUCT.md
ARCHITECTURE.md
ROADMAP.md
```

e junto deles um backlog de aproximadamente **20–30 issues já escritas no formato ideal para serem entregues uma por vez ao Cursor ou ao Codex**.

Essa provavelmente é a melhor maneira de começar o projeto sem deixar os agentes transformarem a ideia em um código gigantesco e difícil de manter.
