#include "effects/face_tiles.h"

#include <algorithm>
#include <cmath>

namespace atemfx {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float smooth(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool finiteFace(const FaceObservation& face)
{
    return std::isfinite(face.centerX) && std::isfinite(face.centerY) && std::isfinite(face.width) &&
           std::isfinite(face.height) && face.width > 0.0f && face.height > 0.0f;
}

} // namespace

float faceTileRandom(uint32_t seed, uint32_t trackId, uint32_t copy, uint32_t channel)
{
    // splitmix64 over the packed keys: cheap, stateless and well spread, so
    // the same face and copy land on the same spot every frame.
    uint64_t x = (static_cast<uint64_t>(seed) << 32) ^ (static_cast<uint64_t>(trackId) << 16) ^
                 (static_cast<uint64_t>(copy) << 4) ^ channel;
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(x >> 40) / static_cast<float>(1u << 24);
}

void FaceTileManager::reset()
{
    for (Tile& tile : tiles_)
    {
        tile.active = false;
    }
}

std::size_t FaceTileManager::activeTiles() const
{
    return static_cast<std::size_t>(
        std::count_if(std::begin(tiles_), std::end(tiles_), [](const Tile& t) { return t.active; }));
}

void FaceTileManager::update(const FacesSnapshot& faces, const FaceTileSettings& settings, float deltaTime)
{
    const int copies   = std::clamp(settings.copiesPerFace, 0, static_cast<int>(kMaxTiles));
    const int maxTiles = std::clamp(settings.maxTiles, 0, static_cast<int>(kMaxTiles));
    const int maxFaces = std::clamp(settings.maxFaces, 0, static_cast<int>(kMaxFaces));

    for (Tile& tile : tiles_)
    {
        tile.wanted = false;
    }

    // Faces arrive oldest first, so the budget goes to people who have been
    // in shot longest and a newcomer never evicts them.
    int wantedTiles = 0;
    int usedFaces   = 0;
    const uint32_t count = std::min<uint32_t>(faces.count, static_cast<uint32_t>(kMaxFaces));
    for (uint32_t i = 0; i < count && usedFaces < maxFaces; ++i)
    {
        const FaceObservation& face = faces.faces[i];
        if (face.trackId == 0 || face.confidence < settings.minConfidence || !finiteFace(face))
        {
            continue;
        }
        ++usedFaces;

        for (int c = 0; c < copies && wantedTiles < maxTiles; ++c)
        {
            const uint32_t copy = static_cast<uint32_t>(c);

            Tile* tile = nullptr;
            Tile* free = nullptr;
            for (Tile& candidate : tiles_)
            {
                if (candidate.active && candidate.trackId == face.trackId && candidate.copy == copy)
                {
                    tile = &candidate;
                    break;
                }
                if (!candidate.active && !free)
                {
                    free = &candidate;
                }
            }

            if (!tile)
            {
                if (!free)
                {
                    // Every slot is busy, fading ones included. The tile
                    // arrives once a fade finishes.
                    continue;
                }
                tile          = free;
                *tile         = Tile{};
                tile->active  = true;
                tile->trackId = face.trackId;
                tile->copy    = copy;
                tile->born    = ++births_;
            }

            tile->wanted = true;
            tile->face   = face;
            ++wantedTiles;
        }
    }

    const float fade = settings.fadeSeconds > 0.0f ? deltaTime / settings.fadeSeconds : 1.0f;
    for (Tile& tile : tiles_)
    {
        if (!tile.active)
        {
            continue;
        }

        if (tile.wanted)
        {
            tile.fade = std::min(1.0f, tile.fade + fade);
        }
        else
        {
            tile.fade -= fade;
            if (tile.fade <= 0.0f)
            {
                tile.active = false;
            }
        }
    }
}

std::size_t FaceTileManager::build(SpriteInstance*         out,
                                   std::size_t             capacity,
                                   const FaceTileSettings& settings) const
{
    if (!out || capacity == 0)
    {
        return 0;
    }

    const Tile* order[kMaxTiles];
    std::size_t n = 0;
    for (const Tile& tile : tiles_)
    {
        if (tile.active && tile.fade > 0.0f)
        {
            order[n++] = &tile;
        }
    }
    std::sort(order, order + n, [](const Tile* a, const Tile* b) { return a->born < b->born; });
    n = std::min(n, capacity);

    const float minScale  = std::max(0.01f, std::min(settings.minScale, settings.maxScale));
    const float maxScale  = std::max(minScale, std::max(settings.minScale, settings.maxScale));
    const float padding   = std::max(0.0f, settings.padding);
    const float spread    = std::clamp(settings.spread, 0.0f, 1.0f);
    const float maxRadian = std::clamp(settings.maxRotationDegrees, 0.0f, 180.0f) * kPi / 180.0f;
    const float opacity   = std::clamp(settings.opacity, 0.0f, 1.0f);

    for (std::size_t i = 0; i < n; ++i)
    {
        const Tile& tile = *order[i];

        // Positions come from the tile's own random numbers and the seed in
        // force now, so changing the seed reshuffles tiles already on air.
        const float rx = faceTileRandom(settings.seed, tile.trackId, tile.copy, 0);
        const float ry = faceTileRandom(settings.seed, tile.trackId, tile.copy, 1);
        const float rs = faceTileRandom(settings.seed, tile.trackId, tile.copy, 2);
        const float rr = faceTileRandom(settings.seed, tile.trackId, tile.copy, 3);

        const float halfU = 0.5f * tile.face.width * (1.0f + padding);
        const float halfV = 0.5f * tile.face.height * (1.0f + padding);

        const float scale = minScale + (maxScale - minScale) * rs;
        const float dstHalfX = halfU * scale;
        const float dstHalfY = halfV * scale;

        // Somewhere inside the area the whole tile fits, when it can fit at all.
        const float areaX   = std::max(0.0f, settings.areaMaxX - settings.areaMinX);
        const float areaY   = std::max(0.0f, settings.areaMaxY - settings.areaMinY);
        const float marginX = std::min(dstHalfX, 0.5f * areaX);
        const float marginY = std::min(dstHalfY, 0.5f * areaY);
        const float targetX = settings.areaMinX + marginX + rx * (areaX - 2.0f * marginX);
        const float targetY = settings.areaMinY + marginY + ry * (areaY - 2.0f * marginY);

        SpriteInstance& instance = out[i];
        instance.destination[0] = tile.face.centerX + (targetX - tile.face.centerX) * spread;
        instance.destination[1] = tile.face.centerY + (targetY - tile.face.centerY) * spread;
        instance.destination[2] = dstHalfX;
        instance.destination[3] = dstHalfY;

        instance.source[0] = tile.face.centerX;
        instance.source[1] = tile.face.centerY;
        instance.source[2] = halfU;
        instance.source[3] = halfV;

        instance.style[0] = (rr * 2.0f - 1.0f) * maxRadian;
        instance.style[1] = opacity * smooth(tile.fade);
        instance.style[2] = 0.0f;
        instance.style[3] = 0.0f;
    }

    return n;
}

} // namespace atemfx
