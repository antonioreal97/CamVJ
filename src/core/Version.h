#pragma once

// The one version string, injected by CMake from project(AtemFx VERSION ...)
// so the binary can only ever report what the build system stamped on it.
// See docs/BUILD.md#versioning.
#if !defined(ATEMFX_VERSION)
// A build that bypasses our CMake (an IDE indexer, a hand-rolled compile) must
// still compile, and must not claim to be a release.
#define ATEMFX_VERSION "0.0.0-dev"
#endif

namespace atemfx {

constexpr const char* kVersion = ATEMFX_VERSION;

} // namespace atemfx
