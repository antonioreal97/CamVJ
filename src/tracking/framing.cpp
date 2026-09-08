#include "tracking/framing.h"

#include <algorithm>
#include <cmath>

namespace atemfx {

namespace {

// A frame time this long means the host stalled. Letting it through would move
// the framing by whatever the stall was worth, which on a wall reads as a jump
// cut. Slowing down instead is always the safer failure.
constexpr float kMaxDeltaSeconds = 0.25f;

constexpr float kMinTimeConstant = 0.01f;

// Fraction of the dead zone the subject must return to before the framing
// stops chasing. Stopping at the dead zone boundary would leave the subject
// pinned to the edge and re-trigger on the next twitch.
constexpr float kSettleFraction = 0.2f;

// A 16:9 crop at zoom 1 fills the canvas and cannot pan, so PROGRAM equals
// SOURCE and tracking looks dead. Portrait does not have this problem: its
// home window is already a strip. Used only when following would otherwise
// sit on the whole frame — a close webcam, or a face box expanded to the
// edges.
constexpr float kLandscapeMinFollowZoom = 1.5f;

float clampFinite(float value, float low, float high, float fallback) noexcept
{
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, low, high);
}

// Frame-rate independent ease: the fraction of the remaining distance to cover
// this frame, given a time constant. At dt == tau it covers about 63%.
float easeFactor(float deltaSeconds, float timeConstant) noexcept
{
    return 1.0f - std::exp(-deltaSeconds / std::max(timeConstant, kMinTimeConstant));
}

float approach(float current, float target, float factor, float maxStep) noexcept
{
    float step = (target - current) * factor;
    step       = std::clamp(step, -maxStep, maxStep);
    return current + step;
}

} // namespace

void FramingController::reset() noexcept
{
    *this = FramingController{};
}

FramingRect FramingController::current() const noexcept
{
    const float halfHeight = 0.5f / zoom_;
    const float halfWidth  = halfHeight * uvRatio_;

    FramingRect rect;
    rect.centerX    = centerX_;
    rect.centerY    = centerY_;
    rect.halfWidth  = halfWidth;
    rect.halfHeight = halfHeight;
    return rect;
}

void FramingController::retarget(const TrackingSnapshot& tracking,
                                 const FramingSettings&  settings,
                                 bool                    acquire) noexcept
{
    const float subjectSize = clampFinite(settings.subjectSize, 0.05f, 1.0f, 0.55f);
    const float headroom    = clampFinite(settings.headroom, 0.0f, 0.45f, 0.12f);
    const float deadZone    = clampFinite(settings.deadZone, 0.0f, 0.9f, 0.1f);
    const float maxZoom     = clampFinite(settings.maxZoom, 1.0f, 8.0f, 1.8f);

    const float subjectWidth  = clampFinite(tracking.width, 0.0f, 1.0f, 0.0f);
    const float subjectHeight = clampFinite(tracking.height, 0.0f, 1.0f, 0.0f);

    // Height follows Subject Size. Width only opens the crop far enough that a
    // wide subject — two people, a banner — is not sliced by the output format.
    // A box clamped to the full frame width (a close face expanded to "body")
    // must not force zoom 1: that is why 16:9 PROGRAM matched SOURCE.
    float framedHeight = std::max(subjectHeight, 0.02f) / subjectSize;
    if (subjectWidth < 0.98f)
    {
        framedHeight = std::max(framedHeight, subjectWidth / std::max(uvRatio_, 1.0e-4f));
    }
    framedHeight = std::clamp(framedHeight, 1.0f / maxZoom, 1.0f);

    // Landscape follow: if the crop is still the whole frame it cannot move.
    if (settings.follow && uvRatio_ > 0.95f && framedHeight > 0.85f)
    {
        const float minZoom = std::min(maxZoom, kLandscapeMinFollowZoom);
        framedHeight        = std::min(framedHeight, 1.0f / minZoom);
    }

    const float halfHeight = framedHeight * 0.5f;
    const float halfWidth  = halfHeight * uvRatio_;

    // Headroom is measured from the top of the subject, which is what a camera
    // operator actually keeps constant. Centring on the subject's middle puts
    // a standing person's head at the top edge as soon as the frame tightens.
    const float subjectTop = clampFinite(tracking.centerY, 0.0f, 1.0f, 0.5f)
                             - subjectHeight * 0.5f;

    float desiredX = clampFinite(tracking.centerX, 0.0f, 1.0f, 0.5f);
    float desiredY = subjectTop - headroom * framedHeight + halfHeight;

    // The crop never leaves the frame. When the subject walks towards the edge
    // the framing stops and lets them move off centre, which is what a real
    // camera does; the alternative is black bars on the wall.
    desiredX = std::clamp(desiredX, halfWidth, 1.0f - halfWidth);
    desiredY = std::clamp(desiredY, halfHeight, 1.0f - halfHeight);

    const float desiredZoom = 1.0f / framedHeight;

    const bool settingsChanged = compositionValid_ &&
        (std::abs(subjectSize - lastSubjectSize_) > 1.0e-5f ||
         std::abs(headroom - lastHeadroom_) > 1.0e-5f ||
         std::abs(maxZoom - lastMaxZoom_) > 1.0e-5f ||
         std::abs(settings.outputAspect - lastOutputAspect_) > 1.0e-5f);

    lastSubjectSize_  = subjectSize;
    lastHeadroom_     = headroom;
    lastMaxZoom_      = maxZoom;
    lastOutputAspect_ = settings.outputAspect;
    compositionValid_ = true;

    // A fresh lock, or the operator moving a composition slider, must aim now.
    // The dead zone is for detector twitch, not for Subject Size or Headroom.
    if (acquire || settingsChanged)
    {
        chasing_       = true;
        targetCenterX_ = desiredX;
        targetCenterY_ = desiredY;
        targetZoom_    = desiredZoom;
        return;
    }

    const float currentHalfHeight = 0.5f / zoom_;
    const float currentHalfWidth  = currentHalfHeight * uvRatio_;
    const float offsetX = std::abs(desiredX - centerX_) / std::max(currentHalfWidth, 1.0e-4f);
    const float offsetY = std::abs(desiredY - centerY_) / std::max(currentHalfHeight, 1.0e-4f);
    const float offset  = std::max(offsetX, offsetY);

    if (!chasing_ && offset > deadZone)
    {
        chasing_ = true;
    }
    else if (chasing_ && offset < deadZone * kSettleFraction)
    {
        chasing_ = false;
    }

    if (chasing_)
    {
        targetCenterX_ = desiredX;
        targetCenterY_ = desiredY;
    }

    // Zoom gets its own dead zone, measured against the target rather than the
    // current value so an ease already under way does not keep re-triggering.
    // Detector boxes breathe by a few percent every frame; without this the
    // picture pulses.
    if (std::abs(desiredZoom - targetZoom_) > deadZone * targetZoom_)
    {
        targetZoom_ = desiredZoom;
    }
}

