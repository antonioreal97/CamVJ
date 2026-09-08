# Tech context

## Stack atual (linkado)

| Área | Tecnologia |
| --- | --- |
| Linguagem | C++20; Objective-C++ na cola macOS |
| Build | CMake 3.21+, `CMakeLists.txt` + `cmake/decklink.cmake` |
| GPU | Metal (Apple) ou D3D11 (Windows), compile-time |
| Shaders | HLSL SM 5.0 + MSL, escritos duas vezes |
| UI | Dear ImGui v1.91.9 (FetchContent ou `ATEMFX_IMGUI_DIR`) |
| Janela | AppKit (pump manual) / Win32 |
| Log | `src/core/Log.cpp` — printf UTF-8; console Windows e debugger via Unicode |
| Config | nenhuma (sem JSON) |
| Testes | self-test CLI + teste C++ portátil de automações via CTest, sem Catch2 |
| Discovery | DeckLink SDK externo opcional, somente Windows neste projeto |

Dependência externa do build padrão: ImGui. Discovery Windows pode adicionar
o SDK DeckLink externo com `ATEMFX_ENABLE_DECKLINK=ON` e
`ATEMFX_DECKLINK_SDK_DIR`. O SDK não é baixado nem versionado no repositório.
macOS/build sem SDK usa stub indisponível. CMake fatal em Linux.

Targets: `imgui`, `atem_fx`, `atem_fx_shaders` (copia `shaders/` para
`$<TARGET_FILE_DIR:atem_fx>/shaders`). Binário: `build/bin/atem_fx`.
Com `BUILD_TESTING=ON` (default), também `parameter_automation_test`,
registrado no CTest como `parameter_automation`. Nenhuma dependência externa
adicional; execução com `ctest --test-dir build --output-on-failure`.

## Stack planejada (não linkar agora)

| Área | Tecnologia | Milestone |
| --- | --- | --- |
| Log | spdlog (macros já no formato) | quando hardware exigir sinks |
| Testes | Catch2 | adoção futura; teste de automações já existe sem framework |
| Config / presets | JSON | M3 |
| Captura/saída SDI | DeckLink SDK (discovery já implementado) | M1, Windows |
| Mixer | ATEM SDK | M4, Windows |
| Áudio | WASAPI / CoreAudio | M5 |
| MIDI | RtMidi | M5 |

## CLI

```text
--headless --frames N --dump PATH --enable a,b,c --no-vsync --list-decklink --help
```

Headless sem `--frames` roda 300 frames. `--enable` só liga/desliga os nove
nós do default chain; não cria tipos extras.

`--list-decklink` é um comando exclusivo, executado antes de criar `App`,
GPU ou UI. Não aceita flags de render junto (exit 2). Enumeração bem-sucedida,
mesmo vazia, retorna 0; indisponibilidade ou erro de COM/driver/enumeração
retorna 1. Metadados parciais geram avisos. Validação Windows ainda pendente.

## Shaders em runtime

Ordem de busca: `ATEMFX_SHADER_DIR` → `shaders/` ao lado do exe (até 5
níveis) → árvore fonte (`ATEMFX_SHADER_SOURCE_DIR`). Hot reload: botão no
Stats; shader que falha guarda a versão anterior.

Metal prepende `common.metal` em todo fragment. D3D11 usa `fullscreen.hlsl`
como VS compartilhado.

Debug: `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1`. Windows Debug liga a
debug layer D3D11 se instalada.

## Resolução e formato

Processamento fixo 1920×1080 RGBA16Float. Apresentação 8-bit. Dump: PPM P6.

59,94 é **orçamento**, não genlock. O loop segue o display (ou o `for`
headless).

## Automações de parâmetros

`src/effects/parameter_automation.h/.cpp`: ondas Sine/Triangle/Ramp Up/
Ramp Down/Square, fase double com wrap, ciclo em segundos (0,05–600).
`EffectParameters.h` guarda a configuração e separa valor manual de efetivo.
`EffectChain::process()` avança os relógios pelo delta do frame; bypass
mantém automação ativa. UI no `ParameterWidgets.cpp`, só para efeitos.

Operações escalares, sem alocação, lock, log ou novo recurso de GPU.
Shaders, constant buffer de 96 bytes e RHI permanecem iguais. Estado só da
sessão; não há arquivo de configuração ou integração MIDI/áudio.

## Namespace

Tudo em `atemfx::`. Arquivos `snake_case`, tipos `PascalCase`, membros
privados com `_`.
