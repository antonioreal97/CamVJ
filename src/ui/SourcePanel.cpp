#include "ui/Panels.h"

#include <cstdio>
#include <string>

#include "effects/Effect.h"
#include "imgui.h"
#include "ui/Theme.h"
#include "video/VideoSource.h"

namespace atemfx {

void drawSourcePanel(UiFrameState& state)
{
    char meta[32];
    std::snprintf(meta, sizeof(meta), "%ux%u", state.processingWidth, state.processingHeight);
    if (!theme::drawPanelHeader("SOURCE", theme::kSplitCyanU32, theme::PanelSection::Source, meta))
    {
        return;
    }

    if (state.availableSources && !state.availableSources->empty())
    {
        const std::vector<VideoSourceDescriptor>& sources = *state.availableSources;

        const int  selected = state.selectedSource;
        const bool valid    = selected >= 0 && selected < static_cast<int>(sources.size());
        const char* preview = valid ? sources[selected].displayName.c_str() : "Select input";

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##input", preview))
        {
            for (int i = 0; i < static_cast<int>(sources.size()); ++i)
            {
                const VideoSourceDescriptor& source = sources[i];

                ImGui::PushID(i);
                if (ImGui::Selectable(source.displayName.c_str(), i == selected) &&
                    state.requestedSource)
                {
                    *state.requestedSource = i;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", source.category.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Rescan devices") && state.requestDeviceRescan)
        {
            *state.requestDeviceRescan = true;
        }
    }
    else
    {
        ImGui::TextDisabled("No inputs");
    }

    if (state.source)
    {
        const std::string status = state.source->status();
        if (!status.empty())
        {
            ImGui::TextWrapped("%s", status.c_str());
        }
    }

    if (state.trackingStatus && !state.trackingStatus->empty())
    {
        // Tungsten is the locked subject in the mark. Use it when the tracker
        // has a body; otherwise this line is just a diagnostic.
        const bool locked = state.effectContext && state.effectContext->tracking.valid;
        ImGui::PushStyleColor(ImGuiCol_Text, locked ? theme::tungsten : theme::rackGrey);
        ImGui::TextWrapped("Tracking  %s", state.trackingStatus->c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    if (state.source)
    {
        drawParameters(state.source->parameters(), "source");
    }
}

} // namespace atemfx
