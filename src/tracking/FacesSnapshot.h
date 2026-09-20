#pragma once

#include <cstddef>
#include <cstdint>

namespace atemfx {

// Faces are a crowd, not a subject: many at once, each keeping its own id
// while it stays in shot. Same rules as TrackingSnapshot — a plain value,
// normalized coordinates with the origin top left, copied into the frame once
// per frame — but a separate struct, because Auto Frame follows one person and
// must never be steered by the audience.
inline constexpr std::size_t kMaxFaces = 16;

// One raw detector hit, before association.
struct FaceDetection
{
    float centerX    = 0.5f;
    float centerY    = 0.5f;
    float width      = 0.0f;
    float height     = 0.0f;
    float confidence = 0.0f;
};

// A face with an identity. `trackId` is never 0 and is not reused while the
// process runs, so an effect can key per-face state on it.
struct FaceObservation
{
    uint32_t trackId    = 0;
    float    centerX    = 0.5f;
    float    centerY    = 0.5f;
    float    width      = 0.0f;
    float    height     = 0.0f;
    float    confidence = 0.0f;
    float    velocityX  = 0.0f;  // fraction of the frame per second
    float    velocityY  = 0.0f;
};

struct FacesSnapshot
{
    // A face sensor is running and has been asked for faces. False is normal:
    // no sensor on this platform, no input with CPU frames, or no effect that
    // wants faces.
    bool available = false;

    uint32_t        count = 0;
    FaceObservation faces[kMaxFaces];
};

} // namespace atemfx
