#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "effects/EffectChain.h"
#include "effects/parameter_automation.h"
#include "overlays/overlay_model.h"
#include "video/program_output.h"

namespace atemfx {

// One scalar on a node, including optional session loop settings.
struct SceneParamState
{
    std::string id;
    float       value = 0.0f;
    bool        automationEnabled = false;
    bool        automationPaused  = false;
    AutomationWaveform waveform   = AutomationWaveform::Sine;
    double      periodSeconds     = 4.0;
    float       automationMin     = 0.0f;
    float       automationMax     = 1.0f;
    float       phaseOffset       = 0.0f;
};

struct SceneEffectState
{
    std::string                 typeId;
    bool                        enabled = true;
    std::vector<SceneParamState> params;
};

// One entry in the overlay stack. Runtime-only identity, fade progress and
// playhead deliberately stay out of a look: recall creates fresh layer ids
// and starts enabled sequences at frame zero.
struct SceneOverlayState
{
    std::string     assetId;
    bool            enabled         = false;
    float           opacity         = 1.0f;
    OverlayPlayback playback        = OverlayPlayback::Loop;
    float           framesPerSecond = kOverlaySequenceFps;
};

// A recallable look: the linear chain plus the PROGRAM end that belongs with
// it (FX or Clean). Freeze and Black are safety gestures, not looks — they are
// never written into a preset and are left alone on recall.
struct ScenePreset
{
    static constexpr int kVersion = 2;

    std::string id;
    std::string name;
    bool        factory = false;
    ProgramMode program = ProgramMode::Effects;
    std::vector<SceneEffectState> effects;
    // Back-to-front. The last entry is the topmost graphic.
    std::vector<SceneOverlayState> overlays;
};

// Snapshot the live chain. `program` is recorded only when it is FX or Clean.
ScenePreset captureScene(const EffectChain& chain, ProgramMode program,
                         const OverlayStackConfig& overlays,
                         std::string id, std::string name);

// Compatibility helper for callers that deliberately capture a look with no
// graphics (factory presets and small tests).
ScenePreset captureScene(const EffectChain& chain, ProgramMode program,
                         std::string id, std::string name);

// Rebuilds the chain from the preset. Unknown type ids fail the whole recall
// so a half-applied look never goes to air. FX/Clean snap without dissolve —
// a preset is a cut to a prepared look, not a gesture.
bool applyScene(const ScenePreset& preset, EffectChain& chain, EffectContext& context,
                ProgramMode& program, ProgramTransition& transition, std::string& error);

std::string serializeScene(const ScenePreset& preset);
bool        parseScene(std::string_view json, ScenePreset& out, std::string& error);

const char* programModeToToken(ProgramMode mode);
bool        programModeFromToken(std::string_view token, ProgramMode& out);

const char* waveformToToken(AutomationWaveform waveform);
bool        waveformFromToken(std::string_view token, AutomationWaveform& out);

const char* overlayPlaybackToToken(OverlayPlayback playback);
bool        overlayPlaybackFromToken(std::string_view token, OverlayPlayback& out);

} // namespace atemfx
