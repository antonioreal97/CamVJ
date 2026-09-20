#include "effects/face_tiles.h"

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

atemfx::FacesSnapshot crowd(uint32_t faces, float confidence = 0.9f)
{
    atemfx::FacesSnapshot s;
    s.available = true;
    s.count     = faces;
    for (uint32_t i = 0; i < faces; ++i)
    {
        s.faces[i].trackId    = 100 + i;
        s.faces[i].centerX    = 0.1f + 0.05f * static_cast<float>(i);
        s.faces[i].centerY    = 0.5f;
        s.faces[i].width      = 0.05f;
        s.faces[i].height     = 0.09f;
        s.faces[i].confidence = confidence;
    }
    return s;
}

std::size_t build(const atemfx::FaceTileManager& m, const atemfx::FaceTileSettings& s,
                  atemfx::SpriteInstance* out)
{
    return m.build(out, atemfx::kMaxSpriteInstances, s);
}

void checkCopiesPerFace()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace = 4;

    m.update(crowd(2), s, 1.0f / 60.0f);
    expect(m.activeTiles() == 8, "two faces x four copies = eight tiles");
}

void checkMaxTilesAndMaxFaces()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace = 8;
    s.maxTiles      = 10;

    m.update(crowd(4), s, 1.0f / 60.0f);
    expect(m.activeTiles() == 10, "tile budget caps the total");

    atemfx::FaceTileManager faces;
    s.maxTiles = 64;
    s.maxFaces = 2;
    faces.update(crowd(4), s, 1.0f / 60.0f);
    expect(faces.activeTiles() == 16, "face budget takes the oldest faces only");
}

void checkThreshold()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.minConfidence = 0.8f;

    m.update(crowd(3, 0.6f), s, 1.0f / 60.0f);
    expect(m.activeTiles() == 0, "faces under the threshold get no tiles");
}

void checkFadeInAndOut()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace = 1;
    s.fadeSeconds   = 0.5f;
    s.opacity       = 1.0f;

    atemfx::SpriteInstance out[atemfx::kMaxSpriteInstances];

    m.update(crowd(1), s, 0.1f);
    expect(build(m, s, out) == 1 && out[0].style[1] > 0.0f && out[0].style[1] < 0.5f,
           "a new tile fades in, it does not pop");

    for (int i = 0; i < 10; ++i)
    {
        m.update(crowd(1), s, 0.1f);
    }
    expect(build(m, s, out) == 1 && near(out[0].style[1], 1.0f, 1e-5f), "a settled tile is fully in");

    m.update(crowd(0), s, 0.1f);
    expect(m.activeTiles() == 1, "a lost face's tile fades rather than vanishing");

    for (int i = 0; i < 10; ++i)
    {
        m.update(crowd(0), s, 0.1f);
    }
    expect(m.activeTiles() == 0, "and is gone once the fade ends");

    atemfx::FaceTileSettings instant = s;
    instant.fadeSeconds              = 0.0f;
    atemfx::FaceTileManager snap;
    snap.update(crowd(1), instant, 1.0f / 60.0f);
    expect(build(snap, instant, out) == 1 && near(out[0].style[1], 1.0f, 1e-5f),
           "fade 0 is a cut, not a division by zero");
}

void checkLayoutIsStableAndSlidersRetarget()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace = 3;
    s.fadeSeconds   = 0.0f;
    s.minScale      = 1.0f;
    s.maxScale      = 2.0f;

    atemfx::SpriteInstance a[atemfx::kMaxSpriteInstances];
    atemfx::SpriteInstance b[atemfx::kMaxSpriteInstances];

    m.update(crowd(1), s, 1.0f / 60.0f);
    const std::size_t n = build(m, s, a);
    m.update(crowd(1), s, 1.0f / 60.0f);
    build(m, s, b);

    bool same = n == 3;
    for (std::size_t i = 0; i < n; ++i)
    {
        same = same && a[i].destination[0] == b[i].destination[0] &&
               a[i].destination[1] == b[i].destination[1];
    }
    expect(same, "tiles do not wander from frame to frame");

    atemfx::FaceTileSettings bigger = s;
    bigger.minScale = 3.0f;
    bigger.maxScale = 3.0f;
    build(m, bigger, b);
    expect(near(b[0].destination[2], a[0].source[2] * 3.0f, 1e-5f),
           "a scale slider resizes tiles already on the wall");

    atemfx::FaceTileSettings reseeded = s;
    reseeded.seed = 7;
    build(m, reseeded, b);
    expect(a[0].destination[0] != b[0].destination[0] || a[0].destination[1] != b[0].destination[1],
           "a new seed reshuffles the layout");
}

void checkSourceIsThePaddedFaceAndTilesStayInArea()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace = 8;
    s.fadeSeconds   = 0.0f;
    s.padding       = 0.5f;
    s.minScale      = 1.0f;
    s.maxScale      = 1.0f;
    s.areaMinX      = 0.34f;  // a 9:16 strip
    s.areaMaxX      = 0.66f;

    m.update(crowd(1), s, 1.0f / 60.0f);
    atemfx::SpriteInstance out[atemfx::kMaxSpriteInstances];
    const std::size_t      n = build(m, s, out);

    expect(near(out[0].source[0], 0.1f, 1e-6f) && near(out[0].source[2], 0.05f * 0.5f * 1.5f, 1e-6f),
           "source crop is the face plus padding");

    bool inside = n == 8;
    for (std::size_t i = 0; i < n; ++i)
    {
        inside = inside && out[i].destination[0] - out[i].destination[2] >= 0.34f - 1e-5f &&
                 out[i].destination[0] + out[i].destination[2] <= 0.66f + 1e-5f;
    }
    expect(inside, "scattered tiles land inside the output window");

    atemfx::FaceTileSettings still = s;
    still.spread = 0.0f;
    build(m, still, out);
    expect(near(out[0].destination[0], out[0].source[0], 1e-6f), "spread 0 keeps copies on the face");
}

void checkRotationBoundsAndReset()
{
    atemfx::FaceTileManager  m;
    atemfx::FaceTileSettings s;
    s.copiesPerFace      = 8;
    s.fadeSeconds        = 0.0f;
    s.maxRotationDegrees = 15.0f;

    m.update(crowd(4), s, 1.0f / 60.0f);
    atemfx::SpriteInstance out[atemfx::kMaxSpriteInstances];
    const std::size_t      n = build(m, s, out);

    bool bounded = n == 32;
    bool turned  = false;
    for (std::size_t i = 0; i < n; ++i)
    {
        bounded = bounded && std::abs(out[i].style[0]) <= 15.0f * 3.14159265f / 180.0f + 1e-5f;
        turned  = turned || std::abs(out[i].style[0]) > 0.01f;
    }
    expect(bounded && turned, "rotation stays within the range and is used");

    expect(m.build(out, 5, s) == 5, "build respects the caller's capacity");

    m.reset();
    expect(m.activeTiles() == 0, "reset clears the table");
}

} // namespace

int main()
{
    checkCopiesPerFace();
    checkMaxTilesAndMaxFaces();
    checkThreshold();
    checkFadeInAndOut();
    checkLayoutIsStableAndSlidersRetarget();
    checkSourceIsThePaddedFaceAndTilesStayInArea();
    checkRotationBoundsAndReset();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d / %d checks failed\n", failures, checks);
        return 1;
    }

    std::printf("%d checks passed\n", checks);
    return 0;
}
