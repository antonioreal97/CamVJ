#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "effects/Effect.h"
#include "overlays/overlay_model.h"

namespace atemfx {

class OverlayLibrary;

// Owns the four-layer runtime, asynchronous PNG playback and GPU compositor.
// UI and presets only issue bounded control calls and read immutable snapshots;
// no decoder or filesystem object crosses into the frame pipeline.
class OverlaySystem
{
public:
    OverlaySystem();
    ~OverlaySystem();

    OverlaySystem(const OverlaySystem&) = delete;
    OverlaySystem& operator=(const OverlaySystem&) = delete;

    bool initialize(EffectContext& context, const OverlayLibrary& library, std::string& error);
    void setLibrary(const OverlayLibrary& library);
    void shutdown();

    OverlayLayerId addLayer(std::string_view assetId, std::string& error);
    bool removeLayer(OverlayLayerId layerId);
    bool moveLayer(OverlayLayerId layerId, int delta);
    bool setLayerAsset(OverlayLayerId layerId, std::string_view assetId, std::string& error);
    bool setLayerEnabled(OverlayLayerId layerId, bool enabled, std::string& error);
    bool setLayerOpacity(OverlayLayerId layerId, float opacity);
    bool setLayerPlayback(OverlayLayerId layerId, OverlayPlayback playback);
    bool setLayerFramesPerSecond(OverlayLayerId layerId, float framesPerSecond);

    // Pause is an operator transport state, not preset state. Fades continue
    // while paused; only the PNG-sequence clock stops.
    bool setLayerPaused(OverlayLayerId layerId, bool paused);
    bool triggerLayer(OverlayLayerId layerId, std::string& error);

    // Read-only preset/reference validation. App calls this before rebuilding
    // the effect chain so a missing graphic cannot leave a half-recalled look.
    bool validateConfig(const OverlayStackConfig& config, std::string& error) const;
    bool applyConfig(const OverlayStackConfig& config, std::string& error);
    const OverlayStackConfig& config() const;
    const OverlayUiSnapshot& snapshot() const;

    // Applies deferred structural cleanup (for example, erasing a layer once
    // its fade-out finished). Call between frames, outside GPU processing.
    void commitControlPlane();

    // Advances clocks and drains already-decoded frames without waiting. Call
    // every processing frame, including behind Freeze/Black. No disk access,
    // lock, allocation or logging occurs here in steady state.
    void service(EffectContext& context);

    // Inserts overlays after EffectChain and before ProgramOutput. effectMix
    // is the existing FX/Clean amount; bypass is true for calibration sources.
    GpuTexture& composite(EffectContext& context, GpuTexture& input,
                          float effectMix = 1.0f, bool bypass = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace atemfx
