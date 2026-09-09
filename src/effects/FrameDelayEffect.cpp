#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace atemfx {

namespace {

uint32_t nextFrameDelayKey()
{
    static uint32_t next = 0;
    return next++;
}

// Weight the oldest requested copy is left with. The per-step fade is derived
// from it so the operator sets a number of copies instead of a decay rate.
constexpr float kOldestCopyWeight = 0.15f;

// Slot 4 is params[1].x. process() rewrites it per pass; everything else the
// shader reads is the same for all three.
constexpr std::size_t kPassSlot = 4;

constexpr float kPassComposite  = 0.0f;
constexpr float kPassAccumulate = 1.0f;
constexpr float kPassSeed       = 2.0f;

// Delayed copies of the picture, the frame-by-frame echo a dancer leaves
// behind them. Two persistent targets carry the trail and are ping-ponged,
// because a pass cannot read and write the same texture: one holds the copies
// as they stand, the other receives them faded with the live frame stamped in.
//
// The chain skips a bypassed node, so the trail would otherwise come back
// holding whatever was on air when the operator switched it off. A gap in
// frameIndex re-seeds it — enabling the effect starts a new echo rather than
// flashing an old one.
class FrameDelayEffect final : public ShaderEffect
{
public:
    FrameDelayEffect()
        : ShaderEffect({"frame_delay", "Frame Delay", "Temporal",
                        "Delayed copies of past frames, echoing behind the live one."},
                       "frame_delay",
                       SamplerFilter::Point)
        , trailKey_("frame_delay." + std::to_string(nextFrameDelayKey()))
    {
        parameters_.add(Parameter::makeInt("copies", "Copies", 8, 1, 24));
        parameters_.add(Parameter::makeInt("spacing", "Frames Apart", 3, 1, 30));
        // Blend is an int slider for the same reason Mirror's mode is: the
        // parameter system grows enumerations in M3, and an effect must not
        // grow bespoke UI to get one.
        parameters_.add(Parameter::makeInt("blend", "Blend (Lighten/Screen/Darken)", 0, 0, 2));
        parameters_.add(Parameter::makeFloat("key", "Key", 0.10f, 0.0f, 0.80f));
        parameters_.add(Parameter::makeBool("freeze", "Freeze Trail", false));
        parameters_.add(Parameter::makeFloat("mix", "Mix", 1.0f, 0.0f, 1.0f));
    }

    bool initialize(EffectContext& context) override
    {
        if (!ShaderEffect::initialize(context))
        {
            return false;
        }

        if (context.targets == nullptr)
        {
            lastError_ = "Frame Delay needs a target pool for its trail";
            return false;
        }

        trail_[0] = &context.targets->persistent(trailKey_ + ".a");
        trail_[1] = &context.targets->persistent(trailKey_ + ".b");
        if (!trail_[0]->valid() || !trail_[1]->valid())
        {
            lastError_ = "Failed to allocate the frame delay trail";
            return false;
        }

        seeded_ = false;
        return true;
    }

    bool process(EffectContext&    context,
                 const GpuTexture& source,
                 GpuTexture&       destination) override
    {
        if (trail_[0] == nullptr || trail_[1] == nullptr ||
            !trail_[0]->valid() || !trail_[1]->valid())
        {
            return false;
        }

        const ShaderHandle shader = context.shaders->shader(shaderName());
        if (!shader)
        {
            return false;
        }

        if (!lastError_.empty())
        {
            lastError_.clear();
        }

        EffectConstants constants;
        packConstants(context, constants);

        // The node did not run last frame: bypassed, or the input was lost.
        // Either way the trail is stale, so start the echo from this picture.
        const bool resumed = !seeded_ || context.frameIndex != lastFrameIndex_ + 1;
        if (resumed)
        {
            setParameterConstant(constants, kPassSlot, kPassSeed);
            // The trail is bound at slot 1 on every pass, seed included: an
            // unbound texture argument fails Metal validation, and the seed
            // ignores what it samples there.
            context.fullscreen->draw(*trail_[0], shader, &source, constants,
                                     SamplerFilter::Point, &source);
            context.fullscreen->draw(*trail_[1], shader, &source, constants,
                                     SamplerFilter::Point, &source);
            current_         = 0;
            framesSinceStep_ = 0;
            seeded_          = true;
        }

        // Composite before stamping. Folding the live frame in first would
        // make the nearest copy the picture itself, and the echo would only
        // start one step behind where the operator asked for it.
        setParameterConstant(constants, kPassSlot, kPassComposite);
        context.fullscreen->draw(destination, shader, &source, constants,
                                 SamplerFilter::Point, trail_[current_]);

        const int spacing = std::max(1, static_cast<int>(parameters_.valueOr("spacing", 3.0f)));
        const bool frozen = parameters_.valueOr("freeze", 0.0f) >= 0.5f;
        if (!frozen && ++framesSinceStep_ >= spacing)
        {
            const std::size_t next = current_ ^ 1u;
            setParameterConstant(constants, kPassSlot, kPassAccumulate);
            context.fullscreen->draw(*trail_[next], shader, &source, constants,
                                     SamplerFilter::Point, trail_[current_]);
            current_         = next;
            framesSinceStep_ = 0;
        }

        lastFrameIndex_ = context.frameIndex;
        return true;
    }

    void shutdown() override
    {
        trail_[0] = nullptr;
        trail_[1] = nullptr;
        seeded_   = false;
        ShaderEffect::shutdown();
    }

protected:
    void packConstants(const EffectContext& context, EffectConstants& constants) const override
    {
        setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);

        setParameterConstant(constants, 0, parameters_.valueOr("blend", 0.0f));
        setParameterConstant(constants, 1, parameters_.valueOr("key", 0.10f));
        setParameterConstant(constants, 2, parameters_.valueOr("mix", 1.0f));
        setParameterConstant(constants, 3, decayPerCopy());
        setParameterConstant(constants, 4, kPassComposite);
    }

private:
    // copies = 1 leaves one copy at kOldestCopyWeight and the next at its
    // square, which is below anything a projector resolves. Raising the count
    // fades more slowly, so more copies stay on the wall at once.
    float decayPerCopy() const
    {
        const float copies = std::max(1.0f, parameters_.valueOr("copies", 8.0f));
        return std::clamp(std::pow(kOldestCopyWeight, 1.0f / copies), 0.0f, 0.995f);
    }

    std::string trailKey_;
    GpuTexture* trail_[2]        = {nullptr, nullptr};
    std::size_t current_         = 0;
    int         framesSinceStep_ = 0;
    uint64_t    lastFrameIndex_  = 0;
    bool        seeded_          = false;
};

} // namespace

std::unique_ptr<Effect> createFrameDelayEffect()
{
    return std::make_unique<FrameDelayEffect>();
}

} // namespace atemfx
