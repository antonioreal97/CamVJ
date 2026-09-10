#include "platform/display_routing.h"

#include <cmath>
#include <cstddef>

namespace atemfx {
namespace {

const DisplayInfo* previousDisplay(const std::vector<DisplayInfo>& displays, int index) noexcept
{
    if (index < 0 || static_cast<std::size_t>(index) >= displays.size())
        return nullptr;
    return &displays[static_cast<std::size_t>(index)];
}

int currentIndex(const std::vector<DisplayInfo>& displays, const DisplayInfo* previous) noexcept
{
    if (!previous)
        return -1;
    for (std::size_t i = 0; i < displays.size(); ++i)
    {
        if (displays[i].id == previous->id)
            return static_cast<int>(i);
    }
    return -1;
}

bool modeChanged(const DisplayInfo& previous, const DisplayInfo& current) noexcept
{
    if (previous.width != current.width || previous.height != current.height)
        return true;

    // Arrangement and scaled resolution leave the native raster alone and move
    // the output window: the window is created on the desktop rect at open and
    // is never repositioned.
    if (previous.desktopX != current.desktopX || previous.desktopY != current.desktopY ||
        previous.desktopWidth != current.desktopWidth ||
        previous.desktopHeight != current.desktopHeight)
        return true;

    if (std::abs(previous.scaleFactor - current.scaleFactor) > 1e-3f)
        return true;

    // Unknown refresh rates cannot establish a mode change. Small differences
    // in reported precision must not interrupt an otherwise stable destination.
    const bool knownRates = std::isfinite(previous.refreshHz) && previous.refreshHz > 0.0 &&
                            std::isfinite(current.refreshHz) && current.refreshHz > 0.0;
    return knownRates && std::abs(previous.refreshHz - current.refreshHz) > 0.01;
}

} // namespace

DisplayRoutingUpdate reconcileDisplayRouting(const std::vector<DisplayInfo>& previous,
                                            const std::vector<DisplayInfo>& current,
                                            int selectedIndex,
                                            int requestedIndex) noexcept
{
    DisplayRoutingUpdate update;
    const auto* selected = previousDisplay(previous, selectedIndex);
    update.selected = currentIndex(current, selected);
    if (selected)
    {
        if (update.selected < 0)
            update.loss = DisplayRouteLoss::Disconnected;
        else if (modeChanged(*selected, current[static_cast<std::size_t>(update.selected)]))
            update.loss = DisplayRouteLoss::ModeChanged;

        if (update.loss != DisplayRouteLoss::None)
        {
            update.selected = -1;
            return update;
        }
    }

    update.requested = currentIndex(current, previousDisplay(previous, requestedIndex));
    return update;
}

} // namespace atemfx
