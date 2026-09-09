#include "ui/Panels.h"

#include <algorithm>

#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "video/FrameTiming.h"
#include "video/VideoSource.h"

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

    // --- Column 1: INPUT ---
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("INPUT");
    ImGui::PopStyleColor();
    ImGui::Separator();

    const SourceHealth& health = state.sourceHealth;
    if (state.sourceDisconnected || health.signal == SourceSignal::Stale)
    {
        bool flash = (static_cast<int>(ImGui::GetTime() * 4.0) % 2) == 0;
        ImGui::TextColored(flash ? theme::splitMagenta : theme::keyLight, "CÂMERA CONGELADA");
        ImGui::TextColored(theme::splitMagenta, state.sourceDisconnected ? "Disconnected" : "Stale");
    }
    else
    {
        const char* signal = "Waiting";
        ImVec4 colour = theme::rackGrey;
        switch (health.signal)
        {
            case SourceSignal::Generated: signal = "Generated"; break;
            case SourceSignal::Waiting:   signal = "Waiting"; break;
            case SourceSignal::Live:      signal = "Live"; colour = theme::splitCyan; break;
            case SourceSignal::Stale:     signal = "Stale"; colour = theme::splitMagenta; break;
        }
        ImGui::TextColored(colour, "%s", signal);
    }

    if (health.signal != SourceSignal::Generated && health.receivedFrames > 0)
    {
        ImGui::Text("%.1f", health.captureFps);
        ImGui::SameLine();
        ImGui::TextDisabled("capture fps");

        if (health.ageSeconds < 1.0)
        {
            ImGui::Text("%.0f ms", health.ageSeconds * 1000.0);
        }
        else
        {
            ImGui::Text("%.1f s", health.ageSeconds);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("age");

        ImGui::Text("%llu", static_cast<unsigned long long>(health.overwrittenFrames));
        ImGui::SameLine();
        ImGui::TextDisabled("drops");

        ImGui::Text("%llu", static_cast<unsigned long long>(health.repeatedFrames));
        ImGui::SameLine();
        ImGui::TextDisabled("repeats");
    }
    else
    {
        ImGui::TextDisabled(health.signal == SourceSignal::Generated ? "GPU source" : "Waiting for frames");
    }

    ImGui::NextColumn();

    // --- Column 2: PROCESSING ---
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("PROCESSING");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::TextColored(theme::budgetColour(timing->averageFrameMs()), "%.1f", timing->fps());
    ImGui::SameLine();
    ImGui::TextDisabled("render fps");

    ImGui::Text("%.2f ms", timing->averageFrameMs());
    ImGui::SameLine();
    ImGui::TextDisabled("frame time");

    if (state.gpuTimingValid)
    {
        ImGui::TextColored(theme::budgetColour(state.gpuMilliseconds), "%.2f ms", state.gpuMilliseconds);
        ImGui::SameLine();
        ImGui::TextDisabled("gpu");
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::budgetColour(state.gpuMilliseconds));
        ImGui::ProgressBar(std::min(state.gpuMilliseconds / theme::kFrameBudgetMs, 1.0f),
                           ImVec2(-1.0f, 0.0f), "");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::TextDisabled("measuring gpu...");
    }

    if (state.vsync)
    {
        ImGui::BeginDisabled(state.outputActive);
        ImGui::Checkbox("VSync", state.vsync);
        ImGui::EndDisabled();
    }

    ImGui::NextColumn();

    // --- Column 3: OUTPUT ---
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("OUTPUT");
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (state.outputActive)
    {
        if (state.inputHealthy)
        {
            ImGui::TextColored(theme::splitCyan, "LIVE");
        }
        else
        {
            ImGui::TextColored(theme::splitMagenta, "FROZEN");
            ImGui::TextDisabled("No fresh input");
        }
        ImGui::Text("%ux%u", state.outputWidth, state.outputHeight);
    }
    else
    {
        ImGui::TextDisabled("Not sending");
    }
    
    ImGui::BeginDisabled(state.operationLocked && *state.operationLocked);
    if (ImGui::Button("Reload shaders") && state.requestShaderReload)
    {
        *state.requestShaderReload = true;
    }
    ImGui::EndDisabled();
    
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
