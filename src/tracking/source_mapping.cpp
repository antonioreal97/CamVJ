#include "tracking/source_mapping.h"

#include <algorithm>
#include <cmath>

namespace atemfx {

void clampNormalizedRect(float& centerX, float& centerY, float& width, float& height)
{
    width  = std::clamp(width, kMinLockExtent, 1.0f);
    height = std::clamp(height, kMinLockExtent, 1.0f);

    const float halfWidth  = width * 0.5f;
    const float halfHeight = height * 0.5f;

    centerX = std::clamp(centerX, halfWidth, 1.0f - halfWidth);
    centerY = std::clamp(centerY, halfHeight, 1.0f - halfHeight);
}

void defaultLockRect(float clickX, float clickY, float& centerX, float& centerY, float& width,
                     float& height)
{
    centerX = clickX;
    centerY = clickY;
    width   = kDefaultLockWidth;
    height  = kDefaultLockHeight;
    clampNormalizedRect(centerX, centerY, width, height);
}

bool pickIsDrag(float startX, float startY, float endX, float endY)
{
    const float dx = endX - startX;
    const float dy = endY - startY;
    return (dx * dx + dy * dy) >= (kPickDragThresholdUv * kPickDragThresholdUv);
}

int hitTestCandidate(const TrackingCandidate* candidates, std::size_t count, float x, float y)
{
    if (!candidates || count == 0)
    {
        return -1;
    }

    int   best     = -1;
    float bestDist = 1.0e9f;

    for (std::size_t i = 0; i < count; ++i)
    {
        const TrackingCandidate& candidate = candidates[i];
        const float              halfW     = candidate.width * 0.5f;
        const float              halfH     = candidate.height * 0.5f;
        if (x < candidate.centerX - halfW || x > candidate.centerX + halfW ||
            y < candidate.centerY - halfH || y > candidate.centerY + halfH)
        {
            continue;
        }

        const float dx   = candidate.centerX - x;
        const float dy   = candidate.centerY - y;
        const float dist = dx * dx + dy * dy;
        if (dist < bestDist)
        {
            bestDist = dist;
            best     = static_cast<int>(i);
        }
    }

    return best;
}

void mapSourceToCanvas(const SourceMapping& mapping, float& x, float& y)
{
    const float scaleX = (std::isfinite(mapping.scaleX) && mapping.scaleX > 1.0e-4f)
                             ? mapping.scaleX
                             : 1.0f;
    const float scaleY = (std::isfinite(mapping.scaleY) && mapping.scaleY > 1.0e-4f)
                             ? mapping.scaleY
                             : 1.0f;

    x = (x - 0.5f) / scaleX + 0.5f;
    y = (y - 0.5f) / scaleY + 0.5f;
    if (mapping.mirrored)
    {
        x = 1.0f - x;
    }
}

void mapSourceToCanvas(const SourceMapping& mapping, float& x, float& y, float& width, float& height)
{
    const float scaleX = (std::isfinite(mapping.scaleX) && mapping.scaleX > 1.0e-4f)
                             ? mapping.scaleX
                             : 1.0f;
    const float scaleY = (std::isfinite(mapping.scaleY) && mapping.scaleY > 1.0e-4f)
                             ? mapping.scaleY
                             : 1.0f;

    mapSourceToCanvas(mapping, x, y);
    width /= scaleX;
    height /= scaleY;
}

void mapCanvasToSource(const SourceMapping& mapping, float& x, float& y)
{
    const float scaleX = (std::isfinite(mapping.scaleX) && mapping.scaleX > 1.0e-4f)
                             ? mapping.scaleX
                             : 1.0f;
    const float scaleY = (std::isfinite(mapping.scaleY) && mapping.scaleY > 1.0e-4f)
                             ? mapping.scaleY
                             : 1.0f;

    if (mapping.mirrored)
    {
        x = 1.0f - x;
    }
    x = (x - 0.5f) * scaleX + 0.5f;
    y = (y - 0.5f) * scaleY + 0.5f;
}

void mapCanvasToSource(const SourceMapping& mapping, float& x, float& y, float& width, float& height)
{
    const float scaleX = (std::isfinite(mapping.scaleX) && mapping.scaleX > 1.0e-4f)
                             ? mapping.scaleX
                             : 1.0f;
    const float scaleY = (std::isfinite(mapping.scaleY) && mapping.scaleY > 1.0e-4f)
                             ? mapping.scaleY
                             : 1.0f;

    mapCanvasToSource(mapping, x, y);
    width *= scaleX;
    height *= scaleY;
}

void fillLockRequest(TrackingLockRequest& request, float centerX, float centerY, float width,
                     float height)
{
    clampNormalizedRect(centerX, centerY, width, height);
    request.pending = true;
    request.centerX = centerX;
    request.centerY = centerY;
    request.width   = width;
    request.height  = height;
}

} // namespace atemfx
