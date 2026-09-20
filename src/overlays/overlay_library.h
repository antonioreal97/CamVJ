#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "overlays/overlay_model.h"

namespace atemfx {

struct OverlayVariant
{
    OverlayAspect                      aspect = OverlayAspect::Landscape16x9;
    std::uint32_t                      width  = 0;
    std::uint32_t                      height = 0;
    std::vector<std::filesystem::path> frames;
};

struct OverlayAsset
{
    std::string                                  id;
    std::string                                  name;
    OverlayKind                                 kind           = OverlayKind::Still;
    std::uint32_t                               fpsNumerator   = 30;
    std::uint32_t                               fpsDenominator = 1;
    std::array<std::optional<OverlayVariant>, 2> variants;

    const OverlayVariant* variant(OverlayAspect aspect) const;
    OverlayVariant*       variant(OverlayAspect aspect);
};

struct OverlayImportSource
{
    OverlayAspect          aspect = OverlayAspect::Landscape16x9;
    std::filesystem::path path;
    // New imports infer the logical format from the exact source raster.
    // Variant add/replace keeps this false because the operator chose a slot.
    bool                  inferAspect = false;
};

struct OverlayImportRequest
{
    std::string                      name;
    std::vector<OverlayImportSource> sources;
};

struct OverlayLibraryWarning
{
    std::filesystem::path path;
    std::string           message;
};

struct OverlayImportControl
{
    std::atomic<bool>  cancelRequested{false};
    std::atomic<float> progress{0.0f};
};

// Managed, control-thread-only asset library. Import and scan perform disk IO
// and PNG validation; callers must keep them off the render/video hot path.
// Asset pointers are stable across imports/replacements, but not removal or a
// scan that discovers that an asset was deleted externally. copyAsset() is the
// safe hand-off for asynchronous prepare/playback work.
class OverlayLibrary
{
public:
    OverlayLibrary();
    explicit OverlayLibrary(std::filesystem::path rootDirectory);

    const std::filesystem::path& rootDirectory() const { return rootDirectory_; }

    bool scan(std::string& error);

    std::vector<OverlayAsset>             assets() const;
    const OverlayAsset*                   find(std::string_view id) const;
    bool                                  copyAsset(std::string_view id, OverlayAsset& out) const;
    const std::vector<OverlayLibraryWarning>& warnings() const { return warnings_; }

    // Imports one logical asset with one or both aspect variants. The source is
    // copied into the managed library and never referenced in place. Import does
    // not activate a layer.
    bool importAsset(const OverlayImportRequest& request,
                     OverlayAsset&               imported,
                     std::string&                error,
                     OverlayImportControl*       control = nullptr);

    // Adds or replaces one aspect while preserving assetId. Publication is an
    // atomic manifest swap; a validation/copy failure leaves the old variant live.
    bool replaceVariant(std::string_view          assetId,
                        const OverlayImportSource& source,
                        OverlayAsset&              updated,
                        std::string&               error,
                        OverlayImportControl*      control = nullptr);

    // Reference checks live in App, which owns the active stack and presets.
    // Once authorised, removal is recoverable: the asset is renamed into the
    // library's .trash directory rather than recursively deleted on the UI
    // thread.
    bool removeAsset(std::string_view assetId, std::string& error);

    // Storage records are public for the small, translation-unit-local JSON
    // codec. They are not part of the operator/runtime model; callers should
    // use OverlayAsset and the methods above.
    struct ManifestVariant
    {
        OverlayAspect aspect = OverlayAspect::Landscape16x9;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t frameCount = 0;
        std::string   contentId;
    };

    struct Manifest
    {
        std::uint32_t                version        = 1;
        std::string                  id;
        std::string                  name;
        OverlayKind                 kind           = OverlayKind::Still;
        std::uint32_t               fpsNumerator   = 30;
        std::uint32_t               fpsDenominator = 1;
        std::vector<ManifestVariant> variants;
    };

    struct AssetRecord
    {
        OverlayAsset asset;
        Manifest     manifest;
    };

private:

    std::filesystem::path                              rootDirectory_;
    std::map<std::string, AssetRecord, std::less<>>   records_;
    std::vector<OverlayLibraryWarning>                 warnings_;
};

} // namespace atemfx
