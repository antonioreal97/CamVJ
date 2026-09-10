<p align="center">
  <img src="assets/files/camvj-logo-horizontal.svg" alt="CamVJ" width="320">
</p>

# CamVJ

Motor de efeitos de vídeo ao vivo. Uma câmera entra, a GPU trata, o quadro
sai para um painel de LED, um projetor ou — no Windows, a partir do M1 — de
volta a um switcher Blackmagic ATEM.

**Estado: M0 feito; M1 em andamento.** Núcleo GPU com dois backends atrás de
uma interface comum — **Metal (macOS)** e **Direct3D 11 (Windows)** — em
1920×1080, onze efeitos, UI ImGui, self-test headless. As entradas de vídeo
ficam atrás de `VideoSource`: padrão de teste, webcam interna e câmeras USB
(no macOS 14+ também iPhone via Continuity). É essa a interface que o DeckLink
vai implementar no M1.

O primeiro passo do M1 é a descoberta de dispositivos DeckLink por comando,
implementada e aguardando validação de build e placa no Windows. Ainda
**não há** captura/saída SDI, ATEM, presets, MIDI nem áudio. Windows é a
plataforma de produção (SDKs da Blackmagic). macOS é desenvolvimento e
demonstração.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
./build/bin/atem_fx                                      # app com interface
./build/bin/atem_fx --headless --frames 200              # gate de toda mudança
./build/bin/atem_fx --headless --frames 300 --dump f.ppm
./build/bin/atem_fx --list-sources                       # entradas de vídeo
./build/bin/atem_fx --list-displays                      # telas de saída
./build/bin/atem_fx --output 2                           # manda o programa para a tela 2
./build/bin/atem_fx --check-shaders                      # compila todos os shaders
./build/bin/atem_fx --version                            # qual build está aqui
```

No macOS o binário fica em `build/bin/atem_fx.app` (o bundle é o que permite o
acesso à câmera); `build/bin/atem_fx` é um symlink para dentro dele.

Windows:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\bin\Release\atem_fx.exe
```

## Baixar

