#include "tracking/face_tracks.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace atemfx {

namespace {

bool finite(const FaceDetection& d)
{
    return std::isfinite(d.centerX) && std::isfinite(d.centerY) && std::isfinite(d.width) &&
           std::isfinite(d.height) && d.width > 0.0f && d.height > 0.0f;
}

} // namespace

float faceIou(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    const float left   = std::max(ax - aw * 0.5f, bx - bw * 0.5f);
    const float right  = std::min(ax + aw * 0.5f, bx + bw * 0.5f);
    const float top    = std::max(ay - ah * 0.5f, by - bh * 0.5f);
    const float bottom = std::min(ay + ah * 0.5f, by + bh * 0.5f);

    const float overlap = std::max(0.0f, right - left) * std::max(0.0f, bottom - top);
    const float united  = aw * ah + bw * bh - overlap;
    return united > 0.0f ? overlap / united : 0.0f;
}

uint32_t FaceTrackManager::nextId()
{
    // 0 means "no face" to consumers; skip it on the (theoretical) wrap.
    ++lastId_;
    if (lastId_ == 0)
    {
        lastId_ = 1;
    }
    return lastId_;
}

void FaceTrackManager::reset()
{
    for (Track& track : tracks_)
    {
        track.active = false;
    }
}

std::size_t FaceTrackManager::activeTracks() const
{
    return static_cast<std::size_t>(
        std::count_if(std::begin(tracks_), std::end(tracks_), [](const Track& t) { return t.active; }));
}

void FaceTrackManager::update(const FaceDetection*     detections,
                              std::size_t              count,
                              double                   timestamp,
                              const FaceTrackSettings& settings)
{
    minHits_ = std::max(1, settings.minHits);
    count    = detections ? std::min(count, kMaxFaces) : 0;

    // Candidate pairs, scored. Fixed storage: kMaxTracks x kMaxFaces.
    struct Pair
    {
        float       score;
        std::size_t track;
        std::size_t detection;
    };
    Pair        pairs[kMaxTracks * kMaxFaces];
    std::size_t pairCount = 0;

    for (std::size_t t = 0; t < kMaxTracks; ++t)
    {
        const Track& track = tracks_[t];
        if (!track.active)
        {
            continue;
        }

        // Predict where the face is now, so a moving head still overlaps.
        const float dt = static_cast<float>(std::max(0.0, timestamp - track.lastSeen));
        const float px = track.face.centerX + track.face.velocityX * dt;
        const float py = track.face.centerY + track.face.velocityY * dt;

        for (std::size_t d = 0; d < count; ++d)
        {
            const FaceDetection& detection = detections[d];
            if (!finite(detection))
            {
                continue;
            }

            const float iou = faceIou(px, py, track.face.width, track.face.height, detection.centerX,
                                      detection.centerY, detection.width, detection.height);

            const float size = std::max(0.5f * (track.face.width + detection.width), 1.0e-4f);
            const float jump = std::hypot(detection.centerX - px, detection.centerY - py) / size;

            float score = -1.0f;
            if (iou >= settings.minIou)
            {
                score = 1.0f + iou;  // any real overlap beats any distance match
            }
            else if (jump <= settings.maxCenterJump)
            {
                score = 1.0f - jump / std::max(settings.maxCenterJump, 1.0e-4f);
            }

            if (score >= 0.0f)
            {
                pairs[pairCount++] = {score, t, d};
            }
        }
    }

    std::sort(pairs, pairs + pairCount, [](const Pair& a, const Pair& b) { return a.score > b.score; });

    bool trackTaken[kMaxTracks]   = {};
    bool detectionTaken[kMaxFaces] = {};

    for (std::size_t i = 0; i < pairCount; ++i)
    {
        const Pair& pair = pairs[i];
        if (trackTaken[pair.track] || detectionTaken[pair.detection])
        {
            continue;
        }
        trackTaken[pair.track]         = true;
        detectionTaken[pair.detection] = true;

        Track&               track     = tracks_[pair.track];
        const FaceDetection& detection = detections[pair.detection];

        const float dt = static_cast<float>(timestamp - track.lastSeen);
        if (dt > 1.0e-4f)
        {
            const float response = std::clamp(settings.velocityResponse, 0.0f, 1.0f);
            const float vx       = (detection.centerX - track.face.centerX) / dt;
            const float vy       = (detection.centerY - track.face.centerY) / dt;
            track.face.velocityX += (vx - track.face.velocityX) * response;
            track.face.velocityY += (vy - track.face.velocityY) * response;
        }

        track.face.centerX    = detection.centerX;
        track.face.centerY    = detection.centerY;
        track.face.width      = detection.width;
        track.face.height     = detection.height;
        track.face.confidence = detection.confidence;
        track.lastSeen        = timestamp;
        ++track.hits;
    }

    // Expire before creating, so a full table frees its dead slots first.
    for (Track& track : tracks_)
    {
        if (track.active && timestamp - track.lastSeen > settings.maxMissedSeconds)
        {
            track.active = false;
        }
    }

    for (std::size_t d = 0; d < count; ++d)
    {
        if (detectionTaken[d] || !finite(detections[d]))
        {
            continue;
        }

        Track* slot = nullptr;
        for (Track& track : tracks_)
        {
            if (!track.active)
            {
                slot = &track;
                break;
            }
        }
        if (!slot)
        {
            break;
        }

        const FaceDetection& detection = detections[d];

        *slot                 = Track{};
        slot->active          = true;
        slot->id              = nextId();
        slot->hits            = 1;
        slot->lastSeen        = timestamp;
        slot->created         = timestamp;
        slot->face.trackId    = slot->id;
        slot->face.centerX    = detection.centerX;
        slot->face.centerY    = detection.centerY;
        slot->face.width      = detection.width;
        slot->face.height     = detection.height;
        slot->face.confidence = detection.confidence;
    }
}

void FaceTrackManager::snapshot(FacesSnapshot& out) const
{
    // Oldest first, so when an effect has to drop faces it drops the newest
    // arrivals rather than someone who has been on the wall for a minute.
    const Track* order[kMaxTracks];
    std::size_t  n = 0;
    for (const Track& track : tracks_)
    {
        if (track.active && track.hits >= minHits_)
        {
            order[n++] = &track;
        }
    }

    std::sort(order, order + n, [](const Track* a, const Track* b) {
        return a->created < b->created || (a->created == b->created && a->id < b->id);
    });

    out.count = static_cast<uint32_t>(std::min(n, kMaxFaces));
    for (uint32_t i = 0; i < out.count; ++i)
    {
        out.faces[i]         = order[i]->face;
        out.faces[i].trackId = order[i]->id;
    }
}

} // namespace atemfx
