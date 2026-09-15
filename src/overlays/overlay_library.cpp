#include "overlays/overlay_library.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <system_error>
#include <utility>

#include "core/Paths.h"
#include "overlays/overlay_platform.h"

namespace atemfx {
namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kLandscapeWidth  = 1920;
constexpr std::uint32_t kLandscapeHeight = 1080;
constexpr std::uint32_t kPortraitWidth   = 1080;
constexpr std::uint32_t kPortraitHeight  = 1920;

std::size_t aspectIndex(OverlayAspect aspect)
{
    return aspect == OverlayAspect::Portrait9x16 ? 1U : 0U;
}

const char* aspectToken(OverlayAspect aspect)
{
    return aspect == OverlayAspect::Portrait9x16 ? "portrait" : "landscape";
}

bool aspectFromToken(std::string_view token, OverlayAspect& out)
{
    if (token == "landscape")
    {
        out = OverlayAspect::Landscape16x9;
        return true;
    }
    if (token == "portrait")
    {
        out = OverlayAspect::Portrait9x16;
        return true;
    }
    return false;
}

const char* kindToken(OverlayKind kind)
{
    return kind == OverlayKind::PngSequence ? "sequence" : "still";
}

bool kindFromToken(std::string_view token, OverlayKind& out)
{
    if (token == "still")
    {
        out = OverlayKind::Still;
        return true;
    }
    if (token == "sequence")
    {
        out = OverlayKind::PngSequence;
        return true;
    }
    return false;
}

std::string lowerAscii(std::string value)
{
    for (char& c : value)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool isPng(const fs::path& path)
{
    return lowerAscii(path.extension().string()) == ".png";
}

bool naturalLess(const fs::path& left, const fs::path& right)
{
    const std::string a = lowerAscii(left.filename().string());
    const std::string b = lowerAscii(right.filename().string());
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size())
    {
        const bool digitA = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool digitB = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (digitA && digitB)
        {
            std::size_t endA = i;
            std::size_t endB = j;
            while (endA < a.size() && std::isdigit(static_cast<unsigned char>(a[endA]))) ++endA;
            while (endB < b.size() && std::isdigit(static_cast<unsigned char>(b[endB]))) ++endB;
            std::size_t significantA = i;
            std::size_t significantB = j;
            while (significantA + 1 < endA && a[significantA] == '0') ++significantA;
            while (significantB + 1 < endB && b[significantB] == '0') ++significantB;
            const std::size_t digitsA = endA - significantA;
            const std::size_t digitsB = endB - significantB;
            if (digitsA != digitsB) return digitsA < digitsB;
            const int numeric = a.compare(significantA, digitsA, b, significantB, digitsB);
            if (numeric != 0) return numeric < 0;
            if (endA - i != endB - j) return endA - i < endB - j;
            i = endA;
            j = endB;
            continue;
        }
        if (a[i] != b[j]) return a[i] < b[j];
        ++i;
        ++j;
    }
    if (a.size() != b.size()) return a.size() < b.size();
    return left.filename().string() < right.filename().string();
}

std::string randomId(const char* prefix)
{
    std::random_device random;
    std::array<std::uint32_t, 4> words{};
    for (std::uint32_t& word : words) word = random();
    char text[64];
    std::snprintf(text, sizeof(text), "%s%08x%08x%08x%08x", prefix,
                  words[0], words[1], words[2], words[3]);
    return text;
}

void appendEscaped(std::string& out, std::string_view value)
{
    out.push_back('"');
    for (char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
            break;
        }
    }
    out.push_back('"');
}

struct JsonCursor
{
    std::string_view text;
    std::size_t index = 0;
    std::string* error = nullptr;

    bool fail(std::string message)
    {
        if (error) *error = std::move(message);
        return false;
    }

    void whitespace()
    {
        while (index < text.size() &&
               (text[index] == ' ' || text[index] == '\n' || text[index] == '\r' ||
                text[index] == '\t'))
            ++index;
    }

