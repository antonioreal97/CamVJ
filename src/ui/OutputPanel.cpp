#include "ui/Panels.h"

#include <cstdio>
#include <string>

#include "imgui.h"
#include "platform/Display.h"
#include "ui/Theme.h"

namespace atemfx {

namespace {

void formatDisplay(const DisplayInfo& display, char* buffer, std::size_t size)
{
    if (display.refreshHz > 0.0)
    {
        std::snprintf(buffer, size, "%s  %ux%u %.0f Hz%s",
                      display.name.c_str(),
                      display.width,
                      display.height,
                      display.refreshHz,
                      display.primary ? "  (primary)" : "");
    }
    else
    {
        std::snprintf(buffer, size, "%s  %ux%u%s",
                      display.name.c_str(),
                      display.width,
                      display.height,
                      display.primary ? "  (primary)" : "");
    }
}

} // namespace

void drawOutputPanel(UiFrameState& state)
{
    if (!theme::drawPanelHeader("OUTPUT",
                                state.outputActive ? theme::kTungstenU32 : theme::kRackGreyU32,
                                theme::PanelSection::Output,
                                state.outputActive ? "LIVE" : "IDLE"))
    {
        return;
    }

    char label[256];

    const int selected = state.selectedDisplay;

    if (state.displays && !state.displays->empty())
    {
        const std::vector<DisplayInfo>& displays = *state.displays;

        const bool valid = selected >= 0 && selected < static_cast<int>(displays.size());
        if (valid)
        {
            formatDisplay(displays[static_cast<std::size_t>(selected)], label, sizeof(label));
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##output", valid ? label : "No output"))
        {
            if (ImGui::Selectable("No output", !valid) && state.requestOutputClose)
            {
                *state.requestOutputClose = true;
            }

            for (int i = 0; i < static_cast<int>(displays.size()); ++i)
            {
                ImGui::PushID(i);
                formatDisplay(displays[static_cast<std::size_t>(i)], label, sizeof(label));
                if (ImGui::Selectable(label, i == selected) && state.requestedDisplay)
                {
                    *state.requestedDisplay = i;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextDisabled("No displays");
    }

    if (state.outputActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::splitMagentaDim);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::splitMagenta);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::splitMagenta);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::keyLight);
    }
    ImGui::BeginDisabled(!state.outputActive);
    if (ImGui::Button("Stop output") && state.requestOutputClose)
    {
        *state.requestOutputClose = true;
    }
    ImGui::EndDisabled();
    if (state.outputActive)
    {
        ImGui::PopStyleColor(4);
    }

    ImGui::SameLine();
    if (ImGui::Button("Rescan displays") && state.requestDisplayRescan)
    {
        *state.requestDisplayRescan = true;
    }

    if (state.outputActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::tungsten);
        ImGui::Text("Sending  %ux%u", state.outputWidth, state.outputHeight);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Output display is the clock   Escape closes");
    }
    else
    {
        ImGui::TextDisabled("Not sending");
    }

    if (state.outputStatus && !state.outputStatus->empty())
    {
        ImGui::TextWrapped("%s", state.outputStatus->c_str());
    }
}

} // namespace atemfx
