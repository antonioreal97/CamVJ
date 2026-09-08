#include "gpu/ShaderPaths.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "core/Log.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace atemfx {

namespace {

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
    auto            path = std::filesystem::canonical(buffer.data(), ec);
    if (ec)
    {
        return std::filesystem::path(buffer.data()).parent_path();
    }
    return path.parent_path();
#else
    return {};
#endif
}

bool accept(const std::filesystem::path& candidate, const std::string& marker)
{
    std::error_code ec;
    return std::filesystem::exists(candidate / marker, ec);
}

} // namespace

std::filesystem::path resolveShaderDirectory(const std::string& backendSubdirectory,
                                             const std::string& marker)
{
    if (const char* overridePath = std::getenv("ATEMFX_SHADER_DIR"))
    {
        const std::filesystem::path candidate = std::filesystem::path(overridePath) / backendSubdirectory;
        if (accept(candidate, marker))
        {
            return candidate;
        }
        ATEMFX_LOG_WARN("ATEMFX_SHADER_DIR is set but has no %s/%s: %s",
                        backendSubdirectory.c_str(), marker.c_str(), overridePath);
    }

    const std::filesystem::path executable = executableDirectory();

    // Inside a macOS application bundle the executable sits in
    // Contents/MacOS and the resources in Contents/Resources, which the
    // walk-up below would never find.
    if (!executable.empty())
    {
        const std::filesystem::path bundled =
            executable.parent_path() / "Resources" / "shaders" / backendSubdirectory;
        if (accept(bundled, marker))
        {
            return bundled;
        }
    }

    std::filesystem::path directory = executable;
    for (int depth = 0; depth < 6 && !directory.empty(); ++depth)
    {
        const std::filesystem::path candidate = directory / "shaders" / backendSubdirectory;
        if (accept(candidate, marker))
        {
            return candidate;
        }
        if (!directory.has_parent_path() || directory.parent_path() == directory)
        {
            break;
        }
        directory = directory.parent_path();
    }

#ifdef ATEMFX_SHADER_SOURCE_DIR
    {
        const std::filesystem::path candidate =
            std::filesystem::path(ATEMFX_SHADER_SOURCE_DIR) / backendSubdirectory;
        if (accept(candidate, marker))
        {
            return candidate;
        }
    }
#endif

    return {};
}

bool readTextFile(const std::filesystem::path& path, std::string& out, std::string& error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open " + path.string();
        return false;
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    out = contents.str();
    return true;
}

} // namespace atemfx
