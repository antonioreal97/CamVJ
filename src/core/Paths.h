#pragma once

#include <filesystem>
#include <string>

namespace atemfx {

// Writable per-user directory for venue boot and operator presets.
// macOS: ~/Library/Application Support/CamVJ
// Windows: the Unicode %APPDATA% path + CamVJ
// Falls back to "./CamVJ-userdata" when the platform path cannot be resolved.
std::filesystem::path userDataDirectory();

// Filesystem paths are native wide strings on Windows. Convert deliberately
// to UTF-8 before showing a name in ImGui or writing it into a JSON manifest.
std::string pathToUtf8(const std::filesystem::path& path);

// Where PROGRAM recordings go unless --record-dir says otherwise.
// macOS: ~/Movies/CamVJ
// Windows: %USERPROFILE%\Videos\CamVJ
// Falls back to "./CamVJ-recordings". Not created here: the recorder creates
// it when a take starts, so an operator who never records gets no folder.
std::filesystem::path recordingsDirectory();

// Ensures the directory exists. Returns false and writes `error` on failure.
bool ensureUserDataDirectory(std::string& error);

} // namespace atemfx
