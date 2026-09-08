#include "tracking/framing.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int failures = 0;
int checks   = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

bool near(float actual, float expected, float tolerance)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

constexpr float kFrameSeconds = 1.0f / 60.0f;

atemfx::TrackingSnapshot subject(float centerX, float centerY, float width, float height)
{
    atemfx::TrackingSnapshot tracking;
    tracking.available  = true;
    tracking.valid      = true;
    tracking.centerX    = centerX;
    tracking.centerY    = centerY;
    tracking.width      = width;
    tracking.height     = height;
    tracking.confidence = 0.9f;
    return tracking;
}

atemfx::FramingRect run(atemfx::FramingController&      controller,
                        const atemfx::TrackingSnapshot& tracking,
                        const atemfx::FramingSettings&  settings,
                        float                           seconds)
{
    atemfx::FramingRect rect;
    const int           frames = static_cast<int>(seconds / kFrameSeconds);
    for (int i = 0; i < frames; ++i)
    {
        rect = controller.update(tracking, settings, kFrameSeconds);
    }
    return rect;
}

bool inside(const atemfx::FramingRect& rect)
{
    return rect.centerX - rect.halfWidth >= -0.0001f &&
           rect.centerX + rect.halfWidth <= 1.0001f &&
           rect.centerY - rect.halfHeight >= -0.0001f &&
           rect.centerY + rect.halfHeight <= 1.0001f;
}

void checkStartsOnTheWholeFrame()
{
    atemfx::FramingController controller;
    const atemfx::FramingRect rect = controller.current();

    expect(near(rect.centerX, 0.5f, 0.0f) && near(rect.centerY, 0.5f, 0.0f),
           "framing starts centred");
    expect(near(rect.halfWidth, 0.5f, 0.0f) && near(rect.halfHeight, 0.5f, 0.0f),
           "framing starts on the whole frame");
    expect(!controller.locked(), "framing starts unlocked");
}

void checkNothingToTrackChangesNothing()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    const atemfx::FramingRect rect = run(controller, {}, settings, 5.0f);

    expect(near(rect.centerX, 0.5f, 0.001f) && near(rect.halfWidth, 0.5f, 0.001f),
           "a tracker that sees nothing leaves the whole frame alone");
    expect(!controller.locked(), "no subject means not locked");
}

void checkAcquireCentresTheSubject()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    // 0.04 of the frame against a full-frame half extent of 0.5 is 8%, inside
    // the 10% dead zone. Without an acquire commit the crop would punch in on
    // the empty centre and leave the speaker off-centre on the wall.
    const atemfx::FramingRect rect =
        run(controller, subject(0.54f, 0.5f, 0.15f, 0.3f), settings, 8.0f);

    expect(near(rect.centerX, 0.54f, 0.02f),
           "a fresh lock aims at the subject, not the centre of the stage");
    expect(controller.locked(), "acquiring a subject reports locked");
    expect(inside(rect), "the acquire crop stays inside the frame");
}

void checkDeadZoneHoldsTheFrameStill()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    // A subject small enough that the framing has room to move at all: at
    // maxZoom 1.8 the crop can travel about a quarter of the frame.
    run(controller, subject(0.5f, 0.5f, 0.15f, 0.3f), settings, 5.0f);
    const atemfx::FramingRect settled = controller.current();

    // Well inside the dead zone: 0.02 of the frame against a half extent of
    // about 0.28 is 7%, and the dead zone is 10%.
    const atemfx::FramingRect drifted =
        run(controller, subject(0.52f, 0.5f, 0.15f, 0.3f), settings, 1.0f);

    expect(near(drifted.centerX, settled.centerX, 0.002f),
           "a subject shifting inside the dead zone does not move the framing");
}

void checkItFollowsBeyondTheDeadZone()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    run(controller, subject(0.5f, 0.5f, 0.15f, 0.3f), settings, 5.0f);
    const atemfx::FramingRect rect =
        run(controller, subject(0.7f, 0.5f, 0.15f, 0.3f), settings, 8.0f);

    expect(near(rect.centerX, 0.7f, 0.02f), "the framing catches up with the subject");
    expect(controller.locked(), "following a subject reports locked");
    expect(inside(rect), "the crop stays inside the frame while following");
}

