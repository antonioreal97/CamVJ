#include "decklink/decklink_discovery.h"

#include "core/Log.h"

namespace atemfx {

int runDeckLinkDiscovery()
{
#if defined(_WIN32)
    ATEMFX_LOG_ERROR("DeckLink discovery is disabled in this build. Reconfigure with "
                     "ATEMFX_ENABLE_DECKLINK=ON and ATEMFX_DECKLINK_SDK_DIR pointing "
                     "to the extracted DeckLink SDK. See docs/BUILD.md.");
#else
    ATEMFX_LOG_ERROR("DeckLink discovery is supported only by the Windows build of CamVJ. "
                     "Use --headless --frames 200 to check the GPU engine on macOS.");
#endif
    return 1;
}

} // namespace atemfx
