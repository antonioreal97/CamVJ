#pragma once

#include "tracking/TrackingSnapshot.h"

#include <algorithm>
#include <cmath>

namespace atemfx {

// UV width/height of a crop with `outputAspect` sitting on a canvas of
// `canvasAspect`. 16:9 on 16:9 is 1; 9:16 on 16:9 is 81/256. Clamped so a
// zoom of 1 always fits inside the frame.
inline float framingUvRatio(float outputAspect, float canvasAspect) noexcept
{
    const float canvas = (std::isfinite(canvasAspect) && canvasAspect > 0.1f && canvasAspect < 10.0f)
                             ? canvasAspect
                             : (16.0f / 9.0f);
    const float output = (std::isfinite(outputAspect) && outputAspect > 0.1f && outputAspect < 10.0f)
                             ? outputAspect
                             : (16.0f / 9.0f);
    return std::clamp(output / canvas, 0.05f, 1.0f);
}

// The rectangle the framing is looking at, in normalized frame coordinates
// with the origin at the top left. Half extents: the whole 16:9 frame is
// {0.5, 0.5, 0.5, 0.5}. A 9:16 crop of that frame is narrower in UV
// (halfWidth < halfHeight) so the pixels stay square.
struct FramingRect
{
    float centerX    = 0.5f;
    float centerY    = 0.5f;
    float halfWidth  = 0.5f;
    float halfHeight = 0.5f;
};

// Largest crop of `outputAspect` that fits the canvas, centred. That is the
// letterbox window the Auto Frame shader writes into: full frame for 16:9,
// a centred vertical strip for 9:16.
inline FramingRect framingOutputWindow(float outputAspect, float canvasAspect) noexcept
{
    FramingRect rect;
    rect.centerX    = 0.5f;
    rect.centerY    = 0.5f;
    rect.halfHeight = 0.5f;
    rect.halfWidth  = 0.5f * framingUvRatio(outputAspect, canvasAspect);
    return rect;
}

// What the operator asked for. Every field is a parameter of the Auto Frame
// effect, so this struct is filled from a ParameterSet once per frame.
struct FramingSettings
{
    bool follow = true;

    // Subject height as a fraction of the framed height. Bigger is tighter.
    float subjectSize = 0.55f;

    // Space above the subject, as a fraction of the framed height. This is the
    // vertical half of the composition: raising it moves the subject down the
    // frame. Measured from the subject's top because that is what a camera
    // operator actually keeps constant.
    float headroom = 0.12f;

    // Where the subject sits horizontally, as a fraction of the framed width
    // away from centre. 0 centres them; positive puts them right of centre,
    // which means the frame itself moves left. This is the looking room a
    // presenter facing across the stage needs, and it is the horizontal half
    // of the composition that `headroom` covers vertically. Without it the
    // subject was welded to the middle of the outgoing picture.
    float subjectOffsetX = 0.0f;

    // How far the subject may drift, in fractions of the current half extent,
    // before the framing starts moving. This is the single most important
    // setting on an LED wall: without it the frame follows every twitch of the
    // detector and the picture never sits still.
    float deadZone = 0.10f;

    // Seconds for the framing to cover most of the distance to its target.
    float smoothingSeconds = 0.6f;

    // Ceiling on framing speed, in frame widths per second. A move that is
    // late looks deliberate; a move that is fast looks like a mistake.
    float maxSpeed = 0.5f;

    // Seconds to hold the last framing after the subject disappears, then
    // seconds for the ease back out to the home crop of the output format.
    float holdSeconds   = 2.0f;
    float returnSeconds = 3.0f;

    // Ceiling on how far the framing may punch in. Cropping throws away
    // resolution: at 1920x1080 in, a zoom of 2 sends 960x540 real pixels to
    // the wall.
    float maxZoom = 1.8f;

    // Used while `follow` is off, so the effect stays useful as a plain
    // crop-and-pan and so turning tracking off eases out instead of snapping.
    float manualZoom    = 1.0f;
    float manualCenterX = 0.5f;
    float manualCenterY = 0.5f;

    // Pixel aspect of the canvas the crop lives on, and of the picture the
    // wall should receive. Defaults keep a 16:9 crop of a 16:9 frame, which
    // is a square in UV. 9:16 on 16:9 is a centred vertical strip.
    float canvasAspect = 16.0f / 9.0f;
    float outputAspect = 16.0f / 9.0f;
};

// A camera operator expressed as scalar arithmetic.
//
// Detection is jittery, intermittent and occasionally wrong. Sending it
// straight to a crop rectangle produces exactly the seasick picture that gets
// auto-framing banned from live shows. Everything that makes the result
// watchable lives here: a dead zone, a speed limit, an exponential ease, and
// an explicit answer to "the subject just vanished".
//
// No GPU, no platform, no allocation: this is deterministic and covered by
// tests/framing_test.cpp.
class FramingController
{
public:
    // Called once per frame with the frame's tracking snapshot. `deltaSeconds`
    // is clamped internally, so a stall in the host application slows the
    // framing down instead of teleporting it.
    FramingRect update(const TrackingSnapshot& tracking,
                       const FramingSettings&  settings,
                       float                   deltaSeconds) noexcept;

    FramingRect current() const noexcept;

    // True while the framing is steering from a subject rather than holding,
    // returning or following the manual controls. For the status line.
    bool locked() const noexcept { return locked_; }

    // Seconds since the last valid observation, for the status line.
    float secondsSinceSubject() const noexcept { return sinceSubject_; }

    void reset() noexcept;

private:
    // Where the framing would sit if the subject stood still and the framing
    // could move instantly. `acquire` is a fresh lock: go to the subject
    // now, rather than waiting for them to walk out of the dead zone of the
    // whole frame — that is what puts a presenter on the LED instead of
    // punching in on the empty centre of the stage.
    void retarget(const TrackingSnapshot& tracking,
                  const FramingSettings&  settings,
                  bool                    acquire) noexcept;

    float centerX_ = 0.5f;
    float centerY_ = 0.5f;
    float zoom_    = 1.0f;

    // UV width/height of the crop. 1 for a 16:9 crop of a 16:9 canvas.
    float uvRatio_ = 1.0f;

    float targetCenterX_ = 0.5f;
    float targetCenterY_ = 0.5f;
    float targetZoom_    = 1.0f;

    // Last composition the operator asked for. The dead zone must ignore
    // detector twitch, not a slider the operator just moved.
    bool  compositionValid_   = false;
    float lastSubjectSize_    = 0.0f;
    float lastHeadroom_       = 0.0f;
    float lastOffsetX_        = 0.0f;
    float lastMaxZoom_        = 0.0f;
    float lastOutputAspect_   = 0.0f;

    // Hysteresis: the framing commits to a move once the subject leaves the
    // dead zone and keeps going until it is back near the centre, instead of
    // stopping the instant the subject re-enters the zone.
    bool  chasing_      = false;
    bool  locked_       = false;
    float sinceSubject_ = 0.0f;
};

} // namespace atemfx