    bool peek(char token)
    {
        whitespace();
        return index < text.size() && text[index] == token;
    }

    bool expect(char token)
    {
        whitespace();
        if (index >= text.size() || text[index] != token)
            return fail("Malformed overlay manifest");
        ++index;
        return true;
    }

    bool string(std::string& out)
    {
        whitespace();
        if (index >= text.size() || text[index++] != '"')
            return fail("Overlay manifest expected a string");
        out.clear();
        while (index < text.size())
        {
            const char c = text[index++];
            if (c == '"') return true;
            if (c == '\\')
            {
                if (index >= text.size()) return fail("Overlay manifest has a bad escape");
                const char escaped = text[index++];
                switch (escaped)
                {
                case '"': case '\\': case '/': out.push_back(escaped); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: return fail("Overlay manifest has an unsupported escape");
                }
            }
            else if (static_cast<unsigned char>(c) < 0x20)
            {
                return fail("Overlay manifest has a control character");
            }
            else
            {
                out.push_back(c);
            }
        }
        return fail("Overlay manifest has an unterminated string");
    }

    bool integer(std::uint32_t& out)
    {
        whitespace();
        if (index >= text.size() || !std::isdigit(static_cast<unsigned char>(text[index])))
            return fail("Overlay manifest expected an integer");
        std::uint64_t value = 0;
        while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index])))
        {
            value = value * 10U + static_cast<unsigned>(text[index++] - '0');
            if (value > 0xffffffffULL) return fail("Overlay manifest integer is too large");
        }
        out = static_cast<std::uint32_t>(value);
        return true;
    }
};

bool parseManifest(std::string_view json, OverlayLibrary::Manifest& manifest, std::string& error)
{
    JsonCursor c{json, 0, &error};
    if (!c.expect('{')) return false;
    bool sawVersion = false;
    bool sawId = false;
    bool sawName = false;
    bool sawKind = false;
    bool sawVariants = false;
    bool firstField = true;
    while (!c.peek('}'))
    {
        if (!firstField && !c.expect(',')) return false;
        firstField = false;
        std::string key;
        if (!c.string(key) || !c.expect(':')) return false;
        if (key == "version")
        {
            if (!c.integer(manifest.version)) return false;
            sawVersion = true;
        }
        else if (key == "id")
        {
            if (!c.string(manifest.id)) return false;
            sawId = true;
        }
        else if (key == "name")
        {
            if (!c.string(manifest.name)) return false;
            sawName = true;
        }
        else if (key == "kind")
        {
            std::string token;
            if (!c.string(token) || !kindFromToken(token, manifest.kind))
                return c.fail("Overlay manifest has an unknown kind");
            sawKind = true;
        }
        else if (key == "fpsNumerator")
        {
            if (!c.integer(manifest.fpsNumerator)) return false;
        }
        else if (key == "fpsDenominator")
        {
            if (!c.integer(manifest.fpsDenominator)) return false;
        }
        else if (key == "variants")
        {
            sawVariants = true;
            if (!c.expect('[')) return false;
            bool firstVariant = true;
            while (!c.peek(']'))
            {
                if (!firstVariant && !c.expect(',')) return false;
                firstVariant = false;
                OverlayLibrary::ManifestVariant variant;
                if (!c.expect('{')) return false;
                bool sawAspect = false;
                bool sawWidth = false;
                bool sawHeight = false;
                bool sawFrames = false;
                bool sawContent = false;
                bool firstVariantField = true;
                while (!c.peek('}'))
                {
                    if (!firstVariantField && !c.expect(',')) return false;
                    firstVariantField = false;
                    std::string field;
                    if (!c.string(field) || !c.expect(':')) return false;
                    if (field == "aspect")
                    {
                        std::string token;
                        if (!c.string(token) || !aspectFromToken(token, variant.aspect))
                            return c.fail("Overlay manifest has an unknown aspect");
                        sawAspect = true;
                    }
                    else if (field == "width")
                    {
                        if (!c.integer(variant.width)) return false;
                        sawWidth = true;
                    }
                    else if (field == "height")
                    {
                        if (!c.integer(variant.height)) return false;
                        sawHeight = true;
                    }
                    else if (field == "frameCount")
                    {
                        if (!c.integer(variant.frameCount)) return false;
                        sawFrames = true;
                    }
                    else if (field == "contentId")
                    {
                        if (!c.string(variant.contentId)) return false;
                        sawContent = true;
                    }
                    else
                    {
                        return c.fail("Overlay manifest has an unknown variant field");
                    }
                }
                if (!c.expect('}')) return false;
                if (!sawAspect || !sawWidth || !sawHeight || !sawFrames || !sawContent)
                    return c.fail("Overlay manifest variant is incomplete");
                manifest.variants.push_back(std::move(variant));
            }
            if (!c.expect(']')) return false;
        }
        else
        {
            return c.fail("Overlay manifest has an unknown field");
        }
    }
    if (!c.expect('}')) return false;
    c.whitespace();
    if (c.index != c.text.size()) return c.fail("Trailing data in overlay manifest");
    if (!sawVersion || !sawId || !sawName || !sawKind || !sawVariants)
        return c.fail("Overlay manifest is incomplete");
    if (manifest.version != 1 || manifest.id.empty() || manifest.name.empty() ||
        manifest.variants.empty() || manifest.variants.size() > 2 ||
        manifest.fpsDenominator == 0 || manifest.fpsNumerator == 0)
        return c.fail("Overlay manifest values are invalid");
    return true;
}