void checkHeadroom()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    const atemfx::FramingRect rect =
        run(controller, subject(0.5f, 0.5f, 0.15f, 0.3f), settings, 8.0f);

    const float framedHeight = rect.halfHeight * 2.0f;
    const float cropTop      = rect.centerY - rect.halfHeight;
    const float subjectTop   = 0.5f - 0.15f;

    expect(near((subjectTop - cropTop) / framedHeight, settings.headroom, 0.01f),
           "the space above the subject matches the headroom setting");
}

void checkSubjectSizeAndZoomCeiling()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.subjectSize = 0.5f;
    settings.maxZoom     = 3.0f;

    const atemfx::FramingRect rect =
        run(controller, subject(0.5f, 0.5f, 0.2f, 0.4f), settings, 10.0f);

    expect(near(0.4f / (rect.halfHeight * 2.0f), settings.subjectSize, 0.02f),
           "the subject fills the requested fraction of the framed height");

    atemfx::FramingController tight;
    atemfx::FramingSettings   ceiling;
    ceiling.maxZoom = 1.8f;

    const atemfx::FramingRect punched =
        run(tight, subject(0.5f, 0.5f, 0.02f, 0.05f), ceiling, 10.0f);

    expect(punched.halfWidth >= 0.5f / ceiling.maxZoom - 0.001f,
           "a tiny subject does not punch in past the zoom ceiling");
}

void checkTheCropNeverLeavesTheFrame()
{
    atemfx::FramingSettings settings;
    settings.maxZoom = 3.0f;

    for (int step = 0; step <= 20; ++step)
    {
        const float position = static_cast<float>(step) / 20.0f;

        atemfx::FramingController controller;
        const atemfx::FramingRect rect =
            run(controller, subject(position, position, 0.1f, 0.2f), settings, 10.0f);

        expect(inside(rect), "the crop stays inside the frame for a subject at any position");
    }
}

void checkSpeedLimit()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.maxSpeed         = 0.2f;
    settings.smoothingSeconds = 0.05f;  // as good as instant, so only the limit remains

    run(controller, subject(0.5f, 0.5f, 0.15f, 0.3f), settings, 5.0f);

    const atemfx::TrackingSnapshot moved = subject(0.9f, 0.5f, 0.15f, 0.3f);

    float previous = controller.current().centerX;
    for (int i = 0; i < 60; ++i)
    {
        const float centerX = controller.update(moved, settings, kFrameSeconds).centerX;
        expect(std::abs(centerX - previous) <= settings.maxSpeed * kFrameSeconds + 0.0001f,
               "no frame moves the framing faster than the speed limit");
        previous = centerX;
    }
}

void checkAStallDoesNotTeleportTheFraming()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    run(controller, subject(0.5f, 0.5f, 0.15f, 0.3f), settings, 5.0f);
    const float before = controller.current().centerX;

    // Five seconds of frame time in one update: a stalled host, not a move.
    const float after = controller.update(subject(0.9f, 0.5f, 0.15f, 0.3f), settings, 5.0f).centerX;

    expect(std::abs(after - before) <= settings.maxSpeed * 0.25f + 0.0001f,
           "a long frame moves the framing by at most the clamped step");
}

void checkHoldThenReturn()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    run(controller, subject(0.75f, 0.5f, 0.15f, 0.3f), settings, 8.0f);
    const atemfx::FramingRect tracked = controller.current();

    atemfx::TrackingSnapshot lost;
    lost.available = true;
    lost.valid     = false;

    const atemfx::FramingRect held = run(controller, lost, settings, settings.holdSeconds * 0.7f);
    expect(near(held.centerX, tracked.centerX, 0.005f) &&
               near(held.halfWidth, tracked.halfWidth, 0.005f),
           "the framing holds where it was while the subject is briefly lost");
    expect(!controller.locked(), "a lost subject reports unlocked");

    const atemfx::FramingRect returned = run(controller, lost, settings, 20.0f);
    expect(near(returned.centerX, 0.5f, 0.02f) && near(returned.centerY, 0.5f, 0.02f) &&
               near(returned.halfWidth, 0.5f, 0.02f),
           "a subject that does not come back eases the framing out to the whole frame");
}

