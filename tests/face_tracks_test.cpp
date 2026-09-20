#include "tracking/face_tracks.h"

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

atemfx::FaceDetection face(float x, float y, float size = 0.08f, float confidence = 0.9f)
{
    atemfx::FaceDetection d;
    d.centerX    = x;
    d.centerY    = y;
    d.width      = size;
    d.height     = size * 16.0f / 9.0f;
    d.confidence = confidence;
    return d;
}

atemfx::FacesSnapshot snapshotOf(const atemfx::FaceTrackManager& manager)
{
    atemfx::FacesSnapshot snapshot;
    manager.snapshot(snapshot);
    return snapshot;
}

void checkIou()
{
    expect(near(atemfx::faceIou(0.5f, 0.5f, 0.2f, 0.2f, 0.5f, 0.5f, 0.2f, 0.2f), 1.0f, 1e-6f),
           "identical boxes have IoU 1");
    expect(near(atemfx::faceIou(0.2f, 0.2f, 0.1f, 0.1f, 0.8f, 0.8f, 0.1f, 0.1f), 0.0f, 1e-6f),
           "disjoint boxes have IoU 0");
    expect(near(atemfx::faceIou(0.5f, 0.5f, 0.2f, 0.2f, 0.6f, 0.5f, 0.2f, 0.2f), 1.0f / 3.0f, 1e-5f),
           "half-overlapping boxes have IoU 1/3");
}

void checkNewFaceNeedsTwoHits()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    const atemfx::FaceDetection one[] = {face(0.3f, 0.4f)};
    manager.update(one, 1, 0.0, settings);
    expect(snapshotOf(manager).count == 0, "a single detection is not published");

    manager.update(one, 1, 0.066, settings);
    const atemfx::FacesSnapshot s = snapshotOf(manager);
    expect(s.count == 1, "second detection confirms the track");
    expect(s.faces[0].trackId != 0, "published id is never 0");
}

void checkIdentityPersistsWhileMoving()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    uint32_t id = 0;
    bool     stable = true;
    for (int i = 0; i < 30; ++i)
    {
        // Walks right at 0.3 frames per second: well under a face width per cycle.
        const atemfx::FaceDetection d[] = {face(0.2f + 0.02f * static_cast<float>(i), 0.5f)};
        manager.update(d, 1, i / 15.0, settings);
        const atemfx::FacesSnapshot s = snapshotOf(manager);
        if (s.count == 1)
        {
            if (id == 0)
            {
                id = s.faces[0].trackId;
            }
            stable = stable && s.faces[0].trackId == id;
        }
    }
    expect(id != 0 && stable, "a walking face keeps one id");
    expect(manager.activeTracks() == 1, "no ghost tracks behind a walking face");

    const atemfx::FacesSnapshot s = snapshotOf(manager);
    expect(near(s.faces[0].velocityX, 0.3f, 0.05f), "velocity follows the walk");
}

void checkFastJumpMatchesByDistance()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    const atemfx::FaceDetection a[] = {face(0.30f, 0.5f)};
    manager.update(a, 1, 0.0, settings);
    manager.update(a, 1, 0.066, settings);
    const uint32_t id = snapshotOf(manager).faces[0].trackId;

    // Moves most of a face width in one cycle: no overlap left, same person.
    const atemfx::FaceDetection b[] = {face(0.35f, 0.5f)};
    manager.update(b, 1, 0.133, settings);
    const atemfx::FacesSnapshot s = snapshotOf(manager);
    expect(s.count == 1 && s.faces[0].trackId == id, "a quick head turn keeps the id");
}

void checkTwoFacesKeepTheirOwnIds()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    const atemfx::FaceDetection first[] = {face(0.25f, 0.5f), face(0.75f, 0.5f)};
    manager.update(first, 2, 0.0, settings);
    manager.update(first, 2, 0.066, settings);
    atemfx::FacesSnapshot s = snapshotOf(manager);
    expect(s.count == 2, "two faces, two tracks");

    const uint32_t left  = s.faces[0].centerX < 0.5f ? s.faces[0].trackId : s.faces[1].trackId;
    const uint32_t right = s.faces[0].centerX < 0.5f ? s.faces[1].trackId : s.faces[0].trackId;
    expect(left != right, "distinct ids");

    // Detector reports them in the opposite order.
    const atemfx::FaceDetection swapped[] = {face(0.76f, 0.5f), face(0.26f, 0.5f)};
    manager.update(swapped, 2, 0.133, settings);
    s = snapshotOf(manager);
    bool ok = s.count == 2;
    for (uint32_t i = 0; i < s.count; ++i)
    {
        const uint32_t expected = s.faces[i].centerX < 0.5f ? left : right;
        ok = ok && s.faces[i].trackId == expected;
    }
    expect(ok, "detection order does not swap identities");
}

void checkLostFaceExpiresAndReturnsWithNewId()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    const atemfx::FaceDetection d[] = {face(0.5f, 0.5f)};
    manager.update(d, 1, 0.0, settings);
    manager.update(d, 1, 0.066, settings);
    const uint32_t id = snapshotOf(manager).faces[0].trackId;

    manager.update(nullptr, 0, 0.3, settings);
    expect(snapshotOf(manager).count == 1, "a short dropout keeps the track");

    manager.update(nullptr, 0, 0.7, settings);
    expect(snapshotOf(manager).count == 0, "a long dropout expires the track");

    manager.update(d, 1, 0.8, settings);
    manager.update(d, 1, 0.866, settings);
    const atemfx::FacesSnapshot s = snapshotOf(manager);
    expect(s.count == 1 && s.faces[0].trackId != id, "a returning face gets a new id");
}

void checkCapacityAndBadInput()
{
    atemfx::FaceTrackManager  manager;
    atemfx::FaceTrackSettings settings;

    atemfx::FaceDetection crowd[atemfx::kMaxFaces + 4];
    for (std::size_t i = 0; i < atemfx::kMaxFaces + 4; ++i)
    {
        crowd[i] = face(0.03f + 0.058f * static_cast<float>(i), 0.5f, 0.02f);
    }
    manager.update(crowd, atemfx::kMaxFaces + 4, 0.0, settings);
    manager.update(crowd, atemfx::kMaxFaces + 4, 0.066, settings);
    expect(snapshotOf(manager).count == atemfx::kMaxFaces, "never more than kMaxFaces published");

    atemfx::FaceTrackManager clean;
    atemfx::FaceDetection    bad[] = {face(std::numeric_limits<float>::quiet_NaN(), 0.5f),
                                      face(0.5f, 0.5f, 0.0f)};
    clean.update(bad, 2, 0.0, settings);
    clean.update(bad, 2, 0.066, settings);
    expect(clean.activeTracks() == 0, "non-finite and empty boxes are ignored");

    manager.reset();
    expect(manager.activeTracks() == 0 && snapshotOf(manager).count == 0, "reset drops every track");
}

} // namespace

int main()
{
    checkIou();
    checkNewFaceNeedsTwoHits();
    checkIdentityPersistsWhileMoving();
    checkFastJumpMatchesByDistance();
    checkTwoFacesKeepTheirOwnIds();
    checkLostFaceExpiresAndReturnsWithNewId();
    checkCapacityAndBadInput();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d / %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("%d checks passed\n", checks);
    return 0;
}