std::string serializeManifest(const OverlayLibrary::Manifest& manifest)
{
    std::string out = "{\n  \"version\": 1,\n  \"id\": ";
    appendEscaped(out, manifest.id);
    out += ",\n  \"name\": ";
    appendEscaped(out, manifest.name);
    out += ",\n  \"kind\": ";
    appendEscaped(out, kindToken(manifest.kind));
    out += ",\n  \"fpsNumerator\": " + std::to_string(manifest.fpsNumerator);
    out += ",\n  \"fpsDenominator\": " + std::to_string(manifest.fpsDenominator);
    out += ",\n  \"variants\": [\n";
    for (std::size_t i = 0; i < manifest.variants.size(); ++i)
    {
        const auto& variant = manifest.variants[i];
        out += "    {\"aspect\": ";
        appendEscaped(out, aspectToken(variant.aspect));
        out += ", \"width\": " + std::to_string(variant.width);
        out += ", \"height\": " + std::to_string(variant.height);
        out += ", \"frameCount\": " + std::to_string(variant.frameCount);
        out += ", \"contentId\": ";
        appendEscaped(out, variant.contentId);
        out += '}';
        if (i + 1 < manifest.variants.size()) out += ',';
        out += '\n';
    }
    out += "  ]\n}\n";
    return out;
}

bool writeText(const fs::path& path, std::string_view text, std::string& error)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        error = "Could not create " + path.string();
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream)
    {
        error = "Could not write " + path.string();
        return false;
    }
    return true;
}

bool readText(const fs::path& path, std::string& text, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open " + path.string();
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof())
    {
        error = "Could not read " + path.string();
        return false;
    }
    return true;
}

bool ensureLibraryDirectories(const fs::path& root, std::string& error)
{
    std::error_code ec;
    fs::create_directories(root / ".staging", ec);
    if (!ec) fs::create_directories(root / ".trash", ec);
    if (ec)
    {
        error = "Could not create overlay library: " + ec.message();
        return false;
    }
    return true;
}

bool validContentId(std::string_view id)
{
    if (!id.starts_with("content_") || id.size() != 40) return false;
    return std::all_of(id.begin() + 8, id.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    });
}

