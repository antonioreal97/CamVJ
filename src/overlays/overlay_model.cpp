#include "overlays/overlay_model.h"

namespace atemfx {

const char* overlayAspectName(OverlayAspect aspect)
{
    switch (aspect)
    {
    case OverlayAspect::Landscape16x9: return "16:9";
    case OverlayAspect::Portrait9x16:  return "9:16";
    }
    return "16:9";
}

const char* overlayKindName(OverlayKind kind)
{
    switch (kind)
    {
    case OverlayKind::Still:       return "Still";
    case OverlayKind::PngSequence: return "PNG Sequence";
    }
    return "Still";
}

const char* overlayLayerPhaseName(OverlayLayerPhase phase)
{
    switch (phase)
    {
    case OverlayLayerPhase::Disabled:  return "Disabled";
    case OverlayLayerPhase::Buffering: return "Buffering";
    case OverlayLayerPhase::FadingIn:  return "Fading In";
    case OverlayLayerPhase::Live:      return "Live";
    case OverlayLayerPhase::FadingOut: return "Fading Out";
    case OverlayLayerPhase::Error:     return "Error";
    }
    return "Error";
}

} // namespace atemfx
