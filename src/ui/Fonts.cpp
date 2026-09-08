#include "ui/Fonts.h"

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "core/Log.h"
#include "imgui.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace atemfx {
namespace ui {

namespace {

// Sizes are in ImGui units, not pixels: loadFonts multiplies them out. 14 and
// 12 read at arm's length on a rack-mounted laptop, which is the posture this
// UI is actually used in.
constexpr float kUiSize   = 14.0f;
constexpr float kMonoSize = 12.0f;

FontSet g_fonts;

std::filesystem::path executableDirectory()
{
#if defined(_WIN32)
    wchar_t     buffer[MAX_PATH] = {};
    const DWORD length           = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return {};
    }
    std::error_code ec;
    const auto      path = std::filesystem::canonical(buffer.data(), ec);
    return ec ? std::filesystem::path(buffer.data()).parent_path() : path.parent_path();
#else
    return {};
#endif
}

// Where the brand faces will live once they are licensed and committed. The
// directory is allowed not to exist: the cascade simply moves on to the
// system fonts.
const std::filesystem::path& brandFontDirectory()
{
    static const std::filesystem::path directory = [] {
        const std::filesystem::path executable = executableDirectory();
        std::error_code              ec;

        if (!executable.empty())
        {
            // Inside a macOS bundle the executable sits in Contents/MacOS.
            const std::filesystem::path bundled =
                executable.parent_path() / "Resources" / "fonts";
            if (std::filesystem::is_directory(bundled, ec))
            {
                return bundled;
            }
        }

        std::filesystem::path walk = executable;
        for (int depth = 0; depth < 6 && !walk.empty(); ++depth)
        {
            const std::filesystem::path candidate = walk / "assets" / "fonts";
            if (std::filesystem::is_directory(candidate, ec))
            {
                return candidate;
            }
            if (!walk.has_parent_path() || walk.parent_path() == walk)
            {
                break;
            }
            walk = walk.parent_path();
        }

#ifdef ATEMFX_ASSET_SOURCE_DIR
        const std::filesystem::path source =
            std::filesystem::path(ATEMFX_ASSET_SOURCE_DIR) / "fonts";
        if (std::filesystem::is_directory(source, ec))
        {
            return source;
        }
#endif
        return std::filesystem::path{};
    }();
    return directory;
}

bool readable(const std::filesystem::path& path)
{
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

// ImGui asserts rather than returning null when a font file is missing, so
// every candidate is checked before it is handed over.
ImFont* addFace(const std::filesystem::path& path, float sizePixels)
{
    if (!readable(path))
    {
        return nullptr;
    }
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), sizePixels);
    if (font)
    {
        ATEMFX_LOG_INFO("UI font: %s at %.0f px", path.string().c_str(), sizePixels);
    }
    return font;
}

// One role's cascade: an explicit override, then the brand directory, then
// whatever the platform ships. Returns null only if every entry missed, which
// leaves the caller to fall back to ImGui's built-in font.
ImFont* loadFace(const char*                        overrideVariable,
                 std::initializer_list<const char*> brandFiles,
                 std::initializer_list<const char*> systemPaths,
                 float                              sizePixels)
{
    if (const char* explicitPath = std::getenv(overrideVariable))
    {
        if (ImFont* font = addFace(explicitPath, sizePixels))
        {
            return font;
        }
        ATEMFX_LOG_WARN("%s is set but is not a readable font file: %s",
                        overrideVariable, explicitPath);
    }

    const std::filesystem::path& brandDirectory = brandFontDirectory();
    if (!brandDirectory.empty())
    {
        for (const char* file : brandFiles)
        {
            if (ImFont* font = addFace(brandDirectory / file, sizePixels))
            {
                return font;
            }
        }
    }

    for (const char* path : systemPaths)
    {
        if (ImFont* font = addFace(path, sizePixels))
        {
            return font;
        }
    }

    return nullptr;
}

} // namespace

void loadFonts(const DisplayScale& scale)
{
    ImGuiIO& io = ImGui::GetIO();

    // Re-callable: the window can move to a display of a different density,
    // and the atlas has to be rasterised again to stay sharp there.
    io.Fonts->Clear();
    g_fonts = FontSet{};

    // The atlas is rasterised at the size the glyphs will actually occupy on
    // the panel, and FontGlobalScale takes them back down to ImGui units. On
    // Retina that is a 2x atlas drawn at half scale — the difference between
    // crisp text and an upscaled bitmap.
    const float density = scale.pixelDensity > 0.0f ? scale.pixelDensity : 1.0f;
    const float content = scale.contentScale > 0.0f ? scale.contentScale : 1.0f;
    const float atlas   = density * content;

    ATEMFX_LOG_INFO("UI scale: %.2fx density, %.2fx content", density, content);

    g_fonts.ui = loadFace("ATEMFX_FONT_UI",
                          {"BigShouldersDisplay-Regular.ttf", "CamVJ-UI.ttf"},
#if defined(_WIN32)
                          {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"},
#elif defined(__APPLE__)
                          {"/System/Library/Fonts/SFNS.ttf",
                           "/System/Library/Fonts/HelveticaNeue.ttc",
                           "/System/Library/Fonts/Helvetica.ttc"},
#else
                          {},
#endif
                          kUiSize * atlas);

    if (!g_fonts.ui)
    {
        ATEMFX_LOG_WARN("No UI font found; falling back to the built-in bitmap font");
        ImFontConfig config;
        config.SizePixels = kUiSize * atlas;
        g_fonts.ui        = io.Fonts->AddFontDefault(&config);
    }

    g_fonts.mono = loadFace("ATEMFX_FONT_MONO",
                            {"GeistMono-Regular.ttf", "CamVJ-Mono.ttf"},
#if defined(_WIN32)
                            {"C:\\Windows\\Fonts\\consola.ttf",
                             "C:\\Windows\\Fonts\\cour.ttf"},
#elif defined(__APPLE__)
                            {"/System/Library/Fonts/SFNSMono.ttf",
                             "/System/Library/Fonts/Menlo.ttc",
                             "/System/Library/Fonts/Monaco.ttf"},
#else
                            {},
#endif
                            kMonoSize * atlas);

    io.FontDefault     = g_fonts.ui;
    io.FontGlobalScale = content / atlas;
}

const FontSet& fonts()
{
    return g_fonts;
}

void pushMono()
{
    if (g_fonts.mono)
    {
        ImGui::PushFont(g_fonts.mono);
    }
}

void popMono()
{
    if (g_fonts.mono)
    {
        ImGui::PopFont();
    }
}

} // namespace ui
} // namespace atemfx
