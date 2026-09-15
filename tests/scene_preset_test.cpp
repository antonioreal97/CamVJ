#include "presets/boot_state.h"
#include "presets/preset_store.h"
#include "presets/scene_preset.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char* label)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

} // namespace

int main()
{
    using namespace atemfx;

    {
        ScenePreset original = factoryPresets().front();
        expect(original.id == "clean_frame", "first factory is clean_frame");
        expect(original.program == ProgramMode::Clean, "clean_frame is Clean");
        expect(!original.effects.empty(), "factory has effects");

        const std::string json = serializeScene(original);
        ScenePreset       parsed;
        std::string       error;
        expect(parseScene(json, parsed, error), "parse factory clean_frame");
        expect(error.empty() || parsed.id == "clean_frame", error.c_str());
        expect(parsed.id == original.id, "id round-trip");
        expect(parsed.name == original.name, "name round-trip");
        expect(parsed.program == original.program, "program round-trip");
        expect(parsed.effects.size() == original.effects.size(), "effect count");
        expect(parsed.effects.front().typeId == "auto_frame", "first node auto_frame");
        expect(parsed.effects.front().enabled, "auto_frame enabled");
        expect(parsed.overlays.empty(), "factory has no overlays");
    }

    {
        ScenePreset pulse;
        expect(findFactoryPreset("rgb_pulse", pulse), "find rgb_pulse");
        bool foundLoop = false;
        for (const SceneEffectState& node : pulse.effects)
        {
            if (node.typeId != "rgb_split" || !node.enabled) continue;
            for (const SceneParamState& param : node.params)
            {
                if (param.id == "amount" && param.automationEnabled)
                {
                    foundLoop = true;
                    expect(param.waveform == AutomationWaveform::Sine, "rgb amount sine");
                }
            }
        }
        expect(foundLoop, "rgb_pulse has looped amount");

        const std::string json = serializeScene(pulse);
        ScenePreset       parsed;
        std::string       error;
        expect(parseScene(json, parsed, error), "parse rgb_pulse");
        expect(parsed.effects.size() == pulse.effects.size(), "rgb_pulse effect count");
    }

    {
        ScenePreset withOverlay = factoryPresets().front();
        SceneOverlayState lower;
        lower.assetId          = "ovl_lower";
        lower.enabled          = true;
        lower.opacity          = 0.65f;
        lower.playback         = OverlayPlayback::Loop;
        lower.framesPerSecond  = 24.0f;
        withOverlay.overlays.push_back(lower);

        SceneOverlayState upper;
        upper.assetId          = "ovl_upper";
        upper.enabled          = false;
        upper.opacity          = 1.0f;
        upper.playback         = OverlayPlayback::OneShot;
        upper.framesPerSecond  = 30.0f;
        withOverlay.overlays.push_back(upper);

        ScenePreset parsed;
        std::string error;
        expect(parseScene(serializeScene(withOverlay), parsed, error), "parse overlay preset v2");
        expect(parsed.overlays.size() == 2, "overlay count round-trip");
        expect(parsed.overlays[0].assetId == "ovl_lower", "overlay order round-trip");
        expect(parsed.overlays[0].enabled, "overlay enabled round-trip");
        expect(parsed.overlays[0].opacity > 0.64f && parsed.overlays[0].opacity < 0.66f,
               "overlay opacity round-trip");
        expect(parsed.overlays[1].playback == OverlayPlayback::OneShot,
               "overlay playback round-trip");
        expect(parsed.overlays[1].framesPerSecond == 30.0f, "overlay fps round-trip");
    }

    {
        const char* legacy = R"({
          "version": 1,
          "id": "legacy",
          "name": "Legacy",
          "factory": false,
          "program": "fx",
          "effects": [{"type":"auto_frame","enabled":true,"params":{}}]
        })";
        ScenePreset parsed;
        std::string error;
        expect(parseScene(legacy, parsed, error), "parse legacy preset v1");
        expect(parsed.overlays.empty(), "legacy preset migrates to empty overlays");
    }

    {
        const char* duplicate = R"({
          "version": 2,
          "id": "bad",
          "name": "Bad",
          "factory": false,
          "program": "fx",
          "effects": [{"type":"auto_frame","enabled":true,"params":{}}],
          "overlays": [
            {"assetId":"same","enabled":true,"opacity":1,"playback":"loop","fps":30},
            {"assetId":"same","enabled":false,"opacity":1,"playback":"loop","fps":30}
          ]
        })";
        ScenePreset parsed;
        std::string error;
        expect(!parseScene(duplicate, parsed, error), "reject duplicate overlay asset");
    }

    {
        expect(presetIdFromName("My Look") == "my_look", "slug spaces");
        expect(presetIdFromName("VHS / CRT") == "vhs_crt", "slug punctuation");
        expect(presetIdFromName("@@@") == "look", "slug fallback");
    }

    {
        BootState state;
        state.sourceId        = "camera:demo";
        state.outputDisplayId = "2";
        state.portrait        = true;
        const std::string json = serializeBoot(state);
        BootState         parsed;
        std::string       error;
        expect(parseBoot(json, parsed, error), "parse boot");
        expect(parsed.sourceId == state.sourceId, "boot source");
        expect(parsed.outputDisplayId == state.outputDisplayId, "boot output");
        expect(parsed.portrait, "boot portrait");
    }

    {
        std::string error;
        ScenePreset bogus;
        expect(!parseScene("{ \"version\": 99, \"id\": \"x\", \"name\": \"x\", \"effects\": [] }",
                           bogus, error),
               "reject bad version");
    }

    if (g_failures != 0)
    {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "scene_preset ok\n";
    return 0;
}
