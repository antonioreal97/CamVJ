#pragma once

#include <cstdint>
#include <string>

#include "effects/EffectParameters.h"
#include "gpu/Rhi.h"
#include "tracking/TrackingSnapshot.h"
#include "tracking/framing.h"

namespace atemfx {

enum class EffectRole
{
    Visual,
    Framing,
};

// Everything an effect is allowed to see, snapshotted once per frame.
//
// Effects never reach for global state and never name a graphics API. When
// processing moves to its own thread in M1, this struct is what crosses the
// boundary.
struct EffectContext
{
    ShaderLibrary*  shaders    = nullptr;
    FullscreenPass* fullscreen = nullptr;
    TargetPool*     targets    = nullptr;

    uint32_t width  = 0;
    uint32_t height = 0;

    float    time       = 0.0f;  // seconds since start
    float    deltaTime  = 0.0f;  // seconds since previous frame
    uint64_t frameIndex = 0;

    // Where the subject is, in canvas coordinates. Filled by App from the
    // tracker, like every other control-plane value: by copy, once per frame,
    // never read mid-frame from shared state. Default-constructed means
    // nobody is tracking, which effects must treat as a normal state.
    TrackingSnapshot tracking;

    // Where Auto Frame is looking, and the aspect the wall should receive.
    // App clears framingActive each frame; the effect sets it if it ran.
    // The Preview draws the crop on the source from these, not from the
    // processed picture — after the crop the window is the whole frame.
    FramingRect framing;
    bool        framingActive = false;
    float       outputAspect  = 16.0f / 9.0f;
};

// One node of the processing chain.
//
// The contract (docs/ARCHITECTURE.md section 6): read only from `source`,
// write only to `destination` plus any persistent targets you own, and never
// allocate or block inside process().
class Effect
{
public:
    virtual ~Effect() = default;

    Effect(const Effect&)            = delete;
    Effect& operator=(const Effect&) = delete;

    // Allocate every GPU resource here. Called once, before the first frame,
    // or when an effect is added to a running chain.
    virtual bool initialize(EffectContext& context) = 0;

    // Hot path. Returns true if `destination` was written; false means the
    // effect declined to run and the chain keeps the previous image.
    virtual bool process(EffectContext&    context,
                         const GpuTexture& source,
                         GpuTexture&       destination) = 0;

    virtual void shutdown() = 0;

    // Clean output retains geometric framing without naming individual
    // effects in the chain or changing their saved enabled state.
    virtual EffectRole role() const { return EffectRole::Visual; }

    const EffectDescriptor& descriptor() const { return descriptor_; }

    ParameterSet&       parameters() { return parameters_; }
    const ParameterSet& parameters() const { return parameters_; }

    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

    // Non-empty when the effect could not be initialised, for the UI to show.
    const std::string& lastError() const { return lastError_; }

protected:
    explicit Effect(EffectDescriptor descriptor)
        : descriptor_(std::move(descriptor))
    {
    }

    EffectDescriptor descriptor_;
    ParameterSet     parameters_;
    bool             enabled_ = true;
    std::string      lastError_;
};

} // namespace atemfx
