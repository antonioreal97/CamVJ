#pragma once

namespace atemfx {

// Where the subject is, as the pixel pipeline sees it: normalized frame
// coordinates, origin at the top left, one snapshot per frame.
//
// This is a control-plane value crossing into the pixel pipeline by copy, the
// same way parameters do (docs/ARCHITECTURE.md section 1). It is deliberately
// a plain struct with no dependencies: `effects/` reads it, `tracking/` writes
// it, and neither has to include the other's machinery.
struct TrackingSnapshot
{
    // A tracker is running. False means nobody is looking, which is a normal
    // state — no tracker on this platform, or no input that hands over frames.
    bool available = false;

    // A subject was seen recently enough to steer the framing. False during a
    // dropout; the framing controller decides how long to hold before it gives
    // up, so the pixel side never has to know about timeouts.
    bool valid = false;

    float centerX = 0.5f;  // subject centre
    float centerY = 0.5f;
    float width   = 0.0f;  // subject extent, fraction of the frame
    float height  = 0.0f;
    float confidence = 0.0f;
};

} // namespace atemfx
