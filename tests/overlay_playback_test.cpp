#include "overlays/overlay_playback.h"

#include <cmath>
#include <cstdio>

namespace {

using namespace atemfx;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

bool near(float actual, float expected, float tolerance = 0.0001f)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void checkFadeAndReversal()
{
    OverlayPlaybackState state;
    state.configure(OverlayKind::Still, 1, OverlayPlayback::Loop, 30.0f);
    state.setEnabled(true);
    expect(state.phase() == OverlayLayerPhase::Buffering,
           "an enabled layer stays invisible until its first texture is ready");

    state.setReady(true);
    state.advance(kOverlayFadeSeconds * 0.5f);
    expect(state.phase() == OverlayLayerPhase::FadingIn && near(state.fadeAmount(), 0.5f),
           "enable uses the shared 0.35 second eased fade");

    state.setEnabled(false);
    const float beforeReverse = state.fadeAmount();
    state.setEnabled(true);
    expect(near(state.fadeAmount(), beforeReverse),
           "reversing an in-flight fade does not jump opacity");
    state.advance(kOverlayFadeSeconds * 0.5f);
    expect(state.phase() == OverlayLayerPhase::Live && near(state.fadeAmount(), 1.0f),
           "a reversed enable fade lands at full opacity");

    state.setEnabled(false);
    state.advance(kOverlayFadeSeconds);
    expect(state.phase() == OverlayLayerPhase::Disabled && near(state.fadeAmount(), 0.0f),
           "disable fades fully before becoming inactive");
}

void checkThirtyFpsClockAndPause()
{
    OverlayPlaybackState state;
    state.configure(OverlayKind::PngSequence, 300, OverlayPlayback::Loop, 30.0f);
    state.setReady(true);
    state.setEnabled(true);

    // A 59.94 Hz render clock must select one 30 Hz overlay frame for roughly
    // every two video frames; playback is time based, never render-frame based.
    state.advance(1.0f / 59.94f);
    expect(state.desiredFrame() == 0, "first 59.94 Hz tick still displays sequence frame zero");
    state.advance(1.0f / 59.94f);
    expect(state.desiredFrame() == 1, "second 59.94 Hz tick advances a 30 fps sequence");

    state.setPaused(true);
    const std::uint32_t held = state.desiredFrame();
    state.advance(1.0f);
    expect(state.desiredFrame() == held, "pause freezes only the sequence ordinal");
    expect(state.phase() == OverlayLayerPhase::Live && near(state.fadeAmount(), 1.0f),
           "the visibility fade keeps advancing while playback is paused");

    state.setPaused(false);
    state.advance(1.0f / 30.0f);
    expect(state.desiredFrame() != held, "resume continues the clock without a restart");
}

void checkOneShotLifecycle()
{
    OverlayPlaybackState state;
    state.configure(OverlayKind::PngSequence, 3, OverlayPlayback::OneShot, 10.0f);
    state.setReady(true);
    state.setEnabled(true);

    state.advance(kOverlayFadeSeconds);
    expect(state.desiredFrame() == 2 && state.phase() == OverlayLayerPhase::FadingOut,
           "a one-shot holds its final frame while beginning the automatic fade");

    bool autoDisabled = false;
    for (int i = 0; i < 30 && !autoDisabled; ++i)
    {
        autoDisabled = state.advance(1.0f / 60.0f);
    }
    expect(autoDisabled && state.phase() == OverlayLayerPhase::Disabled,
           "one-shot reports exactly when its fade has completed");
    expect(!state.advance(1.0f), "one-shot auto-disable is reported only once");

    state.restart();
    expect(state.desiredFrame() == 0 && state.phase() == OverlayLayerPhase::FadingIn,
           "trigger restarts an ended one-shot at frame zero");
}

void checkBounds()
{
    OverlayPlaybackState state;
    state.configure(OverlayKind::PngSequence, 5, OverlayPlayback::Loop, 500.0f);
    state.setReady(true);
    state.setEnabled(true);
    state.advance(1.0f / 30.0f);
    expect(state.desiredFrame() == 1, "playback rate is capped at 30 fps");

    state.configure(OverlayKind::PngSequence, 5, OverlayPlayback::Loop, -1.0f);
    state.setReady(true);
    state.setEnabled(true);
    state.advance(1.0f);
    expect(state.desiredFrame() == 1, "playback rate is clamped to at least 1 fps");
}

} // namespace

int main()
{
    checkFadeAndReversal();
    checkThirtyFpsClockAndPause();
    checkOneShotLifecycle();
    checkBounds();

    if (failures == 0)
    {
        std::printf("overlay playback: %d checks passed\n", checks);
    }
    return failures == 0 ? 0 : 1;
}
