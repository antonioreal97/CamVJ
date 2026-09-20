#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "presets/scene_preset.h"

namespace atemfx {

struct PresetListEntry
{
    std::string id;
    std::string name;
    bool        factory = false;
};

// Built-in looks that ship with the binary. They are not written to disk.
std::vector<ScenePreset> factoryPresets();

// Operator presets live in Application Support/CamVJ/presets/*.json.
std::vector<PresetListEntry> listUserPresets(std::string& error);
bool loadUserPreset(const std::string& id, ScenePreset& out, std::string& error);
bool saveUserPreset(const ScenePreset& preset, std::string& error);
bool findFactoryPreset(const std::string& id, ScenePreset& out);

// Stable file id from a display name (lowercase, [a-z0-9_]).
std::string presetIdFromName(std::string_view name);

} // namespace atemfx