void checkManualFraming()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.follow        = false;
    settings.manualZoom    = 2.0f;
    settings.manualCenterX = 0.3f;
    settings.manualCenterY = 0.4f;

    const atemfx::FramingRect rect =
        run(controller, subject(0.9f, 0.9f, 0.2f, 0.4f), settings, 20.0f);

    expect(near(rect.halfWidth, 0.25f, 0.005f), "manual zoom sets the crop size");
    expect(near(rect.centerX, 0.3f, 0.01f) && near(rect.centerY, 0.4f, 0.01f),
           "manual framing ignores the subject");
    expect(!controller.locked(), "manual framing reports unlocked");
}

void checkPortraitKeepsAspect()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.canvasAspect = 16.0f / 9.0f;
    settings.outputAspect = 9.0f / 16.0f;

    const float ratio = atemfx::framingUvRatio(settings.outputAspect, settings.canvasAspect);

    const atemfx::FramingRect home = run(controller, {}, settings, 5.0f);
    expect(near(home.halfHeight, 0.5f, 0.001f), "portrait home is full canvas height");
    expect(near(home.halfWidth, 0.5f * ratio, 0.001f),
           "portrait home is the centred 9:16 strip, not the whole frame");
    expect(near(home.halfWidth / home.halfHeight, ratio, 0.001f),
           "portrait home keeps the 9:16 UV ratio");
    expect(inside(home), "portrait home stays inside the frame");
}

void checkPortraitFollowsWithAspect()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.canvasAspect = 16.0f / 9.0f;
    settings.outputAspect = 9.0f / 16.0f;
    settings.maxZoom      = 2.5f;

    const float ratio = atemfx::framingUvRatio(settings.outputAspect, settings.canvasAspect);

    const atemfx::FramingRect rect =
        run(controller, subject(0.45f, 0.5f, 0.12f, 0.35f), settings, 10.0f);

    expect(near(rect.halfWidth / rect.halfHeight, ratio, 0.002f),
           "a portrait crop keeps the 9:16 UV ratio while following");
    expect(inside(rect), "a portrait crop stays inside the frame while following");
    expect(controller.locked(), "portrait following reports locked");
}

void checkPortraitReturnStaysPortrait()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.canvasAspect = 16.0f / 9.0f;
    settings.outputAspect = 9.0f / 16.0f;

    const float ratio = atemfx::framingUvRatio(settings.outputAspect, settings.canvasAspect);

    run(controller, subject(0.4f, 0.5f, 0.12f, 0.35f), settings, 8.0f);

    atemfx::TrackingSnapshot lost;
    lost.available = true;
    lost.valid     = false;

    const atemfx::FramingRect returned = run(controller, lost, settings, 20.0f);
    expect(near(returned.centerX, 0.5f, 0.02f) && near(returned.centerY, 0.5f, 0.02f),
           "portrait return recentres");
    expect(near(returned.halfHeight, 0.5f, 0.02f), "portrait return opens to full height");
    expect(near(returned.halfWidth, 0.5f * ratio, 0.02f),
           "portrait return stays on the 9:16 strip, not the whole 16:9 frame");
}

void checkOperatorSizeSliderMovesTheCrop()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.maxZoom = 3.0f;

    const atemfx::TrackingSnapshot locked = subject(0.5f, 0.5f, 0.2f, 0.4f);
    run(controller, locked, settings, 8.0f);
    const float loose = controller.current().halfHeight;

    settings.subjectSize = 0.85f;
    const atemfx::FramingRect tight = run(controller, locked, settings, 8.0f);

    expect(tight.halfHeight < loose - 0.02f,
           "raising Subject Size after lock tightens the crop");
}

