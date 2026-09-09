#include "ui/Panels.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "effects/EffectChain.h"
#include "effects/EffectRegistry.h"
#include "imgui.h"
#include "ui/Inspector.h"
#include "ui/Theme.h"

namespace atemfx {

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

    const bool operationLocked = state.operationLocked && *state.operationLocked;
    ImGui::BeginDisabled(operationLocked);
    // The only way to grow the chain, so it reads as the panel's action rather
    // than as one more grey control in a column of them.
    if (effectsOpen && theme::actionButton("Add effect", theme::ButtonAccent::Cyan,
                                           ImVec2(-1.0f, 0.0f)))
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
                    // Straight into the inspector: an effect is added to be
                    // set up, and the panel that sets it up is the one under
                    // the preview.
                    ui::inspectEffect(&chain->at(chain->size() - 1));
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
    ImGui::EndDisabled();

    const std::size_t count          = chain->size();
    Effect*           selectedEffect = ui::inspectedEffect();

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
        const bool selected = (&effect == selectedEffect);
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
        const ImGuiStyle& style = ImGui::GetStyle();
        // Square controls the height of the row: the old "^ v x" were text-sized
        // targets on a control surface that gets used mid-show. The extra gap
        // before the delete keeps a reorder misclick from removing the effect.
        const float glyph     = ImGui::GetFrameHeight();
        const float deleteGap = theme::scaled(14.0f);
        const float controls  = glyph * 3.0f + style.ItemSpacing.x * 3.0f + deleteGap;
        const float rowWidth =
            std::max(theme::scaled(40.0f), ImGui::GetContentRegionAvail().x - controls);

        if (!enabled)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Selectable(label, selected, 0, ImVec2(rowWidth, glyph)))
        {
            // Clicking the same row twice puts the numbers back: the panel
            // under the preview is one place, and the operator needs a way
            // out of it that is where they already are.
            if (selected)
            {
                ui::closeInspector();
            }
            else
            {
                ui::inspectEffect(&effect);
            }
        }
        ImGui::PopStyleVar();
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
            ImGui::SetTooltip("%s\n%zu parameter loops enabled\n%s",
                              effect.descriptor().displayName.c_str(), loopCount,
                              selected ? "Click to close its parameters"
                                       : "Click to edit its parameters below the preview");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(operationLocked || i == 0);
        if (theme::glyphButton("##up", theme::Glyph::Up, glyph, "Move earlier in the chain"))
        {
            changedIndex = i;
            moveDelta = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(operationLocked || i + 1 == count);
        if (theme::glyphButton("##down", theme::Glyph::Down, glyph, "Move later in the chain"))
        {
            changedIndex = i;
            moveDelta = 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, style.ItemSpacing.x + deleteGap);
        ImGui::BeginDisabled(operationLocked);
        if (theme::glyphButton("##remove", theme::Glyph::Close, glyph, "Remove from the chain",
                               theme::ButtonAccent::Magenta))
        {
            changedIndex = i;
            removeEffect = true;
        }
        ImGui::EndDisabled();

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
            // The panel under the preview goes back to the stats strip rather
            // than to a neighbour: nobody asked to edit the effect that
            // happened to sit next to the one they deleted.
            if (&chain->at(changedIndex) == selectedEffect)
            {
                ui::closeInspector();
            }
            chain->remove(changedIndex);
        }
        else
        {
            chain->move(changedIndex, moveDelta);
        }
    }

    if (effectsOpen && count > 0 && !ui::inspectedEffect())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Click an effect to edit it below the preview");
    }
}

} // namespace atemfx