Por enquanto só há build **macOS**. O pacote Windows (`.zip`) vem depois.
Arquivos nos [Releases do GitHub](https://github.com/antonioreal97/CamVJ/releases)
(pacotes **não assinados** nesta etapa).

| Sistema | Status | Arquivo | O que fazer |
| ------- | ------ | ------- | ----------- |
| **macOS** (Apple Silicon, 11+) | Disponível | `CamVJ-<versão>-macos-arm64.dmg` | Abrir o DMG → arrastar **CamVJ** para **Applications** → abrir o app |
| **Windows** (x64, 10 1703+) | Em breve | `CamVJ-<versão>-windows-x64.zip` | Extrair a pasta → abrir `CamVJ/CamVJ.exe` |

Exemplo atual: `CamVJ-1.0.0-macos-arm64.dmg`. A build publicada hoje é
`arm64`; o projeto também pode ser compilado localmente em um Mac Intel, mas
esse binário ainda não é distribuído e o pacote não é universal.

**macOS.** Na primeira abertura, se o Gatekeeper bloquear: clique direito no
app → **Abrir**. Autorize a câmera quando pedido (ou em **Ajustes do Sistema ›
Privacidade e Segurança › Câmera**).

Quando o ZIP Windows existir: se o SmartScreen avisar, use **Mais informações**
→ **Executar assim mesmo** (só se você confiar na build). A pasta `shaders/`
precisa ficar ao lado do `CamVJ.exe`.

Quem gera o DMG a partir do código (Mac, após build Release):

```bash
./scripts/package_macos.sh
```

A versão sai do `project(... VERSION ...)` do `CMakeLists.txt` — hoje
**1.0.0**, a mesma da tag `v1.0.0` e do release. `-v` só serve para gerar um
pacote fora dessa numeração.

Para saber qual build está numa máquina, sem abrir o app:

```bash
./build/bin/atem_fx --version      # CamVJ 1.0.0 (Metal)
```

O mesmo número aparece no cabeçalho da interface (ao lado da marca) e na
primeira linha do log — é o que se lê depois do show, no arquivo.

O script Windows (`scripts/package_windows.ps1`) já existe; falta só rodar o
build Release numa máquina com Visual Studio. Detalhes em
[docs/BUILD.md](docs/BUILD.md#distribution-packages).

## O que o M0 faz

```text
TestPatternSource (GPU) → EffectChain → ProgramOutput → Preview ImGui ou dump PPM
                          câmera                      → tela de saída (HDMI/DP)
```

Efeitos: `passthrough`, `rgb_split`, `pixelate`, `fm_raster`, `subpixel`,
`shutter`, `frame_delay`, `vhs`, `crt`, `mirror` e `auto_frame` (HLSL + MSL).
Processamento sempre em 1920×1080, independente do tamanho da janela.
Orçamento: 16,68 ms/frame (59,94 fps). A taxa não é travada no display.

CLI: `--headless`, `--frames N`, `--dump PATH`, `--enable a,b,c`,
`--no-vsync`, `--source ID`, `--pattern NAME`, `--list-sources`, `--output ID`,
`--list-displays`, `--webcam`, `--program MODE`, `--list-decklink`,
`--version`, `--help`.
Detalhes em [docs/BUILD.md](docs/BUILD.md).

## Pattern para mapear painéis de LED

Em **SOURCE**, escolha **Test Pattern** e, em **Pattern**, selecione
**LED Mapping (16:9 + 9:16)**. Em **OUTPUT**, escolha a tela que alimenta o
processador de LED. Com PROGRAM em **FX** ou **Clean**, as guias fazem parte
da imagem enviada à tela e à webcam virtual.

O contorno ciano delimita o **16:9 inteiro (1920×1080)**; o magenta delimita
o **9:16 centralizado**, na mesma posição usada pelo Auto Frame em retrato.
A grade quadrada, o centro e as marcas de borda ajudam a conferir proporção,
alinhamento e cortes. O canvas continua em 1920×1080: a faixa 9:16 mede
607,5×1080, entre x=656,25 e x=1263,75.

Esse padrão é estático e ignora os efeitos e o Auto Frame para
preservar as medidas. **Freeze** e **Black** continuam valendo. Ao voltar a
Colour Bars, Plasma, Grid ou câmera, a cadeia retoma os ajustes existentes.
Speed e Motion Markers se aplicam aos três padrões animados.

```bash
./build/bin/atem_fx --headless --pattern led-mapping --frames 200 --dump led-mapping.ppm
```

## Efeitos em loop

Cada linha de parâmetro tem o nome à esquerda, o **valor** à direita e a
**trilha** embaixo. O valor não é só um número: arraste em cima dele para
passos finos, ou dê **Ctrl-clique** para digitar um valor exato — a trilha
sozinha gasta um pixel por 1/250 da faixa, o que não serve para calibrar. Na
trilha há um **risco fino no valor padrão**, para você ver o quanto se afastou;
**clique direito** em qualquer um dos dois volta ao padrão. Uma linha separa
um parâmetro do seguinte, para não restar dúvida sobre qual trilha pertence a
qual nome.

Cada parâmetro pode ter seu próprio loop. Em **EFFECTS**, clique no efeito —
os parâmetros dele abrem no painel largo embaixo do preview — e clique no
**botão de onda** ao lado do valor do parâmetro que deseja animar. Ele acende
em ciano enquanto o loop roda, e o valor passa a mostrar o que o loop está
fazendo. Abra **Loop settings** para escolher a onda em **Shape**, os valores mínimo e
máximo, a duração do ciclo em segundos e a fase em **Phase (%)**. A curva
mostra o movimento configurado.

Por exemplo, no **RGB Split**, configure **Amount** com onda **Sine**, mínimo
`0`, máximo `0.3` e **Cycle (s)** em `4`: a separação de cores aumenta e diminui
continuamente a cada quatro segundos. **Angle** pode ter outro loop, com
**Ramp up**, de `0` a `360`, para girar a direção. Parâmetros inteiros avançam
em valores inteiros e opções de ligar/desligar também podem ser automatizadas.

Use **Pause/Resume** para congelar ou continuar um loop e **Restart** para
recomeçar o ciclo na fase configurada. Desativar **Loop** recupera o valor
manual anterior; ativá-lo novamente retoma a fase. O reset do parâmetro, com
clique direito no controle, restaura o padrão e remove seu loop.
Enquanto o loop está ativo, o controle mostra o valor animado e o ajuste
manual fica desabilitado.

Os loops continuam quando você seleciona outro efeito, reorganiza os nós ou
desliga o efeito na cadeia. Instâncias do mesmo efeito têm controles
independentes. As configurações valem apenas para a sessão e são perdidas ao
encerrar o aplicativo; salvá-las em presets continua previsto para M3.

## Enquadramento automático (Auto Frame)

Segue uma pessoa ou objeto e mantém o enquadramento, para mandar uma câmera de
palco a um painel de LED sem alguém pilotando o corte. Em **EFFECTS**, adicione
**Auto Frame**. Com **Follow Subject** ligado, o efeito recorta e move o
recorte sozinho; desligado, ele vira um crop manual com **Manual Zoom**,
**Manual X** e **Manual Y**.

Os controles são de operador de câmera, não de algoritmo:

| Controle | O que faz |
| -------- | --------- |
| Subject Size | Quanto da altura do quadro a pessoa ocupa. Maior é mais fechado. |
| Headroom | Espaço acima da cabeça. É o ajuste vertical: aumentar desce a pessoa no quadro. |
| Offset X | Onde a pessoa fica na horizontal, em frações da largura do quadro. Positivo joga ela para a direita do centro — o espaço de olhar de quem fala virado para o palco. |
| Dead Zone | Quanto ela pode andar antes do quadro se mexer. **É o que impede a imagem de tremer.** |
| Smoothing (s) | Tempo para o quadro alcançar o alvo. |
| Max Speed | Velocidade máxima do movimento. |
| Hold (s) | Quanto tempo segura o quadro quando perde a pessoa. |
| Return (s) | Tempo para abrir de volta ao quadro cheio depois disso. |
| Max Zoom | Limite de aproximação. |

### Grade de alinhamento

Enquanto os parâmetros do **Auto Frame** estão abertos, os monitores mostram
uma grade de terços com uma cruz no centro, para você calibrar o enquadramento
sem chutar. No SOURCE ela é desenhada **dentro do retângulo do recorte** — o
que você está compondo é a imagem que sai, não o sensor inteiro — e no PROGRAM
sobre a imagem.

A grade **nunca vai para a saída**. Ela é desenhada pela interface por cima do
preview; a textura que vai para o telão e para a webcam virtual é a que a
cadeia produziu, sem grade nenhuma. Ela some sozinha quando você fecha os
parâmetros, e o checkbox **Grid** no cabeçalho do painel desliga antes disso.

Perder a pessoa não é emergência: o quadro **segura** por `Hold` segundos —
alguém que vira de costas não justifica um movimento — e só então abre de
volta, suave. O recorte nunca sai do quadro: quem anda para a borda sai do
centro em vez de aparecer tarja preta no painel.

Cuidado com resolução: recortar joga pixels fora. Com 1920×1080 na entrada,
Max Zoom 1,8 manda cerca de 1067×600 pixels reais para uma saída 1080p, com
upscale — e o processador do painel costuma escalar de novo. `Max Zoom` é o
controle que decide quanta perda é aceitável.

A linha **Tracking** abaixo da entrada mostra o que o detector está vendo.
No macOS ele usa o framework Vision e precisa de permissão de câmera
(Ajustes do Sistema → Privacidade e Segurança → Câmera). **No Windows ainda
não existe detector**: o efeito funciona, mas só nos controles manuais. Os
detalhes e as opções estão em [docs/TRACKING.md](docs/TRACKING.md).

## Saída para uma tela (LED, projetor, segundo monitor)

O quadro processado sai por uma **janela sem bordas em tela cheia** na tela que
você escolher. Para o que estiver do outro lado do cabo — um processador de
LED, um projetor, a entrada HDMI de um switcher — isso é um sinal de vídeo como
qualquer outro: sem barra de título, sem cursor, sem interface. É o mesmo lugar
que um computador de Resolume ocupa em um evento: você manda um sinal, o
mapeamento para os painéis é do outro lado.

```bash
./build/bin/atem_fx --list-displays
```

```text
ID         NAME                           RESOLUTION   REFRESH
1          Built-in Retina Display        3024x1964    120.00 Hz  (primary)
2          LG ULTRAGEAR                   1920x1080    144.00 Hz
```

No painel **OUTPUT**, logo abaixo de SOURCE, escolha a tela na lista. O envio
começa na hora. **Stop output** e a opção **No output** encerram; a tecla
**Escape** também, e é por isso que ela existe: se você mandar a saída para a
tela onde está a interface, a janela cobre tudo, inclusive o botão de parar.
Por linha de comando, `--output 2` já começa enviando.

Conexões e desconexões atualizam a lista automaticamente, inclusive com
**Operation lock** ligado. Se a tela que recebe PROGRAM desaparecer ou mudar
de resolução, taxa, escala ou posição no desktop, o envio para ela é encerrado
e OUTPUT mostra como retomar. Reconectar o cabo não reabre o envio: escolha a
tela novamente. A imagem de PROGRAM e a webcam virtual continuam independentes
dessa conexão.

Minimizar ou cobrir a janela do operador mantém o telão e a webcam virtual
funcionando. Quando só a webcam está ativa e o preview está oculto, o loop usa
um intervalo de aproximadamente 16,68 ms para evitar sobrecarregar a GPU.

O processamento é sempre 1920×1080. Se a tela tiver outra proporção, a imagem
entra inteira com barras pretas (*fit*) — nunca esticada, porque distorção é o
tipo de erro que ninguém consegue corrigir mais adiante na cadeia.

**Com uma saída ativa, ela passa a ser o relógio.** A janela de preview para de
esperar o próprio monitor: esperar dois vsyncs que nunca concordam de fase é
como se transformam dois monitores de 60 Hz em 30 fps. A caixa de vsync no
cabeçalho deixa de valer enquanto a saída estiver no ar, e o painel diz isso.

Medido em um M4 com painel de 1920×1080 a 144 Hz, padrão de teste e um efeito,
seis séries de 600 quadros: **112 a 116 fps**, quadro de CPU entre 8,6 e
8,9 ms. Desse tempo, o processamento na GPU é **menos de 1 ms** — o resto é
espera pelo vsync da tela, que é exatamente o que se quer. Para os 59,94 fps
que o projeto persegue, sobra muita folga.

O que isso **não** é: não é SDI, não é genlock e não é DeckLink. A tela é um
display, e o sistema operacional o entrega no ritmo dele. Para uma parede de
LED atrás de um processador isso é normal e é assim que se trabalha. Para
devolver um sinal limpo à entrada de um ATEM, o caminho continua sendo o M1.

## Segurança da imagem

Num show, a pergunta que importa não é qual efeito está bonito — é **o que
está no telão agora e como eu tiro isso de lá em um movimento**. O painel
**PROGRAM**, no topo da coluna esquerda, tem quatro botões:

| Botão      | O que vai para o telão                      |
| ---------- | ------------------------------------------- |
| **FX**     | a cadeia completa, como você montou         |
| **Clean**  | a imagem sem efeitos visuais                |
| **Freeze** | o último quadro bom, congelado              |
| **Black**  | preto, com a saída no ar                    |

**Clean não é desligar tudo.** O enquadramento continua: o Auto Frame segue
recortando e o formato 9:16 continua onde estava. Você tira o *look* sem
perder o plano e sem mudar o que o processador de LED recebe — que é
justamente o que se quer quando o efeito não combinou com o momento.

**Nenhum dos quatro botões corta.** Todos dissolvem em 0,35 s, com entrada e
saída suavizadas, e o painel mostra o quanto já andou (`CLEAN 62%`). Quem está
assistindo lê um corte não anunciado como defeito; lê uma dissolução como
decisão.

FX e Clean *mudam* a imagem, então a mistura acontece dentro da cadeia: o
*look* se dissolve efeito por efeito. O enquadramento nunca entra na mistura —
o plano não pode escorregar nem ficar meio recortado enquanto o efeito sai.

Freeze e Black *substituem* a imagem, então a mistura acontece na saída, entre
quadros inteiros. Ao apertar Freeze, a câmera continua entrando e a cadeia
continua trabalhando: o telão vê a imagem em movimento se dissolver dentro do
congelado do instante em que você apertou. Black desce igual, e sobe de volta
igual.

Se você mudar de ideia no meio, a transição continua de onde a imagem está —
voltando pelo mesmo caminho, ou saindo da mistura que está no ar se você
escolher um terceiro botão.

**Queda de sinal corta, não dissolve.** Segurança não é gesto: quando a câmera
some, o último quadro bom tem de estar no telão naquele quadro, não em 0,35 s.

**Freeze e Black não param a máquina.** A câmera continua entrando, o tracking
continua seguindo e a cadeia continua preparando o próximo plano atrás da
imagem parada. A saída segue enviando quadro a quadro: o telão vê uma imagem
**parada**, nunca um sinal morto. Quando você volta para FX ou Clean, já
entra no plano de agora, não no de trinta segundos atrás.

### Ensaiar o efeito antes de mandar ao ar

O monitor da **direita** é o PROGRAM: exatamente o que vai para o telão. O da
**esquerda** tem dois botões no topo:

| Botão      | O que aparece                                                |
| ---------- | ------------------------------------------------------------ |
| **SOURCE** | a câmera como ela chega, antes da cadeia — é aqui que você escolhe o assunto |
| **FX**     | a imagem da cadeia: o que o botão **FX** mandaria agora       |

O chevron à esquerda do nome no cabeçalho recolhe a coluna de controles para
um rail de títulos (PROGRAM, SOURCE, OUTPUT, EFFECTS). Os dois monitores
ficam maiores. Clique num título para reabrir aquela seção — os modos de
PROGRAM (FX / Clean / Freeze / Black) voltam a um clique depois disso.

É assim que se monta um look sem ninguém ver: ponha o PROGRAM em **Freeze** ou
**Black**, deixe o monitor da esquerda em **FX**, monte e ajuste o efeito
olhando ali, e só então aperte **FX**. Enquanto isso o telão continua na
imagem parada ou no preto.

Em **Clean** o look é retirado da própria cadeia, então o monitor **FX** mostra
a mesma imagem limpa e escreve a porcentagem da mistura — não é defeito do
efeito. Para ensaiar fora do ar, use Freeze ou Black.

Escolher o assunto continua sendo no SOURCE: o monitor volta para SOURCE
sozinho quando **Pick subject** está ligado, ou quando há gente em quadro e
ninguém está sendo seguido.

### Quando a câmera cai

Se o sinal some — cabo, bateria, alguém esbarrou no USB — o PROGRAM **segura
o último quadro bom** e entra em Freeze sozinho. Três regras saem daí:

- **Perder a câmera nunca escolhe uma entrada.** O padrão de teste é uma
  escolha sua, nunca um plano B automático. Nada coloca aquelas barras no
  telão por conta própria.
- **Câmera que volta não sobe sozinha.** Ela reconecta, a barra de status
  avisa, e o plano só vai ao ar quando você seleciona a entrada e aperta FX ou
  Clean. Uma câmera que volta no meio da música não entra fria e sem
  enquadramento.
- **O que você mandou vale mais.** Se você pôs **Black**, a queda da câmera
  não muda isso.

Antes do primeiro quadro válido não existe nada para segurar, e aí todo modo
mostra preto — Freeze não inventa imagem que não tem.

Para abrir o show já no preto, antes de qualquer coisa ir ao ar:

```bash
./build/bin/atem_fx --output 2 --program black
```

## Descoberta DeckLink (Windows)

O build padrão dispensa o SDK. Para habilitar a descoberta, use o SDK
DeckLink externo da Blackmagic e execute no **x64 Native Tools Command Prompt
for VS 2022**, com o Windows SDK instalado:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DATEMFX_ENABLE_DECKLINK=ON -DATEMFX_DECKLINK_SDK_DIR="C:\SDKs\DeckLink SDK"
cmake --build build --config Release
build\bin\Release\atem_fx.exe --list-decklink
```

O comando lista nomes, capacidades de captura/saída e conexões de vídeo
suportadas e encerra sem abrir a interface nem inicializar a GPU. Essas
capacidades não indicam sinal conectado. Não combine `--list-decklink` com
opções de renderização, `--list-sources` ou `--check-shaders`.

Enumeração bem-sucedida retorna 0, mesmo sem placas. SDK desabilitado,
macOS ou falha de driver/enumeração retornam 1; uso inválido retorna 2.
Metadados incompletos geram avisos. A validação Windows com placa ainda está
pendente; não há captura, playback ou monitoramento de conexão/desconexão.

## Documentação

| Documento | O que é |
| --------- | ------- |
| [AGENTS.md](AGENTS.md) | Constituição. Precedência sobre prompts. |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Estrutura, RHI, threading |
| [docs/RUNTIME.md](docs/RUNTIME.md) | `main` → `renderFrame`, ownership, mapa de arquivos |
| [docs/EFFECT_SYSTEM.md](docs/EFFECT_SYSTEM.md) | Como adicionar um efeito |
| [docs/TRACKING.md](docs/TRACKING.md) | Tracking de pessoa/objeto e enquadramento |
| [docs/VIDEO_PIPELINE.md](docs/VIDEO_PIPELINE.md) | Pipeline M0 + contrato M1 |
| [docs/VIRTUAL_CAMERA.md](docs/VIRTUAL_CAMERA.md) | PROGRAM como webcam (`--webcam`, macOS) |
| [docs/ATEM_INTEGRATION.md](docs/ATEM_INTEGRATION.md) | Design M4 (não implementado) |
| [docs/PRODUCT.md](docs/PRODUCT.md) | Meta V1 vs o que existe |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Milestones e backlog |
| [docs/BUILD.md](docs/BUILD.md) | Build, CLI, shaders, debug |
| [docs/VISION.md](docs/VISION.md) | Ensaio original. **Não é o mapa do repo.** |
| [memory-bank/](memory-bank/) | Memória de trabalho dos agentes (PT-BR) |

## Marco atual

**M1 — DeckLink IN → GPU → DeckLink OUT**, Windows. Próximo passo: validar
FX-010 no Windows; depois avançar com captura, playback, filas e timing.
ATEM, MIDI, áudio e presets pertencem aos milestones seguintes.

## Licença / nome

O produto é **CamVJ**. O binário e o namespace de código continuam `atem_fx` /
`atemfx` — isso é o motor, não a marca. ATEM é marca registrada da Blackmagic
Design.