struct PreparedVariant
{
    OverlayLibrary::ManifestVariant manifest;
    fs::path                         contentDirectory;
    std::vector<fs::path>           canonicalFrames;
};

bool importCancelled(OverlayImportControl* control, std::string& error)
{
    if (!control || !control->cancelRequested.load(std::memory_order_acquire)) return false;
    error = "Import cancelled";
    return true;
}

bool collectInputFrames(const OverlayImportSource& source, OverlayKind& kind,
                        std::vector<fs::path>& frames, std::string& error)
{
    std::error_code ec;
    if (fs::is_regular_file(source.path, ec))
    {
        kind = OverlayKind::Still;
        if (!isPng(source.path))
        {
            error = "Static overlays must be PNG files";
            return false;
        }
        frames.push_back(source.path);
        return true;
    }
    if (!fs::is_directory(source.path, ec))
    {
        error = "Overlay source no longer exists";
        return false;
    }

    kind = OverlayKind::PngSequence;
    fs::directory_iterator input(source.path, ec);
    const fs::directory_iterator end;
    while (!ec && input != end)
    {
        const fs::directory_entry entry = *input;
        const std::string filename = entry.path().filename().string();
        bool regular = entry.is_regular_file(ec);
        if (ec) break;
        if (filename.empty() || filename[0] != '.')
        {
            if (!regular || !isPng(entry.path()))
            {
                error = "PNG sequence folders may contain only PNG frame files";
                return false;
            }
            frames.push_back(entry.path());
        }
        input.increment(ec);
    }
    if (ec)
    {
        error = "Could not read PNG sequence: " + ec.message();
        return false;
    }
    std::sort(frames.begin(), frames.end(), naturalLess);
    if (frames.size() < 2 || frames.size() > kMaxOverlaySequenceFrames)
    {
        error = "PNG sequences need between 2 and 300 frames";
        return false;
    }
    return true;
}

bool expectedRaster(OverlayAspect aspect, std::uint32_t width, std::uint32_t height)
{
    return aspect == OverlayAspect::Landscape16x9
        ? width == kLandscapeWidth && height == kLandscapeHeight
        : width == kPortraitWidth && height == kPortraitHeight;
}

bool aspectFromRaster(std::uint32_t width, std::uint32_t height, OverlayAspect& aspect)
{
    if (width == kLandscapeWidth && height == kLandscapeHeight)
    {
        aspect = OverlayAspect::Landscape16x9;
        return true;
    }
    if (width == kPortraitWidth && height == kPortraitHeight)
    {
        aspect = OverlayAspect::Portrait9x16;
        return true;
    }
    return false;
}

