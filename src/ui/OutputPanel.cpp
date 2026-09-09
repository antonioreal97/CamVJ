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
    const bool expanded = theme::drawPanelHeader(
        "OUTPUT", state.outputActive ? theme::kSplitMagentaU32 : theme::kRackGreyU32,
        theme::PanelSection::Output, state.outputActive ? "SEND" : "OFF");

    if (!expanded)
    {
        return;
    }

    char label[256];
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool routingLocked = state.operationLocked && *state.operationLocked;
    const int selected = state.selectedDisplay;

    // The rescan belongs on the row it repairs: an empty display list is
    // exactly when the operator needs the button, and it used to sit two rows
    // below the words "No displays".
    const float rescanWidth = ImGui::CalcTextSize("Rescan").x + style.FramePadding.x * 2.0f;
    const float rowStartX   = ImGui::GetCursorPosX();
    const float rowWidth    = ImGui::GetContentRegionAvail().x;

    ImGui::BeginDisabled(routingLocked && state.outputActive);
    if (state.displays && !state.displays->empty())
    {
        const std::vector<DisplayInfo>& displays = *state.displays;

        const bool valid = selected >= 0 && selected < static_cast<int>(displays.size());
        if (valid)
        {
            formatDisplay(displays[static_cast<std::size_t>(selected)], label, sizeof(label));
        }

        ImGui::SetNextItemWidth(rowWidth - rescanWidth - style.ItemSpacing.x);
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
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No displays");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + rowWidth - rescanWidth);
    ImGui::BeginDisabled(routingLocked);
    if (ImGui::Button("Rescan", ImVec2(rescanWidth, 0.0f)) && state.requestDisplayRescan)
    {
        *state.requestDisplayRescan = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Look for displays again.");
    }

    // Closing the send is the one control on this panel that has to be found
    // without reading, so it takes the whole row and carries the program
    // colour while it is armed.
    ImGui::BeginDisabled(!state.outputActive || routingLocked);
    if (theme::actionButton("Stop output", theme::ButtonAccent::Magenta,
                            ImVec2(-1.0f, 0.0f), state.outputActive) &&
        state.requestOutputClose)
    {
        *state.requestOutputClose = true;
    }
    ImGui::EndDisabled();

    if (state.outputActive)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
        ImGui::Text("Sending  %ux%u", state.outputWidth, state.outputHeight);
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Send open   Escape closes output");
    }
    else
    {
        ImGui::TextDisabled("Not sending");
    }

    if (state.outputStatus && !state.outputStatus->empty())
    {
        ImGui::TextWrapped("%s", state.outputStatus->c_str());
    }

    // The webcam carries the same PROGRAM picture to a local camera device,
    // so a call application can take it. Its own control rather than a second
    // entry in the display list: it opens no window, it answers to no display,
    // and it is normally used at the same time as the wall, not instead of it.
    ImGui::Spacing();

    const VirtualCameraState webcamState = state.webcamStats.state;
    const bool webcamLive = webcamState == VirtualCameraState::Sending;
    const bool webcamBusy = webcamState == VirtualCameraState::Starting ||
                            webcamState == VirtualCameraState::Stopping;

    theme::drawGroupLabel("WEBCAM", webcamLive ? "LIVE" : "OFF",
                          webcamLive ? theme::kSplitMagentaU32 : theme::kRackGreyU32);

    ImGui::BeginDisabled(!state.webcamSupported || webcamBusy || routingLocked);
    if (webcamLive)
    {
        if (theme::actionButton("Stop webcam", theme::ButtonAccent::Magenta,
                                ImVec2(-1.0f, 0.0f), true) &&
            state.requestWebcamStop)
        {
            *state.requestWebcamStop = true;
        }
    }
    else if (theme::actionButton("Start webcam", theme::ButtonAccent::Magenta,
                                 ImVec2(-1.0f, 0.0f)) &&
             state.requestWebcamStart)
    {
        *state.requestWebcamStart = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip(state.webcamSupported
                              ? "Send PROGRAM to call applications as a camera."
                              : "This build has no webcam output.");
    }

    if (webcamLive)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
        ImGui::Text("Sending to the camera device");
        ImGui::PopStyleColor();
        // Dropped frames are the only symptom a slow consumer produces here,
        // so they are on the panel rather than in a log the operator is not
        // reading during a show.
        ImGui::TextDisabled("Frames %llu   dropped %llu",
                            static_cast<unsigned long long>(state.webcamStats.sent),
                            static_cast<unsigned long long>(state.webcamStats.skipped));
    }
    else if (webcamBusy)
    {
        ImGui::TextDisabled("Working");
    }
    else
    {
        ImGui::TextDisabled("Not sending");
    }

    if (state.webcamStatus && !state.webcamStatus->empty())
    {
        ImGui::TextWrapped("%s", state.webcamStatus->c_str());
    }
}

} // namespace atemfx
