#include "tracking/FaceSensor.h"

namespace atemfx {

// No face detector on this platform yet. Face-driven effects run and simply
// see an empty crowd; see docs/TRACKING.md, The Windows gap.
std::unique_ptr<FaceSensor> createFaceSensor()
{
    return nullptr;
}

} // namespace atemfx
