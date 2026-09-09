#include "tracking/source_mapping.h"

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

bool near(float actual, float expected, float tolerance)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void checkIdentityMappingLeavesPointsAlone()
{
    atemfx::SourceMapping mapping;
    float                 x = 0.25f;
    float                 y = 0.75f;
    float                 w = 0.2f;
    float                 h = 0.4f;
    atemfx::mapSourceToCanvas(mapping, x, y, w, h);
    expect(near(x, 0.25f, 1.0e-6f) && near(y, 0.75f, 1.0e-6f), "identity source→canvas centre");
    expect(near(w, 0.2f, 1.0e-6f) && near(h, 0.4f, 1.0e-6f), "identity source→canvas size");
}

void checkRoundTripWithFitAndMirror()
{
    atemfx::SourceMapping mapping;
    mapping.scaleX   = 1.25f;
    mapping.scaleY   = 0.8f;
    mapping.mirrored = true;

    const float ox = 0.31f;
    const float oy = 0.44f;
    const float ow = 0.18f;
    const float oh = 0.26f;

    float x = ox;
    float y = oy;
    float w = ow;
    float h = oh;
    atemfx::mapSourceToCanvas(mapping, x, y, w, h);
    atemfx::mapCanvasToSource(mapping, x, y, w, h);

    expect(near(x, ox, 1.0e-5f) && near(y, oy, 1.0e-5f), "round-trip centre");
    expect(near(w, ow, 1.0e-5f) && near(h, oh, 1.0e-5f), "round-trip size");
}

void checkMirrorFlipsXOnly()
{
    atemfx::SourceMapping mapping;
    mapping.mirrored = true;
    float x          = 0.25f;
    float y          = 0.40f;
    atemfx::mapSourceToCanvas(mapping, x, y);
    expect(near(x, 0.75f, 1.0e-6f), "mirror flips X");
    expect(near(y, 0.40f, 1.0e-6f), "mirror leaves Y");
}

void checkAppMappingConvention()
{
    // Matches App::updateEffectContext: canvas = (source - 0.5) / scale + 0.5.
    atemfx::SourceMapping mapping;
    mapping.scaleX = 2.0f;
    float         x = 0.5f;
    float         y = 0.5f;
    atemfx::mapSourceToCanvas(mapping, x, y);
    expect(near(x, 0.5f, 1.0e-6f) && near(y, 0.5f, 1.0e-6f), "centre stays centre");

    x = 0.0f;
    y = 0.0f;
    atemfx::mapSourceToCanvas(mapping, x, y);
    expect(near(x, 0.25f, 1.0e-6f), "letterbox source left maps inside canvas");
}

void checkHitTestPrefersCloserCentre()
{
    atemfx::TrackingCandidate candidates[2];
    candidates[0].centerX = 0.40f;
    candidates[0].centerY = 0.50f;
    candidates[0].width   = 0.40f;
    candidates[0].height  = 0.40f;
    candidates[1].centerX = 0.60f;
    candidates[1].centerY = 0.50f;
    candidates[1].width   = 0.40f;
    candidates[1].height  = 0.40f;

    expect(atemfx::hitTestCandidate(candidates, 2, 0.42f, 0.50f) == 0,
           "overlap prefers closer centre (left)");
    expect(atemfx::hitTestCandidate(candidates, 2, 0.58f, 0.50f) == 1,
           "overlap prefers closer centre (right)");
    expect(atemfx::hitTestCandidate(candidates, 2, 0.05f, 0.05f) == -1, "miss is -1");
    expect(atemfx::hitTestCandidate(nullptr, 0, 0.5f, 0.5f) == -1, "empty is -1");
}

void checkClickVsDrag()
{
    expect(!atemfx::pickIsDrag(0.50f, 0.50f, 0.505f, 0.502f), "tiny move is a click");
    expect(atemfx::pickIsDrag(0.20f, 0.20f, 0.40f, 0.45f), "box drag is a drag");
}

void checkDefaultLockClampsToFrame()
{
    float cx = 0.0f;
    float cy = 0.0f;
    float w  = 0.0f;
    float h  = 0.0f;
    atemfx::defaultLockRect(0.0f, 0.0f, cx, cy, w, h);
    expect(near(w, atemfx::kDefaultLockWidth, 1.0e-6f), "default width");
    expect(near(h, atemfx::kDefaultLockHeight, 1.0e-6f), "default height");
    expect(cx - w * 0.5f >= -1.0e-5f && cy - h * 0.5f >= -1.0e-5f, "corner click stays on frame");
    expect(cx + w * 0.5f <= 1.0f + 1.0e-5f && cy + h * 0.5f <= 1.0f + 1.0e-5f,
           "corner click stays on frame (max)");
}

void checkFillLockRequest()
{
    atemfx::TrackingLockRequest request;
    atemfx::fillLockRequest(request, 0.5f, 0.5f, 0.2f, 0.3f);
    expect(request.pending, "fill marks pending");
    expect(near(request.centerX, 0.5f, 1.0e-6f) && near(request.width, 0.2f, 1.0e-6f),
           "fill stores the rect");
}

} // namespace

int main()
{
    checkIdentityMappingLeavesPointsAlone();
    checkRoundTripWithFitAndMirror();
    checkMirrorFlipsXOnly();
    checkAppMappingConvention();
    checkHitTestPrefersCloserCentre();
    checkClickVsDrag();
    checkDefaultLockClampsToFrame();
    checkFillLockRequest();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d / %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("%d checks passed\n", checks);
    return 0;
}
