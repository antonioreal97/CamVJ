#pragma once

#include <filesystem>
#include <string>

namespace atemfx {

// Writable per-user directory for venue boot and operator presets.
// macOS: ~/Library/Application Support/CamVJ
// Windows: the Unicode %APPDATA% path + CamVJ
// Falls back to "./CamVJ-userdata" when the platform path cannot be resolved.
std::filesystem::path userDataDirectory();

// Ensures the directory exists. Returns false and writes `error` on failure.
bool ensureUserDataDirectory(std::string& error);

} // namespace atemfx
