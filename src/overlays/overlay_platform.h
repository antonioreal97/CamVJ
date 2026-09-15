#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace atemfx {

// CPU-side import/prepare representation. The compositor uploads this data once;
// it is never decoded from disk in the render hot path. Pixels are premultiplied-
// alpha BGRA8, with the first row representing the top of the image.
struct DecodedOverlayImage
{
    std::uint32_t             width    = 0;
    std::uint32_t             height   = 0;
    std::size_t               rowBytes = 0;
    std::vector<std::uint8_t> bgra8;
};

// Native PNG decoding lives behind the platform seam so the library adds no
// third-party dependency (ImageIO/CoreGraphics on macOS, WIC on Windows).
bool decodeOverlayPng(const std::filesystem::path& path,
                      DecodedOverlayImage&         out,
                      std::string&                 error);

// Atomically publishes a fully-written file over destination. Source and
// destination must be on the same volume. On success source no longer exists.
bool replaceOverlayFileAtomically(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::string&                 error);

enum class OverlayPickerMode
{
    StillPng,
    SequenceDirectory,
};

enum class OverlayPickerState
{
    Idle,
    Picking,
    Selected,
    Cancelled,
    Failed,
};

struct OverlayPickerResult
{
    OverlayPickerState        state = OverlayPickerState::Idle;
    std::filesystem::path     path;
    std::string               error;
};

// The picker is deliberately pollable: native completion callbacks never need
// to touch Dear ImGui or application state. Call begin/poll/cancel from the UI
// thread. nativeParent is NSView* on macOS and HWND on Windows; it may be null.
class OverlaySourcePicker
{
public:
    virtual ~OverlaySourcePicker() = default;

    virtual bool begin(OverlayPickerMode mode, void* nativeParent, std::string& error) = 0;
    virtual OverlayPickerState state() const = 0;

    // Returns true once for a terminal result, then resets the picker to Idle.
    virtual bool poll(OverlayPickerResult& out) = 0;
    virtual void cancel() = 0;
};

std::unique_ptr<OverlaySourcePicker> createOverlaySourcePicker();

} // namespace atemfx
