#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

namespace atemfx {

namespace {

// An exact copy of the source. Costs one blit and proves the chain is wired
// up correctly; also the template to copy when writing a new effect.
class PassthroughEffect final : public ShaderEffect
{
public:
    PassthroughEffect()
        : ShaderEffect({"passthrough", "Passthrough", "Utility",
                        "Copies the source unchanged."},
                       "passthrough",
                       SamplerFilter::Point)
    {
    }
};

} // namespace

std::unique_ptr<Effect> createPassthroughEffect()
{
    return std::make_unique<PassthroughEffect>();
}

} // namespace atemfx
