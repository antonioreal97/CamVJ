#pragma once

#include <string>

#include "effects/Effect.h"

namespace atemfx {

// The common case: one shader, some scalars, one fullscreen pass.
//
// Deriving from this makes a new effect a constructor plus a shader file. The
// effect owns no GPU resources at all — the backend uploads the constant block
// — so the same class works unchanged on Direct3D 11 and on Metal.
class ShaderEffect : public Effect
{
public:
    bool initialize(EffectContext& context) override;

    bool process(EffectContext&    context,
                 const GpuTexture& source,
                 GpuTexture&       destination) override;

    void shutdown() override;

    const std::string& shaderName() const { return shaderName_; }

protected:
    // `shaderName` has no extension: each backend resolves it to its own
    // source file, "rgb_split" to rgb_split.hlsl or rgb_split.metal.
    ShaderEffect(EffectDescriptor descriptor,
                 std::string      shaderName,
                 SamplerFilter    filter = SamplerFilter::Linear);

    // Default packing: parameter i goes to params[i / 4][i % 4], in the order
    // the effect declared them. See docs/EFFECT_SYSTEM.md.
    virtual void packConstants(const EffectContext& context, EffectConstants& constants) const;

private:
    std::string   shaderName_;
    SamplerFilter filter_;
};

} // namespace atemfx
