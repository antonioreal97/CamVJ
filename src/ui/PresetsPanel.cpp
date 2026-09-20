#include "ui/Panels.h"

#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "presets/preset_store.h"
#include "ui/Theme.h"

namespace atemfx {
namespace {

bool drawPresetButton(const char* label, bool factory)
{
    const float width = theme::rowButtonWidth(3);
    return theme::actionButton(label,
                               factory ? theme::ButtonAccent::Cyan : theme::ButtonAccent::Neutral,
                               ImVec2(width, 0.0f));
}

} // namespace

void drawPresetsPanel(UiFrameState& state)
{
    const bool open =
        theme::drawPanelHeader("PRESETS", theme::kSplitCyanU32, theme::PanelSection::Presets);

    if (!open) return;

    const bool locked = state.operationLocked && *state.operationLocked;
    ImGui::BeginDisabled(locked);

    theme::drawGroupLabel("FACTORY");
    const std::vector<ScenePreset> factory = factoryPresets();
    for (std::size_t i = 0; i < factory.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (drawPresetButton(factory[i].name.c_str(), true) && state.recallPresetId)
        {
            *state.recallPresetId = factory[i].id;
        }
        ImGui::PopID();
        if ((i % 3) != 2 && i + 1 < factory.size())
        {
            ImGui::SameLine();
        }
    }

    ImGui::Spacing();
    theme::drawGroupLabel("USER");

    std::string listError;
    const std::vector<PresetListEntry> user = listUserPresets(listError);
    if (user.empty())
    {
        ImGui::TextDisabled("%s", listError.empty() ? "No saved looks yet" : listError.c_str());
    }
    for (std::size_t i = 0; i < user.size(); ++i)
    {
        ImGui::PushID(1000 + static_cast<int>(i));
        if (drawPresetButton(user[i].name.c_str(), false) && state.recallPresetId)
        {
            *state.recallPresetId = user[i].id;
        }
        ImGui::PopID();
        if ((i % 3) != 2 && i + 1 < user.size())
        {
            ImGui::SameLine();
        }
    }

    ImGui::Spacing();
    theme::drawGroupLabel("SAVE");
    static char nameBuf[64] = "My Look";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##preset_name", nameBuf, sizeof(nameBuf));
    if (theme::actionButton("Save current look", theme::ButtonAccent::Cyan,
                            ImVec2(-1.0f, 0.0f)))
    {
        if (state.savePresetName && state.requestPresetSave)
        {
            *state.savePresetName    = nameBuf;
            *state.requestPresetSave = true;
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Writes chain, Auto Frame and FX/Clean to Application Support");
    }

    ImGui::EndDisabled();
    if (locked)
    {
        ImGui::TextDisabled("Unlock Operation to recall or save");
    }
}

} // namespace atemfx
