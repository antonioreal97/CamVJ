#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

#include <cstdint>
#include <string>

namespace atemfx {

namespace {

uint32_t nextShutterKey()
{
    static uint32_t next = 0;
    return next++;
}

// Temporal smear. The picture from last frame lives in a persistent target so
// the chain's ping-pong cannot overwrite it. The first process() seeds that
// target; later frames mix, then blit the result back. Not the M2 feedback
// graph — one node, one history, linear chain.
class ShutterEffect final : public ShaderEffect
{
public:
    ShutterEffect()
        : ShaderEffect({"shutter", "Shutter", "Temporal",
                        "Smears fast motion, leaving a decaying trail."},
                       "shutter",
                       SamplerFilter::Linear)
        , historyKey_("shutter." + std::to_string(nextShutterKey()))
    {
        parameters_.add(Parameter::makeFloat("decay", "Decay", 0.72f, 0.0f, 0.95f));
        parameters_.add(Parameter::makeFloat("threshold", "Threshold", 0.04f, 0.0f, 0.50f));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }

    bool initialize(EffectContext& context) override
    {
        if (!ShaderEffect::initialize(context))
        {
            return false;
        }

        if (context.shaders->shader("passthrough", &lastError_) == nullptr)
        {
            return false;
        }

        if (context.targets == nullptr)
        {
            lastError_ = "Shutter needs a target pool for history";
            return false;
        }

        history_ = &context.targets->persistent(historyKey_);
        if (!history_->valid())
        {
            lastError_ = "Failed to allocate shutter history";
            return false;
        }

        seeded_ = false;
        return true;
    }

    bool process(EffectContext&    context,
                 const GpuTexture& source,
                 GpuTexture&       destination) override
    {
        if (history_ == nullptr || !history_->valid())
        {
            return false;
        }

        const ShaderHandle shutter = context.shaders->shader(shaderName());
        const ShaderHandle blit    = context.shaders->shader("passthrough");
        if (!shutter || !blit)
        {
            return false;
        }

        if (!lastError_.empty())
        {
            lastError_.clear();
        }

        EffectConstants constants;
        packConstants(context, constants);

        if (!seeded_)
        {
            context.fullscreen->draw(destination, blit, &source, constants, SamplerFilter::Point);
            context.fullscreen->draw(*history_, blit, &source, constants, SamplerFilter::Point);
            seeded_ = true;
            return true;
        }

        context.fullscreen->draw(destination, shutter, &source, constants,
                                 SamplerFilter::Linear, history_);
        context.fullscreen->draw(*history_, blit, &destination, constants, SamplerFilter::Point);
        return true;
    }

    void shutdown() override
    {
        history_ = nullptr;
        seeded_  = false;
        ShaderEffect::shutdown();
    }

private:
    std::string  historyKey_;
    GpuTexture*  history_ = nullptr;
    bool         seeded_  = false;
};

} // namespace

std::unique_ptr<Effect> createShutterEffect()
{
    return std::make_unique<ShutterEffect>();
}

} // namespace atemfx