bool prepareVariant(const OverlayImportSource& source, const fs::path& stageRoot,
                    OverlayKind requiredKind, PreparedVariant& prepared, std::string& error,
                    OverlayImportControl* control, float progressBase, float progressSpan)
{
    std::vector<fs::path> inputs;
    OverlayKind           kind = OverlayKind::Still;
    if (!collectInputFrames(source, kind, inputs, error)) return false;
    if (kind != requiredKind)
    {
        error = "Both variants of an overlay must use the same media type";
        return false;
    }

    prepared.manifest.contentId   = randomId("content_");
    prepared.manifest.frameCount  = static_cast<std::uint32_t>(inputs.size());
    prepared.contentDirectory     = stageRoot / prepared.manifest.contentId;

    std::error_code ec;
    fs::create_directories(kind == OverlayKind::PngSequence
                               ? prepared.contentDirectory / "frames"
                               : prepared.contentDirectory,
                           ec);
    if (ec)
    {
        error = "Could not stage overlay content: " + ec.message();
        return false;
    }

    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        if (importCancelled(control, error)) return false;
        fs::path destination;
        if (kind == OverlayKind::Still)
        {
            destination = prepared.contentDirectory / "still.png";
        }
        else
        {
            char filename[32];
            std::snprintf(filename, sizeof(filename), "frame_%04zu.png", i);
            destination = prepared.contentDirectory / "frames" / filename;
        }
        fs::copy_file(inputs[i], destination, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            error = "Could not copy overlay frame: " + ec.message();
            return false;
        }

        // Validate the managed copy, not the external source. If an editor is
        // still writing the selected file, the bytes we publish are exactly
        // the bytes that passed PNG and raster validation.
        DecodedOverlayImage decoded;
        if (!decodeOverlayPng(destination, decoded, error)) return false;
        OverlayAspect aspect = source.aspect;
        if (source.inferAspect && !aspectFromRaster(decoded.width, decoded.height, aspect))
        {
            error = "Overlay must be exactly 1920x1080 or 1080x1920";
            return false;
        }
        if (!expectedRaster(aspect, decoded.width, decoded.height))
        {
            error = std::string("Overlay ") + aspectToken(aspect) +
                    " must be exactly " +
                    (aspect == OverlayAspect::Landscape16x9 ? "1920x1080" : "1080x1920");
            return false;
        }
        if (i == 0)
        {
            prepared.manifest.aspect = aspect;
            prepared.manifest.width  = decoded.width;
            prepared.manifest.height = decoded.height;
        }
        else if (aspect != prepared.manifest.aspect)
        {
            error = "Every frame in a PNG sequence must use the same canvas format";
            return false;
        }
        prepared.canonicalFrames.push_back(destination);
        if (control)
        {
            const float fraction = static_cast<float>(i + 1U) /
                                   static_cast<float>(inputs.size());
            control->progress.store(progressBase + progressSpan * fraction,
                                    std::memory_order_release);
        }
    }
    return true;
}

bool manifestToRecord(const fs::path& assetRoot, const OverlayLibrary::Manifest& manifest,
                      OverlayLibrary::AssetRecord& record, std::string& error)
{
    if (manifest.id != assetRoot.filename().string() || !manifest.id.starts_with("ovl_"))
    {
        error = "Overlay manifest id does not match its directory";
        return false;
    }

    OverlayAsset asset;
    asset.id             = manifest.id;
    asset.name           = manifest.name;
    asset.kind           = manifest.kind;
    asset.fpsNumerator   = manifest.fpsNumerator;
    asset.fpsDenominator = manifest.fpsDenominator;

    std::set<std::size_t> seen;
    for (const auto& saved : manifest.variants)
    {
        const std::size_t index = aspectIndex(saved.aspect);
        if (!seen.insert(index).second || !validContentId(saved.contentId) ||
            !expectedRaster(saved.aspect, saved.width, saved.height))
        {
            error = "Overlay manifest has an invalid variant";
            return false;
        }
        if ((manifest.kind == OverlayKind::Still && saved.frameCount != 1) ||
            (manifest.kind == OverlayKind::PngSequence &&
             (saved.frameCount < 2 || saved.frameCount > kMaxOverlaySequenceFrames)))
        {
            error = "Overlay manifest has an invalid frame count";
            return false;
        }

        OverlayVariant variant;
        variant.aspect = saved.aspect;
        variant.width  = saved.width;
        variant.height = saved.height;
        const fs::path content = assetRoot / saved.contentId;
        if (manifest.kind == OverlayKind::Still)
        {
            const fs::path frame = content / "still.png";
            std::error_code frameError;
            if (!fs::is_regular_file(frame, frameError) || frameError)
            {
                error = "Overlay still is missing";
                return false;
            }
            variant.frames.push_back(frame);
        }
        else
        {
            for (std::uint32_t i = 0; i < saved.frameCount; ++i)
            {
                char filename[32];
                std::snprintf(filename, sizeof(filename), "frame_%04u.png", i);
                const fs::path frame = content / "frames" / filename;
                std::error_code frameError;
                if (!fs::is_regular_file(frame, frameError) || frameError)
                {
                    error = "Overlay sequence frame is missing";
                    return false;
                }
                variant.frames.push_back(frame);
            }
        }
        asset.variants[index] = std::move(variant);
    }

    if (manifest.kind == OverlayKind::PngSequence && manifest.variants.size() == 2 &&
        manifest.variants[0].frameCount != manifest.variants[1].frameCount)
    {
        error = "Overlay sequence variants must have the same frame count";
        return false;
    }

    record.asset    = std::move(asset);
    record.manifest = manifest;
    return true;
}

