# CamVJ — identidade visual

## A ideia

As quatro imagens de referência têm uma coisa em comum: **luz sobre preto, e luz
que se parte.** A cadeira vazia sob o refletor, o palco estourado no meio da
multidão, a cadeira de plástico virada em ladrilhos de espelho, o violonista
desmanchado em rastros de cor. Nenhuma delas é sobre o objeto — todas são sobre
o que a luz faz com ele.

É exatamente o que o software faz. A marca não desenha uma câmera nem um
switcher: desenha **o quadro e a aberração cromática**.

- **Os quatro cantos** são o recorte do Auto Frame. É o gesto que qualquer
  operador de câmera reconhece.
- **O ponto âmbar** é o sujeito travado. Ele fica **8 unidades acima do centro
  geométrico** — não é descuido, é a regra de headroom do Auto Frame desenhada
  dentro do próprio símbolo.
- **A franja ciano/magenta** é o `rgb_split`, deslocado ±1,15u a 26°. A marca
  está rodando o efeito nela mesma.

O nome ajuda: **Cam** é a disciplina do enquadramento, **VJ** é a performance
ao vivo. O símbolo carrega o primeiro, a franja cromática carrega o segundo. A
cadeira vazia da referência 1 é o argumento comercial do produto — o
enquadramento acontece sem ninguém pilotando o corte. A marca é o quadro que
sobrou quando o operador saiu.

## Sistema

**Tipografia.** Big Shoulders Bold no logotipo — condensada, industrial,
linguagem de etiqueta de rack e case de estrada. A caixa mista é obrigatória:
`CamVJ`, nunca `CAMVJ` nem `camvj`. O `VJ` em maiúsculas é o que separa as duas
metades do nome. Geist Mono na linha técnica e em toda a rotulagem secundária.

**Cores.**

| Nome | Hex | Uso |
| ---- | --- | --- |
| Studio Black | `#0A0B0D` | Fundo padrão. A marca nasceu para o escuro. |
| Key Light | `#F2F4F7` | Traço principal. |
| Tungsten | `#FF9B3D` | Só o sujeito. Nunca o traço, nunca o logotipo. |
| Split Cyan | `#17A9E0` | Franja esquerda. |
| Split Magenta | `#E62466` | Franja direita. |
| Rack Grey | `#6B7280` | Linha técnica, rótulos. |

## Regras de uso

1. **Área de proteção:** uma unidade de braço (22u) em volta de tudo.
2. **A franja cromática é opcional e some abaixo de ~32 px.** Acima disso ela é
   a versão preferida; abaixo, use a mono.
3. **Abaixo de ~20 px, tire o ponto âmbar.** Ele vira sujeira. Use
   `camvj-glyph-mono.svg`.
4. **O âmbar não migra.** O ponto é sempre âmbar; o traço é sempre branco ou
   preto. Nunca inverta, nunca pinte o logotipo de âmbar.
5. **Nada de contorno, sombra, gradiente ou brilho.** O contraste com o fundo já
   é o efeito.
6. **Em fundo claro** use `camvj-logo-horizontal-light-bg.svg`, sem franja.
7. **A linha `LIVE VIDEO FX ENGINE` é opcional.** Some em favicon, ícone de app,
   e em qualquer aplicação abaixo de 200 px de largura.

## Arquivos

| Arquivo | Para quê |
| ------- | -------- |
| `camvj-logo-horizontal.svg` | Marca principal, fundo escuro, com split |
| `camvj-logo-horizontal-dark-bg.svg` | Mesma marca, mono branca |
| `camvj-logo-horizontal-light-bg.svg` | Fundo claro |
| `camvj-logo-vertical.svg` | Lockup empilhado (splash, README centralizado) |
| `camvj-icon-512.svg` / `.png` / `1024.png` | Ícone de app, tile arredondado |
| `camvj-glyph-mono.svg` | Só o símbolo, `currentColor` — barra de menu, favicon, UI |
| `camvj-logo-horizontal.png` | Bitmap 2400 px, fundo transparente |
| `camvj-identidade.png` | Esta proposta em uma folha |

O `camvj-glyph-mono.svg` usa `fill="currentColor"`, então herda a cor do CSS ou
do tema do ImGui — dá para colocá-lo direto na barra de título do app sem gerar
variante nova.

## Nota

Como ATEM é marca registrada da Blackmagic Design, adotar CamVJ como nome do
produto resolve o problema antes de ele existir. Vale acertar o README nesse
ponto: hoje ele traz ATEM FX como nome do produto e CamVJ apenas como nome do
repositório.
