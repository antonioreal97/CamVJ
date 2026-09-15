# Active context

## Foco

Pedido atual: **overlays próprios no sidebar, com variantes 16:9 e 9:16**.
FX-026 foi implementado como abertura estreita do M6, preservando o canvas
1920×1080 e o caminho de show macOS. Sem ATEM, DeckLink, Fill/Key ou blend modes.

## O que entrou neste ciclo

- **FX-009 mínimo:** `src/presets/` — scene JSON (cadeia, params, loops,
  FX/Clean), factory looks, save/recall na UI (**PRESETS** entre OUTPUT e
  EFFECTS). CTest `scene_preset`.
- **Venue boot:** `~/Library/Application Support/CamVJ/boot.json` lembra
  source id, output display id e portrait; CLI `--source` / `--output` vencem.
- **Rack:** Mirror mode e Frame Delay blend viraram `makeChoice`.
- **Runbook** no README (Gatekeeper + show path). DMG unsigned regenerado.
- **FX-026:** `src/overlays/` — biblioteca gerenciada, import assíncrono por
  picker nativo, PNG estático/sequência, variantes exatas 1920×1080 e
  1080×1920, até quatro layers e uma sequência ativa por vez.
- **Composição:** GPU depois da EffectChain e antes do ProgramOutput; Clean
  dissolve overlays junto do look, Freeze/Black mantêm playback atrás, LED
  Mapping ignora a pilha. HLSL + MSL `overlay_composite`.
- **UI:** OVERLAYS entre PRESETS e EFFECTS; library/layer abrem no inspector
  largo. Import nunca põe a arte no ar. Operation lock protege biblioteca e
  estrutura, mas não visibilidade/opacidade/transporte.
- **Presets v2:** guarda a pilha; v1 migra com overlays vazios.

## P0 observado neste Mac

- Build Release, CTest 7/7, shaders 14/14, headless 200 frames OK (~0,6 ms GPU).
- `--list-sources`: Test Pattern + câmera embutida (FX30 não plugada agora).
- `--list-displays`: só Built-in Retina — LED externo não conectado nesta
  máquina; validação de cabo/LED fica no Mac do evento.
- DMG `dist/CamVJ-1.0.0-macos-arm64.dmg` verificado com `hdiutil verify`.

## Decisões

- Presets abrem M3 cedo e estreito; M1/M2/M4/M5 fechados.
- Overlays abrem M6 cedo e estreito; não são Effects nem um layer graph.
- Freeze/Black não entram no look; venue boot é arquivo separado do look.
- JSON hand-rolled — sem nlohmann/spdlog/Catch2 novos.
- Operation lock bloqueia recall/save (igual mutação da chain).

## Próximos (operador, não código)

1. Inspecionar manualmente o painel OVERLAYS e importar uma arte real de cada formato.
2. Ensaiar runbook no Mac do evento com FX30 + processador LED.
3. Validar picker/WIC e runtime no Windows.
