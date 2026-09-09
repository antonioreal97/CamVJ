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
| Config | nenhuma (sem JSON); versão vem do `project(AtemFx VERSION ...)` |
| Testes | self-test CLI + 5 testes C++ portáteis via CTest, sem Catch2 |
| Discovery | DeckLink SDK externo opcional, somente Windows neste projeto |
| Webcam | extensão de câmera já instalada (OBS) via CoreMediaIO, só macOS |
| Empacotamento | `scripts/package_macos.sh` (DMG) e `scripts/package_windows.ps1` (ZIP) |
| CI | GitHub Actions `.github/workflows/ci.yml` — só macOS self-hosted (`self-hosted`, `macOS`, `ARM64`); build Release + CTest + `--check-shaders` + headless 200; sem publicar pacotes |

Dependência externa do build padrão: ImGui. Discovery Windows pode adicionar
o SDK DeckLink externo com `ATEMFX_ENABLE_DECKLINK=ON` e
`ATEMFX_DECKLINK_SDK_DIR`. O SDK não é baixado nem versionado no repositório.
macOS/build sem SDK usa stub indisponível. CMake fatal em Linux.

Targets: `imgui`, `atem_fx`, `atem_fx_shaders` (copia `shaders/` para
`$<TARGET_FILE_DIR:atem_fx>/shaders`). Binário: `build/bin/atem_fx`; no macOS
dentro de `build/bin/atem_fx.app` (o bundle é o que dá acesso à câmera).
Com `BUILD_TESTING=ON` (default), também `parameter_automation_test`,
`framing_test`, `source_mapping_test`, `source_health_test` e
`program_output_test`, registrados no CTest como `parameter_automation`,
`framing`, `source_mapping`, `source_health` e `program_output`. Nenhuma
dependência externa adicional; execução com
`ctest --test-dir build --output-on-failure`.

## Versão

Origem única: `project(AtemFx VERSION 1.0.0)` na linha 3 do `CMakeLists.txt`.
CMake carimba o binário com `ATEMFX_VERSION`; `src/core/Version.h` publica
como `atemfx::kVersion`, e dele saem `--version`, o banner do `--help`, o log
de startup, o banner de timing do headless e o strip cinza do cabeçalho da UI.
Dela também saem `MACOSX_BUNDLE_BUNDLE_VERSION` /
`MACOSX_BUNDLE_SHORT_VERSION_STRING` e o número no nome do DMG/ZIP quando
`-v` / `-Version` não é passado. Build fora deste CMake reporta `0.0.0-dev`. Hoje
**1.0.0**, igual à tag `v1.0.0` e ao release. Binário (`atem_fx`) e bundle id
(`fx.atem.engine`) não são versionados e não mudam — bundle id novo invalida a
permissão de câmera. Passo a passo em `docs/BUILD.md#versioning`.

## Stack planejada (não linkar agora)

| Área | Tecnologia | Milestone |
| --- | --- | --- |
| Log | spdlog (macros já no formato) | quando hardware exigir sinks |
| Testes | Catch2 | adoção futura; os 5 testes atuais já rodam sem framework |
| Config / presets | JSON | M3 |
| Captura/saída SDI | DeckLink SDK (discovery já implementado) | M1, Windows |
| Mixer | ATEM SDK | M4, Windows |
| Áudio | WASAPI / CoreAudio | M5 |
| MIDI | RtMidi | M5 |

## CLI

```text
--headless --frames N --dump PATH --enable a,b,c --no-vsync
--source ID --pattern NAME --output ID --webcam --program MODE
--list-sources --list-displays --check-shaders --list-decklink
--version --help
```

Headless sem `--frames` roda 300 frames. `--enable` só liga/desliga os onze
nós do default chain; não cria tipos extras. `--pattern` aceita bars, plasma,
grid e led-mapping; `--program` aceita fx, clean, freeze e black. `--webcam`
só existe no macOS (extensão de câmera instalada). `--version` responde antes
de qualquer parse, como `--help`, e não abre device. `--output` precisa de
janela e é ignorado em `--headless`.

`--list-decklink` é um comando exclusivo, executado antes de criar `App`,
GPU ou UI. Não aceita flags de render junto (exit 2). Enumeração bem-sucedida,
mesmo vazia, retorna 0; indisponibilidade ou erro de COM/driver/enumeração
retorna 1. Metadados parciais geram avisos. Validação Windows ainda pendente.

## Shaders em runtime

14 shaders por backend (mais `common` e, no HLSL, `fullscreen`).
`--check-shaders` compila todos e imprime a contagem.

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
