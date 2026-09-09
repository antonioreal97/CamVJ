#include "video/source_health.h"

#include <cmath>
#include <cstdio>

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

bool near(double actual, double expected, double tolerance = 1.0e-6)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void checkWaitingIsNotLiveOrRepeated()
{
    expect(atemfx::SourceHealth{}.signal == atemfx::SourceSignal::Generated,
           "a default source has a generated signal, not a camera health claim");

    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;
    render.update(capture.snapshot(), false, 0.0);
    render.update(capture.snapshot(), false, 6000.0);

    const auto health = render.snapshot();
    expect(health.signal == atemfx::SourceSignal::Waiting,
           "rendering for any duration before capture remains waiting");
    expect(health.receivedFrames == 0 && health.consumedFrames == 0 &&
               health.overwrittenFrames == 0 && health.repeatedFrames == 0,
           "an empty input neither consumes nor repeats frames");
    expect(health.ageSeconds == 0.0 && health.captureFps == 0.0,
           "waiting has no measured frame age or capture rate");
}

void checkFirstFrameHasAnAgeButNoRateYet()
{
    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;
    capture.recordFrame(10.0, false);
    render.update(capture.snapshot(), true, 10.004);

    const auto health = render.snapshot();
    expect(health.signal == atemfx::SourceSignal::Live,
           "the first fresh captured frame changes waiting to live");
    expect(near(health.ageSeconds, 0.004),
           "age includes time spent waiting between capture and consumption");
    expect(health.captureFps == 0.0,
           "one captured frame cannot establish a capture rate");
    expect(health.receivedFrames == 1 && health.consumedFrames == 1,
           "the first frame is received and consumed exactly once");
}

void checkCapture30Render60()
{
    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;

    for (int tick = 0; tick < 180; ++tick)
    {
        const double now = 1000.0 + static_cast<double>(tick) / 60.0;
        const bool newFrame = tick % 2 == 0;
        if (newFrame)
        {
            capture.recordFrame(now, false);
        }
        render.update(capture.snapshot(), newFrame, now + 0.002);
    }

    const auto health = render.snapshot();
    expect(health.signal == atemfx::SourceSignal::Live,
           "a 30 fps camera remains live while rendering at 60 fps");
    expect(near(health.captureFps, 30.0),
           "capture rate follows real camera arrival time, not rendering frequency");
    expect(health.receivedFrames == 90 && health.consumedFrames == 90,
           "only distinct camera frames contribute to received and consumed counts");
    expect(health.repeatedFrames == 90 && health.overwrittenFrames == 0,
           "the expected 30-to-60 fps repeats are separate from overwritten frames");
    expect(near(health.ageSeconds, 1.0 / 60.0 + 0.002),
           "a repeated render retains the original arrival time");
}

void checkSilenceAndRecovery()
{
    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;
    capture.recordFrame(20.0, false);
    render.update(capture.snapshot(), true, 20.0);
    capture.recordFrame(20.0 + 1.0 / 30.0, false);
    render.update(capture.snapshot(), true, 20.0 + 1.0 / 30.0);

    const double lastArrival = capture.snapshot().lastArrivalSeconds;
    render.update(capture.snapshot(), false, lastArrival + 0.499);
    expect(render.snapshot().signal == atemfx::SourceSignal::Live,
           "an input below the silence threshold is still live");

    render.update(capture.snapshot(), false, lastArrival + 0.5);
    expect(render.snapshot().signal == atemfx::SourceSignal::Stale,
           "500 ms without a fresh picture marks the camera stale");
    expect(render.snapshot().captureFps == 0.0,
           "a stale camera cannot keep showing its last measured live rate");

    render.update(capture.snapshot(), false, 1000.0);
    expect(near(render.snapshot().ageSeconds, 1000.0 - lastArrival),
           "a stalled render loop reports full wall-clock age on its next tick");
    expect(render.snapshot().receivedFrames == 2 && render.snapshot().consumedFrames == 2,
           "rendering a stale image does not fabricate camera frames");

    capture.recordFrame(1000.1, false);
    render.update(capture.snapshot(), true, 1000.101);
    expect(render.snapshot().signal == atemfx::SourceSignal::Live,
           "the next fresh frame recovers the signal without resetting totals");
    expect(render.snapshot().captureFps == 0.0,
           "recovery starts a new capture-rate window after silence");
    capture.recordFrame(1000.1 + 1.0 / 60.0, false);
    render.update(capture.snapshot(), true, 1000.1 + 1.0 / 60.0);
    expect(near(render.snapshot().captureFps, 60.0),
           "the recovered camera measures its current cadence");
    expect(render.snapshot().receivedFrames == 4 && render.snapshot().consumedFrames == 4 &&
               render.snapshot().repeatedFrames == 3,
           "recovery preserves distinct-frame and repeated-render counters");
}

void checkNewestWinsCountsOverwrites()
{
    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;

    for (int frame = 0; frame < 61; ++frame)
    {
        capture.recordFrame(10.0 + static_cast<double>(frame) / 30.0, frame != 0);
    }
    render.update(capture.snapshot(), true, 12.01);

    const auto health = render.snapshot();
    expect(health.receivedFrames == 61 && health.consumedFrames == 1 &&
               health.overwrittenFrames == 60 && health.repeatedFrames == 0,
           "newest-wins counts every discarded pending frame separately from consumption");
    expect(near(health.ageSeconds, 0.01),
           "consumption uses the newest frame's arrival time");
    expect(near(health.captureFps, 30.0),
           "capture measurement continues accurately while the renderer is stalled");

    render.update(capture.snapshot(), false, 12.02);
    expect(render.snapshot().repeatedFrames == 1 &&
               render.snapshot().overwrittenFrames == 60,
           "reusing the consumed frame does not count as another capture overwrite");

    capture.recordFrame(12.0 + 1.0 / 30.0, false);
    render.update(capture.snapshot(), true, 12.04);
    expect(render.snapshot().receivedFrames == 62 && render.snapshot().consumedFrames == 2 &&
               render.snapshot().overwrittenFrames == 60,
           "capture into an empty slot does not count as an overwrite");
}

void checkAlreadyOldFirstFrameIsStale()
{
    atemfx::SourceCaptureMonitor capture;
    atemfx::SourceHealthMonitor render;
    capture.recordFrame(30.0, false);
    render.update(capture.snapshot(), true, 31.0);

    expect(render.snapshot().signal == atemfx::SourceSignal::Stale &&
               near(render.snapshot().ageSeconds, 1.0),
           "consuming a long-pending frame does not incorrectly refresh its age");
}

} // namespace

int main()
{
    checkWaitingIsNotLiveOrRepeated();
    checkFirstFrameHasAnAgeButNoRateYet();
    checkCapture30Render60();
    checkSilenceAndRecovery();
    checkNewestWinsCountsOverwrites();
    checkAlreadyOldFirstFrameIsStale();

    std::printf("source_health: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
