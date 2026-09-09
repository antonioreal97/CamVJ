#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "video/VideoSource.h"

namespace atemfx {

inline constexpr const char* kDeckLinkSourceIdPrefix = "decklink:";

struct DeckLinkCaptureFrame
{
    const uint8_t* pixels = nullptr;
    uint32_t       width = 0;
    uint32_t       height = 0;
    std::size_t    rowBytes = 0;
    bool           bottomUp = false;
};

using DeckLinkCaptureCallback = std::function<void(const DeckLinkCaptureFrame&)>;

class DeckLinkCapture
{
public:
    virtual ~DeckLinkCapture() = default;

    DeckLinkCapture(const DeckLinkCapture&) = delete;
    DeckLinkCapture& operator=(const DeckLinkCapture&) = delete;

    virtual bool start(const std::string& deviceId,
                       DeckLinkCaptureCallback callback,
                       std::string& error) = 0;
    virtual void stop() = 0;
    virtual void poll() {}
    virtual std::string status() const = 0;

protected:
    DeckLinkCapture() = default;
};

std::vector<VideoSourceDescriptor> enumerateDeckLinkCaptureSources();
std::unique_ptr<DeckLinkCapture> createDeckLinkCapture();

} // namespace atemfx
