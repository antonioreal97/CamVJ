#include "decklink/decklink_capture.h"

namespace atemfx {

std::vector<VideoSourceDescriptor> enumerateDeckLinkCaptureSources()
{
    return {};
}

std::unique_ptr<DeckLinkCapture> createDeckLinkCapture()
{
    return nullptr;
}

} // namespace atemfx
