#include "overlays/overlay_library.h"
#include "overlays/overlay_platform.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace atemfx {

// Platform seam stub: the first byte is enough to express an exact canvas in
// this storage/manifest test. Native PNG decoders are covered by the headless
// shader/runtime gate and platform builds.
bool decodeOverlayPng(const std::filesystem::path& path,
                      DecodedOverlayImage& out, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    char marker = 0;
    if (!file.get(marker) || (marker != 'L' && marker != 'P'))
    {
        error = "fake PNG is invalid";
        return false;
    }
    out.width = marker == 'L' ? 1920U : 1080U;
    out.height = marker == 'L' ? 1080U : 1920U;
    out.rowBytes = static_cast<std::size_t>(out.width) * 4U;
    return true;
}

bool replaceOverlayFileAtomically(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::string& error)
{
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    if (ec)
    {
        error = ec.message();
        return false;
    }
    return true;
}

std::unique_ptr<OverlaySourcePicker> createOverlaySourcePicker() { return {}; }

} // namespace atemfx

namespace {

using namespace atemfx;
namespace fs = std::filesystem;

int checks = 0;
int failures = 0;

void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

void writeMarker(const fs::path& path, const char* marker)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << marker;
}

std::string readText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

fs::path temporaryRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("camvj_overlay_library_" + std::to_string(stamp));
}

void checkImportScanAndOrdering(const fs::path& root)
{
    OverlayLibrary library(root / "managed");
    std::string error;
    expect(library.scan(error), "an empty managed library can be created");

    const fs::path still = root / "brand.png";
    writeMarker(still, "L-still");
    OverlayImportRequest request;
    request.name = "Brand";
    request.sources.push_back({OverlayAspect::Portrait9x16, still, true});
    OverlayAsset imported;
    expect(library.importAsset(request, imported, error),
           "a new asset infers 16:9 from its exact raster");
    expect(imported.variant(OverlayAspect::Landscape16x9) != nullptr &&
               imported.variant(OverlayAspect::Portrait9x16) == nullptr,
           "the inferred variant occupies only its matching slot");

    OverlayLibrary rescanned(root / "managed");
    expect(rescanned.scan(error) && rescanned.find(imported.id),
           "published manifests survive a fresh library scan");

    const fs::path sequence = root / "sequence";
    writeMarker(sequence / "frame10.png", "L-ten");
    writeMarker(sequence / "frame2.png", "L-two");
    OverlayImportRequest sequenceRequest;
    sequenceRequest.name = "Sequence";
    sequenceRequest.sources.push_back(
        {OverlayAspect::Landscape16x9, sequence, true});
    OverlayAsset sequenceAsset;
    expect(library.importAsset(sequenceRequest, sequenceAsset, error),
           "a valid PNG folder imports as one sequence asset");
    const OverlayVariant* variant = sequenceAsset.variant(OverlayAspect::Landscape16x9);
    expect(variant && variant->frames.size() == 2 &&
               readText(variant->frames[0]) == "L-two" &&
               readText(variant->frames[1]) == "L-ten",
           "sequence frames are canonicalised in natural filename order");
}

void checkFailureAtomicity(const fs::path& root)
{
    OverlayLibrary library(root / "managed_failures");
    std::string error;
    expect(library.scan(error), "failure test library starts empty");

    const fs::path mixed = root / "mixed";
    writeMarker(mixed / "frame1.png", "L-one");
    writeMarker(mixed / "frame2.png", "P-two");
    OverlayImportRequest mixedRequest;
    mixedRequest.name = "Mixed";
    mixedRequest.sources.push_back({OverlayAspect::Landscape16x9, mixed, true});
    OverlayAsset ignored;
    expect(!library.importAsset(mixedRequest, ignored, error) && library.assets().empty(),
           "mixed 16:9/9:16 frames reject the whole sequence atomically");

    const fs::path still = root / "cancelled.png";
    writeMarker(still, "L-cancel");
    OverlayImportRequest cancelRequest;
    cancelRequest.name = "Cancelled";
    cancelRequest.sources.push_back({OverlayAspect::Landscape16x9, still, true});
    OverlayImportControl control;
    control.cancelRequested.store(true, std::memory_order_release);
    expect(!library.importAsset(cancelRequest, ignored, error, &control) &&
               library.assets().empty(),
           "cancellation before publication leaves no logical asset");
}

void checkVariantAndRemoval(const fs::path& root)
{
    OverlayLibrary library(root / "managed_variant");
    std::string error;
    expect(library.scan(error), "variant test library starts empty");
    const fs::path landscape = root / "landscape.png";
    const fs::path portrait = root / "portrait.png";
    writeMarker(landscape, "L-base");
    writeMarker(portrait, "P-variant");

    OverlayImportRequest request;
    request.name = "Two formats";
    request.sources.push_back({OverlayAspect::Landscape16x9, landscape, false});
    OverlayAsset asset;
    expect(library.importAsset(request, asset, error), "base variant imports");
    const fs::path original = asset.variant(OverlayAspect::Landscape16x9)->frames[0];

    OverlayImportSource unsafe{OverlayAspect::Portrait9x16, portrait, true};
    OverlayAsset updated;
    expect(!library.replaceVariant(asset.id, unsafe, updated, error) &&
               library.find(asset.id)->variant(OverlayAspect::Landscape16x9)->frames[0] == original,
           "variant replacement never infers a different manifest slot");

    OverlayImportSource explicitPortrait{OverlayAspect::Portrait9x16, portrait, false};
    expect(library.replaceVariant(asset.id, explicitPortrait, updated, error) &&
               updated.variant(OverlayAspect::Landscape16x9) &&
               updated.variant(OverlayAspect::Portrait9x16),
           "an explicit second format preserves the stable logical asset id");
    expect(library.removeAsset(asset.id, error) && !library.find(asset.id),
           "authorised removal leaves the active library and moves storage to trash");
}

} // namespace

int main()
{
    const fs::path root = temporaryRoot();
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    checkImportScanAndOrdering(root);
    checkFailureAtomicity(root);
    checkVariantAndRemoval(root);

    fs::remove_all(root, ec);
    if (failures == 0)
        std::printf("overlay library: %d checks passed\n", checks);
    return failures == 0 ? 0 : 1;
}