std::string uniqueName(std::string requested,
                       const std::map<std::string, OverlayLibrary::AssetRecord, std::less<>>& records)
{
    if (requested.empty()) requested = "Overlay";
    std::set<std::string> names;
    for (const auto& [id, record] : records)
    {
        static_cast<void>(id);
        names.insert(lowerAscii(record.asset.name));
    }
    if (!names.contains(lowerAscii(requested))) return requested;
    for (unsigned suffix = 2; suffix < 10000; ++suffix)
    {
        const std::string candidate = requested + " (" + std::to_string(suffix) + ")";
        if (!names.contains(lowerAscii(candidate))) return candidate;
    }
    return requested + " (copy)";
}

} // namespace

const OverlayVariant* OverlayAsset::variant(OverlayAspect aspect) const
{
    const auto& value = variants[aspectIndex(aspect)];
    return value ? &*value : nullptr;
}

OverlayVariant* OverlayAsset::variant(OverlayAspect aspect)
{
    auto& value = variants[aspectIndex(aspect)];
    return value ? &*value : nullptr;
}

OverlayLibrary::OverlayLibrary()
    : OverlayLibrary(userDataDirectory() / "overlays")
{
}

OverlayLibrary::OverlayLibrary(fs::path rootDirectory)
    : rootDirectory_(std::move(rootDirectory))
{
}

bool OverlayLibrary::scan(std::string& error)
{
    records_.clear();
    warnings_.clear();
    if (!ensureLibraryDirectories(rootDirectory_, error)) return false;

    std::error_code ec;
    fs::directory_iterator input(rootDirectory_, ec);
    const fs::directory_iterator end;
    while (!ec && input != end)
    {
        const fs::directory_entry entry = *input;
        const bool directory = entry.is_directory(ec);
        if (ec) break;
        const std::string filename = entry.path().filename().string();
        if (!directory || filename.empty() || filename[0] == '.')
        {
            input.increment(ec);
            continue;
        }

        std::string json;
        std::string warning;
        Manifest manifest;
        AssetRecord record;
        if (!readText(entry.path() / "manifest.json", json, warning) ||
            !parseManifest(json, manifest, warning) ||
            !manifestToRecord(entry.path(), manifest, record, warning))
        {
            warnings_.push_back({entry.path(), warning});
            input.increment(ec);
            continue;
        }
        records_.emplace(record.asset.id, std::move(record));
        input.increment(ec);
    }
    if (ec)
    {
        error = "Could not scan overlay library: " + ec.message();
        return false;
    }
    return true;
}

std::vector<OverlayAsset> OverlayLibrary::assets() const
{
    std::vector<OverlayAsset> result;
    result.reserve(records_.size());
    for (const auto& [id, record] : records_)
    {
        static_cast<void>(id);
        result.push_back(record.asset);
    }
    return result;
}

const OverlayAsset* OverlayLibrary::find(std::string_view id) const
{
    const auto found = records_.find(id);
    return found == records_.end() ? nullptr : &found->second.asset;
}

bool OverlayLibrary::copyAsset(std::string_view id, OverlayAsset& out) const
{
    const OverlayAsset* asset = find(id);
    if (!asset) return false;
    out = *asset;
    return true;
}

