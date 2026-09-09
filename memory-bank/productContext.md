# Product context

## Por que existe

Em live com ATEM, tratar uma câmera (glitch, pixelate, split) costuma exigir
um box de FX, um PC com software de VJ, ou um DVE limitado. CamVJ é um
motor **estreito**: uma entrada, uma cadeia de shaders, uma saída, controlado
pelo mesmo operador do mixer.

Não é Resolume. Não é editor. Não substitui a ATEM.

## Como deve funcionar (V1)

1. Câmera entra no mixer.
2. AUX manda essa câmera para o DeckLink IN.
3. CamVJ processa na GPU.
4. DeckLink OUT devolve para uma entrada (ex.: INPUT 8).
5. O operador corta CAM n (clean) vs INPUT 8 (tratado).
6. **FX Bus / FX TAKE** automatizam “manda o Preview pro FX e dá AUTO”.

A ATEM controla comportamento. Nunca faz parte do caminho de pixels.

## O que o operador vê hoje (M0)

Janela 1600×900, processamento 1920×1080, identidade CamVJ (luz sobre preto):

- **Header** — marca, resolução, backend, fps, tally LIVE/IDLE da saída
- **Source** — entrada, tracking (âmbar Tungsten quando o assunto está travado)
- **Output** — tela de envio; Stop em magenta quando está no ar
- **Effects** — add/remove/reorder/enable; sliders genéricos
- **Preview** — SOURCE (ciano, caixas do assunto/recorte) | PROGRAM (magenta; LIVE se a saída está enviando)
- **Stats** — rate, GPU ms, engine (vsync, reload), frametime

Não há presets, botões 1–5, Program/Preview de mixer, nem seletor de DeckLink.

M1 começou com `--list-decklink`, um comando de diagnóstico separado que
lista nomes, capacidades e conexões de vídeo suportadas. Não mostra sinal
conectado nem inicia captura/saída. A implementação com SDK Windows ainda
aguarda validação de build e placa; o preview continua usando a fonte de teste.

Efeitos reais: Passthrough, RGB Split, Pixelate, FM Raster, Subpixel,
Shutter, VHS, CRT, Mirror, Auto Frame. Shutter cobre o que a visão chamava de
Trails e VHS cobre o look de fita; Glitch continua só na visão
(`docs/VISION.md`), fora do binário.

## Experiência que importa

- Estabilidade acima de beleza. Frame dropado não embarca.
- Latência baixa o bastante para o talento não notar o tratado atrasado.
- UI genérica: efeito novo não pede painel custom.
- Diagnóstico barulhento em hardware. Falha silenciosa é inaceitável.

## Hoje vs V1

| | Pipeline atual (M0) | V1 |
| --- | --- | --- |
| Fonte | Test pattern GPU | DeckLink capture |
| Saída | Preview / PPM | DeckLink playback |
| Threads | Uma (UI + process) | Capture / GPU / Output separados |
| Controle | Mouse na UI | UI + ATEM (AUX, FX TAKE) + presets |
