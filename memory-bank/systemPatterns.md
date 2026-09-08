# System patterns

## Princípio

**Control plane ≠ pixel pipeline.** UI, ATEM, MIDI, áudio publicam um
snapshot de parâmetros. Capture, Process e Output nunca esperam por eles.

Em M0 o snapshot é `EffectContext`, preenchido em `App::updateEffectContext()`
antes da chain.

## RHI estreito

`src/gpu/Rhi.h` é a interface gráfica inteira:

- `GpuTexture`, `ShaderLibrary`, `FullscreenPass`, `TargetPool`, `GraphicsDevice`

Um primitivo de desenho: **fullscreen pass**. Efeito que precisa de mais do
que isso pede um design, não um método novo no RHI.

Dois backends, um por build (CMake): `gpu/d3d11` ou `gpu/metal`. Código
portátil (`app/`, `effects/`, `video/`, `ui/` exceto `ui/backend/`) não nomeia
`ID3D11*`, `id<MTL*>`, `HWND`, `NSView`.

## Ping-pong + persistentes

Chain linear: dois scratch targets em rotação, formato RGBA16Float.

`TargetPool::persistent(key)` sobrevive entre frames. `TestPatternSource`
escreve `"source.frame"`. Isso **não** significa que M2 (grafo DAG, efeito
de feedback) está feito.

## Efeitos

- `Effect` + `ParameterSet` (Float/Int/Bool). UI genérica.
- `ShaderEffect` cobre o caso “um pixel shader + escalares”.
- Registro explícito em `BuiltinEffects.cpp` (ordem = menu Add). Sem
  static-initialiser: o linker não pode dropar o efeito.
- `process()` retorna false → no-op, não frame preto.
- Constant buffer compartilhado 96 bytes: `EffectConstants.h` +
  `common.hlsli` + `common.metal`. Mudou um, mudam os três.

`ParameterAutomation` é estado por parâmetro e por instância do efeito, sem
referência a índice da chain. A chain avança todos os relógios uma vez com
`EffectContext::deltaTime`, antes de decidir bypass. Assim, troca de seleção,
reorder ou bypass não reinicia o loop. Só loops ativos e não pausados avançam.

`Parameter::value` é o manual; `currentValue()` resolve o valor efetivo,
limita ao intervalo e converte Int/Bool. Accessors e `valueOr()` usam o mesmo
valor; `ShaderEffect` o envia ao constant buffer existente. O relógio usa
fase double normalizada e avaliação escalar sem alloc/lock/log. Não faz
processamento de imagem na CPU nem amplia RHI ou shaders.

Desligar Loop preserva configuração e fase, devolvendo o manual; religar
retoma. Pause congela o relógio; Restart zera fase interna e mantém offset.
Reset restaura default e limpa automação. Configuração só da sessão, UI
genérica só nos efeitos. DAG M2, presets M3 e MIDI/áudio M5 continuam futuros.

Custo de um efeito novo:

1. `shaders/hlsl/foo.hlsl`
2. `shaders/metal/foo.metal`
3. `src/effects/FooEffect.cpp`
4. uma linha em `BuiltinEffects.cpp`

Se precisar mais, a abstração está errada.

## Ownership

`App` é o dono: `Window`, `GraphicsDevice`, `TestPatternSource`,
`EffectChain`, `FrameTiming`, `UiLayer`. `unique_ptr` para dono; ponteiro
cru / referência para empréstimo. `EffectContext` e `UiFrameState` são
snapshots por frame.

Discovery (`--list-decklink`) é outro caminho de startup: não cria `App`.
O comando é dono dos recursos SDK/COM e os libera ao encerrar. A interface
`decklink_discovery.h` é portátil; tipos SDK/COM ficam no arquivo Windows.
CMake escolhe implementação Windows opcional ou stub indisponível.

## Threading (M0)

Uma thread faz tudo. Metal timing usa completion handler em background
escrevendo atomics — não é thread de processamento.

Seam do M1: o bloco `beginProcessing`…`endProcessing` de `App::renderFrame()`
sai para a thread de GPU. Filas lock-free, drop oldest, callbacks DeckLink
não bloqueiam.

Esse split ainda é futuro. FX-010 só enumera dispositivos fora do loop;
alocações, consultas de metadados e logs de discovery não entram no hot path.

## Hot path

Depois do steady state: sem alloc, sem lock, sem log no per-frame.
`readback` e dump PPM são diagnóstico, nunca o loop ao vivo.

## Erros

Status + log. Sem exceção atravessando fronteira de módulo no caminho de
vídeo. Sem engolir erro de hardware.

## Verificação

Toda mudança: `atem_fx --headless --frames 200`. Sem display, sem hardware.
Para a lógica portátil de loops, `ctest --test-dir build --output-on-failure`
executa `parameter_automation` com `BUILD_TESTING=ON` (default). Esse teste
não precisa de GPU ou Catch2 e não substitui o gate de renderização.