bool OverlayLibrary::importAsset(const OverlayImportRequest& request,
                                 OverlayAsset& imported, std::string& error,
                                 OverlayImportControl* control)
{
    if (control) control->progress.store(0.02f, std::memory_order_release);
    if (request.sources.empty() || request.sources.size() > 2)
    {
        error = "Choose one or two overlay variants";
        return false;
    }
    if (!ensureLibraryDirectories(rootDirectory_, error)) return false;

    OverlayKind kind = OverlayKind::Still;
    bool kindKnown = false;
    for (const auto& source : request.sources)
    {
        if (importCancelled(control, error)) return false;
        OverlayKind sourceKind = OverlayKind::Still;
        std::vector<fs::path> ignored;
        if (!collectInputFrames(source, sourceKind, ignored, error)) return false;
        if (kindKnown && sourceKind != kind)
        {
            error = "Both overlay variants must use the same media type";
            return false;
        }
        kind = sourceKind;
        kindKnown = true;
    }

    Manifest manifest;
    do
    {
        manifest.id = randomId("ovl_");
    } while (records_.contains(manifest.id) || fs::exists(rootDirectory_ / manifest.id));
    manifest.name = uniqueName(request.name, records_);
    manifest.kind = kind;

    const std::string operation = randomId("import_");
    const fs::path stage = rootDirectory_ / ".staging" / operation / manifest.id;
    std::error_code ec;
    fs::create_directories(stage, ec);
    if (ec)
    {
        error = "Could not create overlay staging directory: " + ec.message();
        return false;
    }

    bool success = true;
    for (std::size_t sourceIndex = 0; sourceIndex < request.sources.size(); ++sourceIndex)
    {
        const auto& source = request.sources[sourceIndex];
        PreparedVariant prepared;
        const float span = 0.78f / static_cast<float>(request.sources.size());
        if (!prepareVariant(source, stage, kind, prepared, error, control,
                            0.08f + span * static_cast<float>(sourceIndex), span))
        {
            success = false;
            break;
        }
        manifest.variants.push_back(prepared.manifest);
    }
    if (success && manifest.variants.size() == 2 &&
        manifest.variants[0].aspect == manifest.variants[1].aspect)
    {
        error = "An overlay can contain only one variant of each format";
        success = false;
    }
    if (success && kind == OverlayKind::PngSequence && manifest.variants.size() == 2 &&
        manifest.variants[0].frameCount != manifest.variants[1].frameCount)
    {
        error = "Both PNG sequence variants need the same frame count";
        success = false;
    }
    if (success)
    {
        if (importCancelled(control, error)) success = false;
    }
    if (success)
    {
        if (control) control->progress.store(0.90f, std::memory_order_release);
        success = writeText(stage / "manifest.json", serializeManifest(manifest), error);
    }
    if (success)
    {
        if (importCancelled(control, error)) success = false;
    }
    if (success)
    {
        fs::rename(stage, rootDirectory_ / manifest.id, ec);
        if (ec)
        {
            error = "Could not publish overlay asset: " + ec.message();
            success = false;
        }
    }
    if (!success)
    {
        std::error_code cleanup;
        fs::remove_all(rootDirectory_ / ".staging" / operation, cleanup);
        return false;
    }
    fs::remove(rootDirectory_ / ".staging" / operation, ec);

    AssetRecord record;
    if (!manifestToRecord(rootDirectory_ / manifest.id, manifest, record, error)) return false;
    imported = record.asset;
    records_[record.asset.id] = std::move(record);
    if (control) control->progress.store(1.0f, std::memory_order_release);
    return true;
}

