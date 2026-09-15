#pragma once

#include <cstddef>
#include <span>
#include <string>

#include "effects/Effect.h"
#include "overlays/overlay_model.h"

namespace atemfx {

struct OverlayCompositeLayer
{
    const GpuTexture* texture = nullptr;
    OverlayAspect     aspect  = OverlayAspect::Landscape16x9;
    float             opacity = 1.0f;
};

// GPU-only normal-alpha compositor. Layers arrive bottom-to-top and cost one
// fullscreen pass each. Two private full-frame targets provide the ping-pong;
// upload textures are owned by OverlaySystem and are only sampled here.
class OverlayCompositor
{
public:
    bool initialize(EffectContext& context, std::string& error);
    void shutdown();

    GpuTexture& composite(EffectContext& context, GpuTexture& input,
                          std::span<const OverlayCompositeLayer> layers,
                          float effectMix = 1.0f, bool bypass = false);

    std::size_t lastPassCount() const { return lastPassCount_; }

private:
    GpuTexture* targets_[2] = {nullptr, nullptr};
    bool        initialized_ = false;
    std::size_t lastPassCount_ = 0;
};

} // namespace atemfx
