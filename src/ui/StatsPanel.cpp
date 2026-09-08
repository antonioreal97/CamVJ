#include "ui/Panels.h"

#include <algorithm>

#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "video/FrameTiming.h"

namespace atemfx {

void drawStatsPanel(UiFrameState& state)
{
    const FrameTiming* timing = state.timing;
    if (!timing)
    {
        return;
    }

    ImGui::Columns(3, "stats", false);

    // Every number in this panel is a measurement, so the whole strip is set
    // in the mono face: digits keep their column as the values change instead
    // of shuffling sideways sixty times a second.
    ui::pushMono();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("RATE");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::TextColored(theme::budgetColour(timing->averageFrameMs()), "%.1f fps", timing->fps());
    ImGui::TextDisabled("frame");
    ImGui::SameLine();
    ImGui::Text("%.2f ms", timing->averageFrameMs());
    ImGui::TextDisabled("peak");
    ImGui::SameLine();
    ImGui::Text("%.2f ms", timing->maxFrameMs());
    ImGui::TextDisabled("frames");
    ImGui::SameLine();
    ImGui::Text("%llu", static_cast<unsigned long long>(timing->frameIndex()));

    ImGui::NextColumn();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("GPU");
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (state.gpuTimingValid)
    {
        ImGui::TextColored(theme::budgetColour(state.gpuMilliseconds), "%.2f ms",
                           state.gpuMilliseconds);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::budgetColour(state.gpuMilliseconds));
        ImGui::ProgressBar(std::min(state.gpuMilliseconds / theme::kFrameBudgetMs, 1.0f),
                           ImVec2(-1.0f, 0.0f),
                           "");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("measuring...");
    }
    ImGui::TextDisabled("budget  %.2f ms", theme::kFrameBudgetMs);
    ImGui::TextWrapped("%s", state.adapterName);

    ImGui::NextColumn();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("ENGINE");
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (state.vsync)
    {
        ImGui::BeginDisabled(state.outputActive);
        ImGui::Checkbox("VSync", state.vsync);
        ImGui::EndDisabled();
        if (state.outputActive && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("The output display is the clock while a send is live.");
        }
    }
    if (ImGui::Button("Reload shaders") && state.requestShaderReload)
    {
        *state.requestShaderReload = true;
    }
    if (state.status && !state.status->empty())
    {
        ImGui::TextWrapped("%s", state.status->c_str());
    }

    ui::popMono();

    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::PlotLines("##frametime",
                     timing->history(),
                     static_cast<int>(FrameTiming::kHistorySize),
                     static_cast<int>(timing->historyOffset()),
                     "frame time (ms)",
                     0.0f,
                     theme::kFrameBudgetMs * 2.0f,
                     ImVec2(-1.0f, theme::scaled(52.0f)));
}

} // namespace atemfx
