#pragma once

#include <cstdint>
#include <string>

#include "video/VideoSource.h"

namespace atemfx {

struct EffectContext;

// GPU-generated video source: the input that is always available, on any
// machine, with no hardware and no permissions.
//
// It produces a frame the same way capture does — a texture at project
// resolution — so nothing downstream knows or cares that there is no camera
// behind it.
class TestPatternSource final : public VideoSource
{
public:
    TestPatternSource();

    bool        initialize(EffectContext& context, std::string& error) override;
    void        shutdown() override;
    GpuTexture* render(EffectContext& context) override;
    std::string status() const override;
    bool        bypassEffects() const override;

private:
    // Held by the pool, not by this class: a source needs a target that
    // survives the chain's ping-pong, which is what a persistent target is for.
    // Every source uses the same key, so switching inputs reuses one texture
    // instead of allocating another 1080p target each time.
    GpuTexture* target_ = nullptr;
};

} // namespace atemfx
