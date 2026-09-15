#pragma once

#include <string>
#include <string_view>

namespace atemfx {

// Venue boot: last source, last output display, and the Auto Frame portrait
// flag. Separate from scene presets so a look recall cannot silently re-route
// the wall.
struct BootState
{
    static constexpr int kVersion = 1;

    std::string sourceId;
    std::string outputDisplayId;
    bool        portrait = false;
};

std::string serializeBoot(const BootState& state);
bool        parseBoot(std::string_view json, BootState& out, std::string& error);

bool loadBootState(BootState& out, std::string& error);
bool saveBootState(const BootState& state, std::string& error);

} // namespace atemfx
