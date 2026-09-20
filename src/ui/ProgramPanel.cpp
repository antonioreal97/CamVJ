#include "ui/Panels.h"

#include <cstdio>

#include "imgui.h"
#include "ui/Theme.h"

namespace atemfx {

float programControlsHeight()
{
    return ImGui::GetTextLineHeightWithSpacing() + theme::actionHeight();
}

void drawProgramPanel(UiFrameState& state)
{
    struct ModeControl
    {
        ProgramMode mode;
        const char* label;
        const char* description;
    };
    constexpr ModeControl controls[] = {
        {ProgramMode::Effects, "FX", "Send the current effect chain."},
        {ProgramMode::Clean, "Clean", "Remove visual effects; preserve framing and the 9:16 output area."},
        {ProgramMode::Freeze, "Freeze", "Hold the last PROGRAM frame while the camera keeps running."},
        {ProgramMode::Black, "Black", "Send black while keeping the output open."},
    };
    constexpr const char* kSafetyFreezeTip =
        "Safety hold from input loss. Select a live input, then FX or Clean to resume.";

    // What goes to air is the first decision of the show, so it sits above
    // routing and the chain rather than inside OUTPUT. The caption repeats the
    // live state where the eye already looks for a panel's value.
    const ProgramMode current = state.programMode ? *state.programMode : ProgramMode::Effects;
    char caption[32];
    if (state.programSafetyHold && current == ProgramMode::Freeze)
    {
        // Distinct from an intentional Freeze: recovery is still an explicit
        // mode change after the camera (or cable) is back.
        std::snprintf(caption, sizeof(caption), "HOLD · INPUT");
    }
    else if (state.programMixing)
    {
        // How far along the dissolve is, not how much of the old picture is
        // left: the operator asked for a mode and wants to know when they
        // have it.
        std::snprintf(caption, sizeof(caption), "%s %.0f%%", programModeName(current),
                      static_cast<double>(state.programProgress) * 100.0);
    }
    else
    {
        std::snprintf(caption, sizeof(caption), "%s", programModeName(current));
    }
    theme::drawGroupLabel("PROGRAM", caption, theme::kSplitMagentaU32);

    const float width  = theme::rowButtonWidth(4);
    const float height = theme::actionHeight();

    ImGui::BeginDisabled(!state.programMode);
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }
        const ModeControl& control = controls[i];
        const bool active = state.programMode && *state.programMode == control.mode;
        if (theme::actionButton(control.label, theme::ButtonAccent::Magenta,
                                ImVec2(width, height), active) &&
            state.programMode)
        {
            *state.programMode = control.mode;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            const char* tip = control.description;
            if (control.mode == ProgramMode::Freeze && state.programSafetyHold)
            {
                tip = kSafetyFreezeTip;
            }
            ImGui::SetTooltip("%s", tip);
        }
    }
    ImGui::EndDisabled();
}

} // namespace atemfx
