#include "core/Paths.h"

#include <cstdlib>
#include <iterator>
#include <system_error>

#include "core/Log.h"

#if defined(__APPLE__)
#include <pwd.h>
#include <unistd.h>
#elif defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace atemfx {
namespace {

std::filesystem::path fallbackUserData()
{
    return std::filesystem::path("CamVJ-userdata");
}

} // namespace

std::filesystem::path userDataDirectory()
{
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0')
    {
        if (const passwd* pw = getpwuid(getuid()))
        {
            home = pw->pw_dir;
        }
    }
    if (home && home[0] != '\0')
    {
        return std::filesystem::path(home) / "Library" / "Application Support" / "CamVJ";
    }
#elif defined(_WIN32)
    wchar_t appData[32768] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData,
                                                  static_cast<DWORD>(std::size(appData)));
    if (length > 0 && length < std::size(appData))
    {
        return std::filesystem::path(appData) / "CamVJ";
    }
#else
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
    {
        return std::filesystem::path(home) / ".local" / "share" / "CamVJ";
    }
#endif
    return fallbackUserData();
}

bool ensureUserDataDirectory(std::string& error)
{
    const std::filesystem::path root = userDataDirectory();
    std::error_code             ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        error = "Could not create " + root.string() + ": " + ec.message();
        ATEMFX_LOG_ERROR("%s", error.c_str());
        return false;
    }

    const std::filesystem::path presets = root / "presets";
    std::filesystem::create_directories(presets, ec);
    if (ec)
    {
        error = "Could not create " + presets.string() + ": " + ec.message();
        ATEMFX_LOG_ERROR("%s", error.c_str());
        return false;
    }
    return true;
}

} // namespace atemfx
