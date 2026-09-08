#include "ui/Panels.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "effects/EffectChain.h"
#include "effects/EffectRegistry.h"
#include "imgui.h"
#include "ui/Theme.h"

namespace atemfx {

namespace {

Effect* g_selectedEffect = nullptr;

} // namespace

void drawEffectsPanel(UiFrameState& state)
{
    EffectChain* chain = state.chain;
    if (!chain)
    {
        return;
    }

    char meta[16];
    std::snprintf(meta, sizeof(meta), "%zu", chain->size());
    const bool effectsOpen =
        theme::drawPanelHeader("EFFECTS", theme::kSplitCyanU32, theme::PanelSection::Effects, meta);

    if (effectsOpen && ImGui::Button("Add effect"))
    {
        ImGui::OpenPopup("add_effect_popup");
    }

    if (effectsOpen && ImGui::BeginPopup("add_effect_popup"))
    {
        for (const EffectRegistry::Entry& entry : EffectRegistry::instance().entries())
        {
            if (ImGui::MenuItem(entry.descriptor.displayName.c_str()))
            {
                std::string error;
                if (state.effectContext &&
                    chain->addByType(entry.descriptor.typeId, *state.effectContext, error))
                {
                    g_selectedEffect = &chain->at(chain->size() - 1);
                    if (state.status)
                    {
                        *state.status = "Added " + entry.descriptor.displayName;
                    }
                }
                else if (state.status)
                {
                    *state.status = "Add failed: " + error;
                }
            }
            if (ImGui::IsItemHovered() && !entry.descriptor.description.empty())
            {
                ImGui::SetTooltip("%s", entry.descriptor.description.c_str());
            }
        }
        ImGui::EndPopup();
    }

    const std::size_t count = chain->size();
    bool selectionExists = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        selectionExists |= &chain->at(i) == g_selectedEffect;
    }
    if (!selectionExists)
    {
        g_selectedEffect = count > 0 ? &chain->at(0) : nullptr;
    }

    std::size_t changedIndex = count;
    int moveDelta = 0;
    bool removeEffect = false;

    if (effectsOpen)
    {
        ImGui::Spacing();
        if (count == 0)
        {
            ImGui::TextDisabled("Chain is empty");
        }
    }

    for (std::size_t i = 0; i < count && effectsOpen; ++i)
    {
        Effect& effect = chain->at(i);
        ImGui::PushID(&effect);

        bool enabled = effect.enabled();
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            effect.setEnabled(enabled);
        }

        ImGui::SameLine();
        const bool selected = (&effect == g_selectedEffect);
        const std::size_t loopCount = effect.parameters().activeAutomationCount();
        char label[256];
        if (loopCount > 0)
        {
            std::snprintf(label, sizeof(label), "%s   %zu loop%s###node",
                          effect.descriptor().displayName.c_str(), loopCount,
                          loopCount == 1 ? "" : "s");
        }
        else
        {
            std::snprintf(label, sizeof(label), "%s###node", effect.descriptor().displayName.c_str());
        }
        const float buttonWidth = ImGui::CalcTextSize("^").x + 2.0f * ImGui::GetStyle().FramePadding.x;
        const float rowWidth = std::max(theme::scaled(40.0f), ImGui::GetContentRegionAvail().x -
                                                3.0f * (buttonWidth + ImGui::GetStyle().ItemSpacing.x));

        if (!enabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
        }
        if (ImGui::Selectable(label, selected, 0, ImVec2(rowWidth, 0.0f)))
        {
            g_selectedEffect = &effect;
        }
        if (!enabled)
        {
            ImGui::PopStyleColor();
        }
        if (selected)
        {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, ImVec2(min.x + theme::scaled(2.0f), max.y), theme::kSplitCyanU32);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s\n%zu parameter loops enabled",
                              effect.descriptor().displayName.c_str(), loopCount);
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("^"))
        {
            changedIndex = i;
            moveDelta = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 == count);
        if (ImGui::SmallButton("v"))
        {
            changedIndex = i;
            moveDelta = 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            changedIndex = i;
            removeEffect = true;
        }

        if (!effect.lastError().empty())
        {
            ImGui::TextColored(theme::splitMagenta, "shader error");
        }

        ImGui::PopID();
    }

    if (changedIndex < count)
    {
        if (removeEffect)
        {
            const bool removedSelection = g_selectedEffect == &chain->at(changedIndex);
            chain->remove(changedIndex);
            if (removedSelection)
            {
                g_selectedEffect = chain->size() > 0 ? &chain->at(std::min(changedIndex, chain->size() - 1)) : nullptr;
            }
        }
        else
        {
            chain->move(changedIndex, moveDelta);
        }
    }

    ImGui::Spacing();
    const bool parametersOpen = theme::drawPanelHeader(
        "PARAMETERS", theme::kRackGreyU32, theme::PanelSection::Parameters,
        g_selectedEffect ? g_selectedEffect->descriptor().displayName.c_str() : nullptr);

    if (!parametersOpen)
    {
        return;
    }

    if (g_selectedEffect)
    {
        Effect& effect = *g_selectedEffect;

        const std::size_t loopCount = effect.parameters().activeAutomationCount();
        if (loopCount > 0)
        {
            ImGui::TextDisabled("%zu parameter loop%s enabled", loopCount, loopCount == 1 ? "" : "s");
        }
        if (!effect.lastError().empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
            ImGui::TextWrapped("%s", effect.lastError().c_str());
            ImGui::PopStyleColor();
        }

        ImGui::PushID(&effect);
        drawParameters(effect.parameters(), effect.descriptor().typeId.c_str(), true);
        ImGui::PopID();
    }
    else
    {
        ImGui::TextDisabled("Select an effect");
    }
}

} // namespace atemfx
