#include "presets/preset_store.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/Log.h"
#include "core/Paths.h"

namespace atemfx {
namespace {

SceneParamState param(std::string id, float value)
{
    SceneParamState p;
    p.id    = std::move(id);
    p.value = value;
    return p;
}

SceneParamState looped(std::string id, float value, AutomationWaveform wave,
                       double period, float minValue, float maxValue)
{
    SceneParamState p = param(std::move(id), value);
    p.automationEnabled = true;
    p.waveform          = wave;
    p.periodSeconds     = period;
    p.automationMin     = minValue;
    p.automationMax     = maxValue;
    return p;
}

SceneEffectState effect(std::string typeId, bool enabled,
                        std::vector<SceneParamState> params = {})
{
    SceneEffectState node;
    node.typeId  = std::move(typeId);
    node.enabled = enabled;
    node.params  = std::move(params);
    return node;
}

// Default chain skeleton: Auto Frame on, everything else off, then overlays
// enable and retune specific nodes for each look.
ScenePreset baseLook(std::string id, std::string name)
{
    ScenePreset preset;
    preset.id      = std::move(id);
    preset.name    = std::move(name);
    preset.factory = true;
    preset.program = ProgramMode::Effects;
    preset.effects = {
        effect("auto_frame", true,
               {param("follow", 1.0f), param("portrait", 0.0f), param("size", 0.55f),
                param("headroom", 0.12f), param("offset_x", 0.0f), param("dead_zone", 0.10f),
                param("smoothing", 0.60f), param("max_speed", 0.50f), param("hold", 2.0f),
                param("return", 3.0f), param("max_zoom", 1.8f), param("zoom", 1.0f),
                param("center_x", 0.5f), param("center_y", 0.5f)}),
        effect("passthrough", false),
        effect("rgb_split", false, {param("amount", 0.15f), param("angle", 0.0f)}),
        effect("pixelate", false, {param("size", 12.0f)}),
        effect("fm_raster", false),
        effect("subpixel", false),
        effect("shutter", false),
        effect("frame_delay", false),
        effect("mirror", false, {param("mode", 0.0f), param("pivot", 0.5f)}),
        effect("vhs", false),
        effect("crt", false),
    };
    return preset;
}

SceneEffectState* findNode(ScenePreset& preset, std::string_view typeId)
{
    for (SceneEffectState& node : preset.effects)
    {
        if (node.typeId == typeId) return &node;
    }
    return nullptr;
}

void enable(ScenePreset& preset, std::string_view typeId, std::vector<SceneParamState> params = {})
{
    if (SceneEffectState* node = findNode(preset, typeId))
    {
        node->enabled = true;
        if (!params.empty()) node->params = std::move(params);
    }
}

bool isSafePresetId(std::string_view id)
{
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

std::filesystem::path presetsDirectory()
{
    return userDataDirectory() / "presets";
}

} // namespace

std::string presetIdFromName(std::string_view name)
{
    std::string slug;
    slug.reserve(name.size());
    for (char c : name)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc))
        {
            slug.push_back(static_cast<char>(std::tolower(uc)));
        }
        else if (c == ' ' || c == '-' || c == '_' || c == '.')
        {
            if (!slug.empty() && slug.back() != '_') slug.push_back('_');
        }
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    if (slug.empty()) slug = "look";
    if (slug.size() > 48) slug.resize(48);
    return slug;
}

std::vector<ScenePreset> factoryPresets()
{
    std::vector<ScenePreset> looks;

    {
        ScenePreset look = baseLook("clean_frame", "Clean Frame");
        look.program = ProgramMode::Clean;
        looks.push_back(std::move(look));
    }
    {
        ScenePreset look = baseLook("rgb_pulse", "RGB Pulse");
        enable(look, "rgb_split",
               {looped("amount", 0.20f, AutomationWaveform::Sine, 3.0, 0.05f, 0.35f),
                param("angle", 45.0f), param("radial", 0.0f)});
        looks.push_back(std::move(look));
    }
    {
        ScenePreset look = baseLook("pixel_stage", "Pixel Stage");
        enable(look, "pixelate",
               {looped("size", 16.0f, AutomationWaveform::Triangle, 5.0, 8.0f, 28.0f)});
        enable(look, "rgb_split", {param("amount", 0.08f), param("angle", 0.0f)});
        looks.push_back(std::move(look));
    }
    {
        ScenePreset look = baseLook("shutter_trail", "Shutter Trail");
        enable(look, "shutter");
        looks.push_back(std::move(look));
    }
    {
        ScenePreset look = baseLook("echo_delay", "Echo Delay");
        enable(look, "frame_delay");
        looks.push_back(std::move(look));
    }
    {
        ScenePreset look = baseLook("vhs_crt", "VHS / CRT");
        enable(look, "vhs");
        enable(look, "crt");
        looks.push_back(std::move(look));
    }

    return looks;
}

bool findFactoryPreset(const std::string& id, ScenePreset& out)
{
    for (ScenePreset& preset : factoryPresets())
    {
        if (preset.id == id)
        {
            out = std::move(preset);
            return true;
        }
    }
    return false;
}

std::vector<PresetListEntry> listUserPresets(std::string& error)
{
    std::vector<PresetListEntry> entries;
    std::string                  ensureError;
    if (!ensureUserDataDirectory(ensureError))
    {
        error = ensureError;
        return entries;
    }

    std::error_code ec;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(presetsDirectory(), ec))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        ScenePreset preset;
        std::string loadError;
        if (!loadUserPreset(entry.path().stem().string(), preset, loadError))
        {
            ATEMFX_LOG_WARN("Skipping preset %s: %s", entry.path().filename().c_str(),
                            loadError.c_str());
            continue;
        }
        entries.push_back({preset.id, preset.name, false});
    }
    std::sort(entries.begin(), entries.end(),
              [](const PresetListEntry& a, const PresetListEntry& b) { return a.name < b.name; });
    return entries;
}

bool loadUserPreset(const std::string& id, ScenePreset& out, std::string& error)
{
    if (!isSafePresetId(id))
    {
        error = "Invalid preset id";
        return false;
    }
    const std::filesystem::path path = presetsDirectory() / (id + ".json");
    std::ifstream               file(path);
    if (!file)
    {
        error = "Preset not found: " + id;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!parseScene(text, out, error)) return false;
    out.factory = false;
    return true;
}

bool saveUserPreset(const ScenePreset& preset, std::string& error)
{
    if (!isSafePresetId(preset.id))
    {
        error = "Invalid preset id";
        return false;
    }
    if (preset.name.empty())
    {
        error = "Preset needs a name";
        return false;
    }
    if (!ensureUserDataDirectory(error)) return false;

    ScenePreset toWrite = preset;
    toWrite.factory     = false;
    const std::filesystem::path path = presetsDirectory() / (preset.id + ".json");
    const std::filesystem::path tmp  = path.string() + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Could not write preset";
            return false;
        }
        file << serializeScene(toWrite);
        if (!file)
        {
            error = "Failed writing preset";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        error = "Could not replace preset: " + ec.message();
        return false;
    }
    ATEMFX_LOG_INFO("Saved preset '%s' to %s", preset.name.c_str(), path.string().c_str());
    return true;
}

} // namespace atemfx
