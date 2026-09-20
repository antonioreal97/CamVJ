#pragma once

#include <cstddef>
#include <cstdint>

#include "tracking/FacesSnapshot.h"

namespace atemfx {

struct FaceTrackSettings
{
    // Overlap that makes a detection the same face as a track. Below it, a
    // centre close enough (in face sizes) still matches: at 15 detections a
    // second a turning head can move more than its own width.
    float minIou          = 0.20f;
    float maxCenterJump   = 0.75f;

    // Detections a new track needs before it is published, so a one-cycle
    // false positive never becomes a tile on the wall.
    int minHits = 2;

    // How long a track survives without a detection. Short on purpose: the
    // box is not extrapolated, so a stale track points at where the face was.
    float maxMissedSeconds = 0.5f;

    // 0 keeps the old velocity, 1 takes the newest measurement.
    float velocityResponse = 0.5f;
};

// Gives detector hits persistent identities (greedy IoU with a centre-distance
// fallback, velocity-predicted boxes). Portable and allocation-free: a fixed
// table, so it can run on the sensor thread forever without touching the heap.
// Tested in tests/face_tracks_test.cpp.
class FaceTrackManager
{
public:
    static constexpr std::size_t kMaxTracks = kMaxFaces * 2;

    // `timestamp` in seconds, monotonic. Detections beyond kMaxFaces are
    // ignored; so are new faces when the table is full.
    void update(const FaceDetection*     detections,
                std::size_t              count,
                double                   timestamp,
                const FaceTrackSettings& settings);

    void reset();

    // Confirmed, unexpired tracks, oldest first.
    void snapshot(FacesSnapshot& out) const;

    std::size_t activeTracks() const;

private:
    struct Track
    {
        bool     active   = false;
        uint32_t id       = 0;
        int      hits     = 0;
        double   lastSeen = 0.0;
        double   created  = 0.0;
        FaceObservation face;
    };

    uint32_t nextId();

    Track    tracks_[kMaxTracks];
    uint32_t lastId_ = 0;
    int      minHits_ = 2;
};

// Intersection over union of two centre/extent boxes. Exposed for the tests.
float faceIou(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh);

} // namespace atemfx
