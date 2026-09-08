#pragma once

#include <filesystem>
#include <string>

namespace atemfx {

// Locates the shaders directory, which holds one subdirectory per backend
// (`hlsl`, `metal`). Search order:
//
//   1. ATEMFX_SHADER_DIR, if set;
//   2. a `shaders` directory beside the executable, or up to five levels above
//      it, which covers running straight out of a build tree;
//   3. the source tree, baked in at configure time.
//
// Returns an empty path when nothing is found. `marker` is a file that must
// exist inside the candidate directory for it to be accepted.
std::filesystem::path resolveShaderDirectory(const std::string& backendSubdirectory,
                                             const std::string& marker);

// Reads a whole text file. Returns false and leaves `out` untouched on failure.
bool readTextFile(const std::filesystem::path& path, std::string& out, std::string& error);

} // namespace atemfx
