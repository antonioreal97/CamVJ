#include "tracking/Tracker.h"

namespace atemfx {

// Platforms with no subject detection. Windows is the production platform and
// this stub is what it gets today: the Auto Frame effect still runs, driven by
// its manual controls, and the status line says why it is not following.
// docs/TRACKING.md has the options for closing this gap.
std::unique_ptr<Tracker> createSubjectTracker()
{
    return nullptr;
}

} // namespace atemfx