bool OverlayLibrary::replaceVariant(std::string_view assetId,
                                    const OverlayImportSource& source,
                                    OverlayAsset& updated, std::string& error,
                                    OverlayImportControl* control)
{
    if (control) control->progress.store(0.02f, std::memory_order_release);
    const auto found = records_.find(assetId);
    if (found == records_.end())
    {
        error = "Overlay asset not found";
        return false;
    }
    if (source.inferAspect ||
        (source.aspect != OverlayAspect::Landscape16x9 &&
         source.aspect != OverlayAspect::Portrait9x16))
    {
        error = "Unknown overlay format";
        return false;
    }

    const std::string operation = randomId("variant_");
    const fs::path stageRoot = rootDirectory_ / ".staging" / operation;
    std::error_code ec;
    fs::create_directories(stageRoot, ec);
    if (ec)
    {
        error = "Could not create overlay staging directory: " + ec.message();
        return false;
    }

    PreparedVariant prepared;
    if (!prepareVariant(source, stageRoot, found->second.asset.kind, prepared, error,
                        control, 0.08f, 0.78f))
    {
        fs::remove_all(stageRoot, ec);
        return false;
    }
    if (found->second.asset.kind == OverlayKind::PngSequence)
    {
        const OverlayAspect otherAspect = source.aspect == OverlayAspect::Landscape16x9
            ? OverlayAspect::Portrait9x16 : OverlayAspect::Landscape16x9;
        if (const OverlayVariant* other = found->second.asset.variant(otherAspect);
            other && other->frames.size() != prepared.manifest.frameCount)
        {
            error = "Both PNG sequence variants need the same frame count";
            fs::remove_all(stageRoot, ec);
            return false;
        }
    }

    if (importCancelled(control, error))
    {
        fs::remove_all(stageRoot, ec);
        return false;
    }

    const fs::path assetRoot = rootDirectory_ / found->second.asset.id;
    fs::rename(prepared.contentDirectory, assetRoot / prepared.manifest.contentId, ec);
    if (ec)
    {
        error = "Could not publish overlay variant: " + ec.message();
        fs::remove_all(stageRoot, ec);
        return false;
    }

    Manifest next = found->second.manifest;
    bool replaced = false;
    for (ManifestVariant& variant : next.variants)
    {
        if (variant.aspect == source.aspect)
        {
            variant = prepared.manifest;
            replaced = true;
            break;
        }
    }
    if (!replaced) next.variants.push_back(prepared.manifest);

    // Prove that the complete next record resolves before publishing its
    // manifest. The old manifest remains authoritative on every failure.
    AssetRecord nextRecord;
    if (!manifestToRecord(assetRoot, next, nextRecord, error))
    {
        fs::remove_all(assetRoot / prepared.manifest.contentId, ec);
        fs::remove_all(stageRoot, ec);
        return false;
    }

    const fs::path manifestNext = assetRoot / "manifest.next.json";
    if (control) control->progress.store(0.90f, std::memory_order_release);
    if (!writeText(manifestNext, serializeManifest(next), error) ||
        !replaceOverlayFileAtomically(manifestNext, assetRoot / "manifest.json", error))
    {
        fs::remove_all(assetRoot / prepared.manifest.contentId, ec);
        fs::remove_all(stageRoot, ec);
        return false;
    }

    updated = nextRecord.asset;
    found->second = std::move(nextRecord);
    fs::remove_all(stageRoot, ec);
    if (control) control->progress.store(1.0f, std::memory_order_release);
    return true;
}

bool OverlayLibrary::removeAsset(std::string_view assetId, std::string& error)
{
    const auto found = records_.find(assetId);
    if (found == records_.end())
    {
        error = "Overlay asset not found";
        return false;
    }
    if (!ensureLibraryDirectories(rootDirectory_, error)) return false;

    const fs::path source = rootDirectory_ / found->second.asset.id;
    fs::path destination = rootDirectory_ / ".trash" /
                           (found->second.asset.id + "_" + randomId("trash_"));
    std::error_code ec;
    fs::rename(source, destination, ec);
    if (ec)
    {
        error = "Could not move overlay to trash: " + ec.message();
        return false;
    }
    records_.erase(found);
    return true;
}

} // namespace atemfx
