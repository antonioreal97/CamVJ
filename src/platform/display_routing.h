#pragma once

#include "platform/Display.h"

#include <vector>

namespace atemfx {

enum class DisplayRouteLoss
{
    None,
    Disconnected,
    ModeChanged
};

struct DisplayRoutingUpdate
{
    int selected  = -1;
    int requested = -1;
    DisplayRouteLoss loss = DisplayRouteLoss::None;
};

// Indexes belong to the previous snapshot. A lost or changed live destination
// cancels pending routing too, so a topology change cannot take another wall live.
// This only reconciles state; opening and closing output remain the caller's job.
DisplayRoutingUpdate reconcileDisplayRouting(const std::vector<DisplayInfo>& previous,
                                            const std::vector<DisplayInfo>& current,
                                            int selectedIndex,
                                            int requestedIndex) noexcept;

} // namespace atemfx