FramingRect FramingController::update(const TrackingSnapshot& tracking,
                                      const FramingSettings&  settings,
                                      float                   deltaSeconds) noexcept
{
    uvRatio_ = framingUvRatio(settings.outputAspect, settings.canvasAspect);

    const float dt      = clampFinite(deltaSeconds, 0.0f, kMaxDeltaSeconds, 0.0f);
    const float maxZoom = clampFinite(settings.maxZoom, 1.0f, 8.0f, 1.8f);

    float timeConstant = clampFinite(settings.smoothingSeconds, kMinTimeConstant, 60.0f, 0.6f);

    // The zoom ceiling protects the picture from a decision the framing made
    // on its own. An operator turning the manual zoom up has already made that
    // decision, so it does not apply to them.
    float zoomCeiling = maxZoom;

    if (!settings.follow)
    {
        const float manualZoom = clampFinite(settings.manualZoom, 1.0f, 8.0f, 1.0f);
        const float halfHeight = 0.5f / manualZoom;
        const float halfWidth  = halfHeight * uvRatio_;

        zoomCeiling    = manualZoom;
        targetZoom_    = manualZoom;
        targetCenterX_ = std::clamp(clampFinite(settings.manualCenterX, 0.0f, 1.0f, 0.5f),
                                    halfWidth, 1.0f - halfWidth);
        targetCenterY_ = std::clamp(clampFinite(settings.manualCenterY, 0.0f, 1.0f, 0.5f),
                                    halfHeight, 1.0f - halfHeight);

        chasing_ = false;
        locked_  = false;
        sinceSubject_ += dt;
    }
    else if (tracking.valid)
    {
        const bool acquire = !locked_;
        sinceSubject_      = 0.0f;
        locked_            = true;
        retarget(tracking, settings, acquire);
    }
    else
    {
        locked_ = false;
        sinceSubject_ += dt;

        const float hold = clampFinite(settings.holdSeconds, 0.0f, 120.0f, 2.0f);
        if (sinceSubject_ > hold)
        {
            // Give up and open back out to the home crop of this output format:
            // the whole frame for 16:9, the centred 9:16 strip for portrait.
            // Opening to 16:9 while the wall is 9:16 would squeeze the stage
            // into the letterbox.
            targetCenterX_ = 0.5f;
            targetCenterY_ = 0.5f;
            targetZoom_    = 1.0f;
            chasing_       = false;
            timeConstant   = clampFinite(settings.returnSeconds, kMinTimeConstant, 120.0f, 3.0f);
        }
    }

    targetZoom_ = std::clamp(targetZoom_, 1.0f, zoomCeiling);

    const float factor  = easeFactor(dt, timeConstant);
    const float maxStep = clampFinite(settings.maxSpeed, 0.01f, 10.0f, 0.5f) * dt;

    // The speed limit applies to the move as a whole, not to each axis, so a
    // diagonal move is not allowed to travel faster than a straight one.
    const float stepX  = (targetCenterX_ - centerX_) * factor;
    const float stepY  = (targetCenterY_ - centerY_) * factor;
    const float length = std::sqrt(stepX * stepX + stepY * stepY);
    const float scale  = (length > maxStep && length > 0.0f) ? maxStep / length : 1.0f;

    centerX_ += stepX * scale;
    centerY_ += stepY * scale;
    zoom_ = approach(zoom_, targetZoom_, factor, maxStep);

    // Only the lower bound is hard, because a crop larger than the home window
    // of this format would sample outside the frame. A ceiling that drops below
    // where the framing already sits — the operator turning manual zoom off —
    // is reached by easing down to the new target, not by a cut.
    zoom_ = std::max(zoom_, 1.0f);

    const float halfHeight = 0.5f / zoom_;
    const float halfWidth  = halfHeight * uvRatio_;
    centerX_               = std::clamp(centerX_, halfWidth, 1.0f - halfWidth);
    centerY_               = std::clamp(centerY_, halfHeight, 1.0f - halfHeight);

    return current();
}

} // namespace atemfx
