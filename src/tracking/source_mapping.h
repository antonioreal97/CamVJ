#pragma once

#include "tracking/TrackingSnapshot.h"

#include <cstddef>

namespace atemfx {

// Where the source's own image landed on the canvas.
//
// The canvas is always 1920x1080; a source that is not that shape is fitted
// into it, and a self-view camera may be mirrored. Anything working in the
// source's coordinates — tracking is the only such consumer today — has to
// know that transform to say where the subject is on the canvas. Identity for
// a source that fills the canvas exactly, which is every SDI input.
//
// Canvas to source, matching source_blit: source = (canvas - 0.5) * scale + 0.5.
struct SourceMapping
{
    float scaleX   = 1.0f;
    float scaleY   = 1.0f;
    bool  mirrored = false;
};

// Default box around a click on empty SOURCE, in normalized UV. Tall enough
// to seed Vision's object tracker with a head-and-shoulders-ish region.
constexpr float kDefaultLockWidth    = 0.16f;
constexpr float kDefaultLockHeight   = 0.24f;
constexpr float kMinLockExtent       = 0.04f;
constexpr float kPickDragThresholdUv = 0.012f;

// Keeps a normalized centre/extent rectangle inside the unit square. A click
// near the edge shifts the centre rather than letting the box hang off-frame.
void clampNormalizedRect(float& centerX, float& centerY, float& width, float& height);

// 0.16 x 0.24 box centred on the click, then clamped.
void defaultLockRect(float clickX, float clickY, float& centerX, float& centerY, float& width,
                     float& height);

bool pickIsDrag(float startX, float startY, float endX, float endY);

// Which candidate contains (x, y). Overlaps prefer the closest centre. -1 if
// none.
int hitTestCandidate(const TrackingCandidate* candidates, std::size_t count, float x, float y);

void mapSourceToCanvas(const SourceMapping& mapping, float& x, float& y);
void mapSourceToCanvas(const SourceMapping& mapping, float& x, float& y, float& width, float& height);
void mapCanvasToSource(const SourceMapping& mapping, float& x, float& y);
void mapCanvasToSource(const SourceMapping& mapping, float& x, float& y, float& width, float& height);

void fillLockRequest(TrackingLockRequest& request, float centerX, float centerY, float width,
                     float height);

} // namespace atemfx