void checkOperatorHeadroomSliderMovesTheCrop()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    const atemfx::TrackingSnapshot locked = subject(0.5f, 0.5f, 0.15f, 0.3f);
    run(controller, locked, settings, 8.0f);
    const float lowHeadroom = controller.current().centerY;

    settings.headroom = 0.35f;
    const atemfx::FramingRect raised = run(controller, locked, settings, 8.0f);

    expect(raised.centerY < lowHeadroom - 0.01f,
           "raising Headroom after lock shifts the crop up");
}

void checkLandscapeFollowPunchesInOnACloseSubject()
{
    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;

    // Webcam close-up: the expanded face box spans most of the 16:9 frame.
    // Without a minimum follow zoom, the crop is the whole picture and
    // PROGRAM equals SOURCE — tracking looks dead, unlike 9:16.
    const atemfx::FramingRect rect =
        run(controller, subject(0.55f, 0.5f, 0.95f, 0.65f), settings, 8.0f);

    expect(rect.halfHeight < 0.45f && rect.halfWidth < 0.45f,
           "16:9 follow punches in on a close subject instead of filling the frame");
    expect(near(rect.centerX, 0.55f, 0.05f),
           "16:9 follow still centres the close subject");
    expect(inside(rect), "the punched-in 16:9 crop stays inside the frame");
}

void checkSmoothingSlowsTheMove()
{
    atemfx::FramingSettings fast;
    fast.smoothingSeconds = 0.05f;
    fast.maxSpeed         = 8.0f;

    atemfx::FramingSettings slow = fast;
    slow.smoothingSeconds        = 2.0f;

    atemfx::FramingController a;
    atemfx::FramingController b;
    run(a, subject(0.5f, 0.5f, 0.15f, 0.3f), fast, 5.0f);
    run(b, subject(0.5f, 0.5f, 0.15f, 0.3f), slow, 5.0f);

    const atemfx::TrackingSnapshot walked = subject(0.8f, 0.5f, 0.15f, 0.3f);
    const float afterFast = run(a, walked, fast, 0.4f).centerX;
    const float afterSlow = run(b, walked, slow, 0.4f).centerX;

    expect(afterFast > afterSlow + 0.03f,
           "longer Smoothing covers less distance in the same time");
}

void checkNonFiniteInputsAreSurvivable()
{
    const float nan      = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    atemfx::FramingController controller;
    atemfx::FramingSettings   settings;
    settings.subjectSize      = nan;
    settings.headroom         = infinity;
    settings.deadZone         = nan;
    settings.smoothingSeconds = nan;
    settings.maxSpeed         = -infinity;
    settings.maxZoom          = nan;

    atemfx::TrackingSnapshot broken = subject(nan, infinity, nan, nan);

    for (int i = 0; i < 120; ++i)
    {
        const atemfx::FramingRect rect = controller.update(broken, settings, nan);
        expect(std::isfinite(rect.centerX) && std::isfinite(rect.centerY) &&
                   std::isfinite(rect.halfWidth) && std::isfinite(rect.halfHeight),
               "a broken observation never produces a broken crop");
        expect(inside(rect), "a broken observation never moves the crop out of the frame");
    }
}

} // namespace

int main()
{
    checkStartsOnTheWholeFrame();
    checkNothingToTrackChangesNothing();
    checkAcquireCentresTheSubject();
    checkDeadZoneHoldsTheFrameStill();
    checkItFollowsBeyondTheDeadZone();
    checkHeadroom();
    checkSubjectSizeAndZoomCeiling();
    checkTheCropNeverLeavesTheFrame();
    checkSpeedLimit();
    checkAStallDoesNotTeleportTheFraming();
    checkHoldThenReturn();
    checkManualFraming();
    checkPortraitKeepsAspect();
    checkPortraitFollowsWithAspect();
    checkPortraitReturnStaysPortrait();
    checkOperatorSizeSliderMovesTheCrop();
    checkOperatorHeadroomSliderMovesTheCrop();
    checkLandscapeFollowPunchesInOnACloseSubject();
    checkSmoothingSlowsTheMove();
    checkNonFiniteInputsAreSurvivable();

    std::printf("framing: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
