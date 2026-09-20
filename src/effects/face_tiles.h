#pragma once

#include <cstddef>
#include <cstdint>

#include "gpu/Rhi.h"
#include "tracking/FacesSnapshot.h"

namespace atemfx {

struct FaceTileSettings
{
    // Which faces get tiles: the oldest `maxFaces` at or above the threshold.
    int   maxFaces      = 8;
    float minConfidence = 0.50f;

    int copiesPerFace = 4;
    int maxTiles      = 32;

    // Margin around the detected box, as a fraction of its size. Vision boxes
    // are tight on the face; without padding the crop cuts through the chin.
    float padding = 0.35f;

    float minScale           = 0.7f;
    float maxScale           = 1.8f;
    float maxRotationDegrees = 0.0f;

    // 0 keeps every copy on its face, 1 scatters them across the whole canvas.
    float spread = 1.0f;

    float opacity     = 1.0f;
    float fadeSeconds = 0.30f;

    // Reshuffles every tile's random placement. Same seed, same layout.
    uint32_t seed = 0;

    // Where scattered tiles may land, canvas UV. The 9:16 window when the
    // output is portrait, so no copy is placed in the letterbox bars.
    float areaMinX = 0.0f;
    float areaMinY = 0.0f;
    float areaMaxX = 1.0f;
    float areaMaxY = 1.0f;
};

// Turns tracked faces into independent copies on the canvas ("FaceTiles").
//
// A tile's random numbers are a pure function of (seed, track id, copy);
// scale, rotation and position are derived from the settings on every build,
// so moving a slider retargets the tiles already on the wall instead of
// waiting for new ones. The table is
// fixed-size and lives in the effect: nothing here allocates.
//
// The tile samples the *live* face region, so it follows the person between
// detections. Tested in tests/face_tiles_test.cpp.
class FaceTileManager
{
public:
    static constexpr std::size_t kMaxTiles = kMaxSpriteInstances;

    void update(const FacesSnapshot& faces, const FaceTileSettings& settings, float deltaTime);

    // Writes at most `capacity` instances, oldest tile first, and returns how
    // many. `faces` and the instances share the canvas UV space.
    std::size_t build(SpriteInstance* out, std::size_t capacity, const FaceTileSettings& settings) const;

    void reset();

    std::size_t activeTiles() const;

private:
    struct Tile
    {
        bool     active  = false;
        bool     wanted  = false;
        uint32_t trackId = 0;
        uint32_t copy    = 0;
        uint64_t born    = 0;  // creation order, for stable draw order

        float           fade = 0.0f;  // 0 invisible .. 1 fully in
        FaceObservation face;
    };

    Tile     tiles_[kMaxTiles];
    uint64_t births_ = 0;
};

// Deterministic 0..1 value from a seed and three keys. Exposed for the tests.
float faceTileRandom(uint32_t seed, uint32_t trackId, uint32_t copy, uint32_t channel);

} // namespace atemfx
