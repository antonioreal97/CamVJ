# Project brief — CamVJ

Motor de efeitos de vídeo em tempo real que senta **ao lado** de um switcher
Blackmagic ATEM: uma câmera sai por AUX, é processada na GPU e volta numa
entrada livre como versão tratada da mesma câmera.

```text
CAMERA → ATEM AUX → DeckLink IN → CamVJ → DeckLink OUT → ATEM INPUT
```

O operador corta entre o clean e o tratado como qualquer outra fonte.

## Meta técnica

- 1920×1080, 59,94 fps
- 0 dropped frames em operação normal
- Orçamento de processamento < 16,68 ms/frame (preferência GPU < 8 ms)
- Latência ≤ 3 frames de vídeo, excluindo o processamento da ATEM
  (meta de engenharia, não promessa comercial)

## Plataformas

- **Windows** — produção. Única com DeckLink SDK e ATEM SDK.
- **macOS** — desenvolvimento e demonstração. Metal. O núcleo M0 foi
  escrito e validado aqui.

Linux não é suportado.

## V1 está pronta quando

O operador roteia uma câmera pelo CamVJ e devolve ao mixer em 1080p59.94,
ajusta efeitos ao vivo, chama presets e aperta **FX TAKE**, com Program/Preview
estáveis e sem dropped frames.

## Fora de escopo da V1

IA, stacks de layers, partículas, NDI, streaming, gravação, timeline, oito
câmeras simultâneas, blend modes, LUTs, OSC, Stream Deck.

## Estado neste repositório

**M0 feito; M1 em andamento.** Núcleo GPU, dois backends, padrão de teste e
câmeras atrás de `VideoSource`, 11 efeitos, automação por parâmetro, tracking
com Vision no macOS, saída para tela, PROGRAM (FX / Clean / Freeze / Black),
webcam virtual no macOS, ImGui e headless. FX-010 discovery DeckLink foi
implementado como comando separado `--list-decklink`, com SDK opcional no
Windows e stub nos demais builds. Build e placa Windows ainda precisam ser
validados antes de marcar FX-010 done. Captura/saída DeckLink e ATEM não foram
implementados.

Versão: **1.0.0** — origem única em `project(AtemFx VERSION ...)` no
`CMakeLists.txt`, mesma da tag `v1.0.0`. Procedimento em
`docs/BUILD.md#versioning`.

Documentos canônicos (inglês): `AGENTS.md`, `docs/ARCHITECTURE.md`,
`docs/RUNTIME.md`, `docs/PRODUCT.md`, `docs/ROADMAP.md`, `docs/BUILD.md`.
O nome do produto é **CamVJ**; o binário e o namespace continuam `atem_fx` /
`atemfx`.
Este memory-bank é a memória de trabalho em PT-BR.
