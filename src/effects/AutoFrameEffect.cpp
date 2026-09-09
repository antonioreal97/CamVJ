#include "effects/BuiltinEffects.h"
#include "effects/ShaderEffect.h"
#include "tracking/framing.h"

namespace atemfx {

namespace {

constexpr float kLandscapeAspect = 16.0f / 9.0f;
constexpr float kPortraitAspect  = 9.0f / 16.0f;

// Keeps the subject in frame by cropping and moving the crop, the way a camera
// operator would: a dead zone so small movements cost nothing, a speed limit
// so corrections read as deliberate, and an explicit answer to the subject
// walking out of shot.
//
// The effect is a single crop on the GPU. The judgement lives in
// FramingController (src/tracking/framing.cpp), which is scalar arithmetic
// with tests. The subject's position arrives through EffectContext, snapshotted
// once per frame like every other control-plane value, so the effect never
// knows whether a person, an operator or nothing at all is driving it.
class AutoFrameEffect final : public ShaderEffect
{
public:
    EffectRole role() const override { return EffectRole::Framing; }

    AutoFrameEffect()
        : ShaderEffect({"auto_frame", "Auto Frame", "Framing",
                        "Follows the subject, keeping them framed."},
                       "auto_frame",
                       SamplerFilter::Linear)
    {
        parameters_.add(Parameter::makeBool("follow", "Follow Subject", true));
        parameters_.add(Parameter::makeBool("portrait", "Portrait (9:16)", false));
        parameters_.add(Parameter::makeFloat("size", "Subject Size", 0.55f, 0.20f, 0.90f));
        parameters_.add(Parameter::makeFloat("headroom", "Headroom", 0.12f, 0.0f, 0.40f));
        parameters_.add(Parameter::makeFloat("offset_x", "Offset X", 0.0f, -0.40f, 0.40f));
        parameters_.add(Parameter::makeFloat("dead_zone", "Dead Zone", 0.10f, 0.0f, 0.50f));
        parameters_.add(Parameter::makeFloat("smoothing", "Smoothing (s)", 0.60f, 0.05f, 3.0f));
        parameters_.add(Parameter::makeFloat("max_speed", "Max Speed", 0.50f, 0.05f, 2.0f));
        parameters_.add(Parameter::makeFloat("hold", "Hold (s)", 2.0f, 0.0f, 10.0f));
        parameters_.add(Parameter::makeFloat("return", "Return (s)", 3.0f, 0.10f, 10.0f));
        parameters_.add(Parameter::makeFloat("max_zoom", "Max Zoom", 1.8f, 1.0f, 3.0f));
        parameters_.add(Parameter::makeFloat("zoom", "Manual Zoom (Follow off)", 1.0f, 1.0f, 3.0f));
        parameters_.add(Parameter::makeFloat("center_x", "Manual X (Follow off)", 0.5f, 0.0f, 1.0f));
        parameters_.add(Parameter::makeFloat("center_y", "Manual Y (Follow off)", 0.5f, 0.0f, 1.0f));
    }

    bool process(EffectContext&    context,
                 const GpuTexture& source,
                 GpuTexture&       destination) override
    {
        // Advancing here rather than in packConstants keeps the framing's
        // clock tied to frames actually rendered. A bypassed node freezes
        // where it stood and eases on from there when it comes back, instead
        // of jumping to wherever the subject went in the meantime.
        FramingSettings configured = settings();
        if (context.height > 0)
        {
            configured.canvasAspect =
                static_cast<float>(context.width) / static_cast<float>(context.height);
        }

        rect_ = controller_.update(context.tracking, configured, context.deltaTime);

        context.framing       = rect_;
        context.framingActive = true;
        context.outputAspect  = configured.outputAspect;

        return ShaderEffect::process(context, source, destination);
    }

protected:
    void packConstants(const EffectContext& context, EffectConstants& constants) const override
    {
        setFrameConstants(constants, context.width, context.height, context.time, context.deltaTime);

        // The operator's parameters steer the controller, not the shader: what
        // the shader needs is the source crop and the (fixed) output window.
        setParameterConstant(constants, 0, rect_.centerX);
        setParameterConstant(constants, 1, rect_.centerY);
        setParameterConstant(constants, 2, rect_.halfWidth);
        setParameterConstant(constants, 3, rect_.halfHeight);

        const float canvasAspect = context.height > 0
            ? static_cast<float>(context.width) / static_cast<float>(context.height)
            : kLandscapeAspect;
        const FramingRect window = framingOutputWindow(outputAspect(), canvasAspect);
        setParameterConstant(constants, 4, window.halfWidth);
        setParameterConstant(constants, 5, window.halfHeight);
    }

private:
    float outputAspect() const
    {
        return parameters_.valueOr("portrait", 0.0f) >= 0.5f ? kPortraitAspect : kLandscapeAspect;
    }

    FramingSettings settings() const
    {
        FramingSettings settings;
        settings.follow           = parameters_.valueOr("follow", 1.0f) >= 0.5f;
        settings.outputAspect     = outputAspect();
        settings.subjectSize      = parameters_.valueOr("size", 0.55f);
        settings.headroom         = parameters_.valueOr("headroom", 0.12f);
        settings.subjectOffsetX   = parameters_.valueOr("offset_x", 0.0f);
        settings.deadZone         = parameters_.valueOr("dead_zone", 0.10f);
        settings.smoothingSeconds = parameters_.valueOr("smoothing", 0.60f);
        settings.maxSpeed         = parameters_.valueOr("max_speed", 0.50f);
        settings.holdSeconds      = parameters_.valueOr("hold", 2.0f);
        settings.returnSeconds    = parameters_.valueOr("return", 3.0f);
        settings.maxZoom          = parameters_.valueOr("max_zoom", 1.8f);
        settings.manualZoom       = parameters_.valueOr("zoom", 1.0f);
        settings.manualCenterX    = parameters_.valueOr("center_x", 0.5f);
        settings.manualCenterY    = parameters_.valueOr("center_y", 0.5f);
        return settings;
    }

    FramingController controller_;
    FramingRect       rect_;
};

} // namespace

std::unique_ptr<Effect> createAutoFrameEffect()
{
    return std::make_unique<AutoFrameEffect>();
}

} // namespace atemfx
