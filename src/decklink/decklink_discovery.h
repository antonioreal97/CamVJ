#pragma once

namespace atemfx {

// One-shot diagnostic, called before App/GPU startup. Returns 0 when
// enumeration completes (including no devices), 1 when unavailable or failed.
int runDeckLinkDiscovery();

} // namespace atemfx
