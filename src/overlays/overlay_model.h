#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace atemfx {

inline constexpr std::size_t kMaxOverlayLayers        = 4;
inline constexpr std::size_t kMaxOverlaySequenceFrames = 300;
inline constexpr float       kOverlaySequenceFps       = 30.0f;
inline constexpr float       kOverlayFadeSeconds       = 0.35f;

enum class OverlayAspect
{
    Landscape16x9,
    Portrait9x16,
};

enum class OverlayKind
{
    Still,
    PngSequence,
};

enum class OverlayPlayback
{
    Loop,
    OneShot,
};

enum class OverlayLayerPhase
{
    Disabled,
    Buffering,
    FadingIn,
    Live,
    FadingOut,
    Error,
};

using OverlayLayerId = std::uint64_t;

struct OverlayLayerConfig
{
    OverlayLayerId  layerId        = 0;
    std::string     assetId;
    bool            enabled        = false;
    float           opacity        = 1.0f;
    OverlayPlayback playback       = OverlayPlayback::Loop;
    float           framesPerSecond = kOverlaySequenceFps;
};

struct OverlayStackConfig
{
    std::array<OverlayLayerConfig, kMaxOverlayLayers> layers;
    std::size_t                                       layerCount = 0;
};

struct OverlayLayerStatus
{
    OverlayLayerConfig config;
    OverlayLayerPhase  phase          = OverlayLayerPhase::Disabled;
    std::uint32_t      displayedFrame = 0;
    std::uint32_t      frameCount     = 0;
    std::uint64_t      skippedFrames  = 0;
    std::uint64_t      underflows     = 0;
    bool               animated       = false;
    bool               paused         = false;
    bool               replacing      = false;
    std::string        error;
};

struct OverlayUiSnapshot
{
    std::array<OverlayLayerStatus, kMaxOverlayLayers> layers;
    std::size_t                                       layerCount = 0;
    bool                                              animationSlotAvailable = true;
};

const char* overlayAspectName(OverlayAspect aspect);
const char* overlayKindName(OverlayKind kind);
const char* overlayLayerPhaseName(OverlayLayerPhase phase);

} // namespace atemfx
