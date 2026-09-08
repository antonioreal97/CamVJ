#include "effects/EffectParameters.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#include "imgui.h"

namespace atemfx {

namespace {

float previewValue(const Parameter& parameter, double phase)
{
    const ParameterAutomation& loop = parameter.automation;
    float value = loop.minValue + ParameterAutomation::sample(loop.waveform, phase) *
                                      (loop.maxValue - loop.minValue);
    if (parameter.type == ParameterType::Bool)
    {
        value = value >= 0.5f ? 1.0f : 0.0f;
    }
    else if (parameter.type == ParameterType::Int)
    {
        value = std::round(value);
    }

    const float span = loop.maxValue - loop.minValue;
    return span > 0.0f ? std::clamp((value - loop.minValue) / span, 0.0f, 1.0f) : 0.5f;
}

void drawLoopPreview(const Parameter& parameter)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(theme::scaled(40.0f), ImGui::GetContentRegionAvail().x),
                      theme::scaled(58.0f));
    ImGui::InvisibleButton("##loop_preview", size);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("One complete cycle. The marker follows the current phase.");
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                            ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);
    const float  inset = theme::scaled(5.0f);
    const ImVec2 min(origin.x + inset, origin.y + inset);
    const ImVec2 max(origin.x + size.x - inset, origin.y + size.y - inset);
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Border);
    drawList->AddLine(ImVec2(min.x, min.y + height * 0.5f),
                      ImVec2(max.x, min.y + height * 0.5f), gridColor);
    for (int quarter = 1; quarter < 4; ++quarter)
    {
        const float x = min.x + width * static_cast<float>(quarter) * 0.25f;
        drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), gridColor);
    }

    std::array<ImVec2, 97> points;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        // Show the left-hand limit at the boundary, so sawtooth and pulse
        // previews do not draw a false return ramp at the end of the cycle.
        const double phase = std::min(static_cast<double>(i) / (points.size() - 1),
                                      1.0 - 1.0e-7);
        points[i] = ImVec2(min.x + width * static_cast<float>(phase),
                            max.y - height * previewValue(parameter, phase));
    }
    drawList->AddPolyline(points.data(), static_cast<int>(points.size()),
                          theme::kSplitCyanU32, ImDrawFlags_None, 1.5f);

    const double phase = parameter.automation.phase();
    const float x = min.x + width * static_cast<float>(phase);
    drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), theme::kTungstenU32);
    drawList->AddCircleFilled(ImVec2(x, max.y - height * previewValue(parameter, phase)),
                              3.0f, theme::kTungstenU32);
}

void loopSettingRow(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void drawLoopSettings(Parameter& parameter)
{
    ParameterAutomation& loop = parameter.automation;
    if (!ImGui::CollapsingHeader("Loop settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }

    if (ImGui::BeginTable("##loop_settings", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, theme::scaled(76.0f));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        loopSettingRow("Shape");
        int waveform = static_cast<int>(loop.waveform);
        if (ImGui::Combo("##shape", &waveform, "Sine\0Triangle\0Ramp up\0Ramp down\0Pulse\0"))
        {
            loop.waveform = static_cast<AutomationWaveform>(waveform);
        }

        if (parameter.type == ParameterType::Bool)
        {
            loopSettingRow("Range");
            ImGui::TextDisabled("Off / On");
        }
        else if (parameter.type == ParameterType::Int)
        {
            loopSettingRow("Min");
            int min = static_cast<int>(loop.minValue);
            if (ImGui::SliderInt("##min", &min, static_cast<int>(parameter.minValue),
                                  static_cast<int>(loop.maxValue), "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                loop.minValue = static_cast<float>(min);
            }
            loopSettingRow("Max");
            int max = static_cast<int>(loop.maxValue);
            if (ImGui::SliderInt("##max", &max, static_cast<int>(loop.minValue),
                                  static_cast<int>(parameter.maxValue), "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                loop.maxValue = static_cast<float>(max);
            }
        }
        else
        {
            loopSettingRow("Min");
            ImGui::SliderFloat("##min", &loop.minValue, parameter.minValue, loop.maxValue,
                                "%.3f", ImGuiSliderFlags_AlwaysClamp);
            loopSettingRow("Max");
            ImGui::SliderFloat("##max", &loop.maxValue, loop.minValue, parameter.maxValue,
                                "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }

        loopSettingRow("Cycle (s)");
        float period = static_cast<float>(loop.periodSeconds);
        if (ImGui::SliderFloat("##cycle", &period, 0.05f, 600.0f, "%.2f",
                                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
        {
            loop.periodSeconds = period;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Seconds per complete cycle. Ctrl-click to enter a value.");
        }

        loopSettingRow("Phase (%)");
        float phasePercent = loop.phaseOffset * 100.0f;
        if (ImGui::SliderFloat("##phase", &phasePercent, 0.0f, 100.0f, "%.0f%%",
                                ImGuiSliderFlags_AlwaysClamp))
        {
            loop.phaseOffset = phasePercent / 100.0f;
        }
        ImGui::EndTable();
    }

    drawLoopPreview(parameter);
    if (ImGui::Button(loop.paused ? "Resume" : "Pause"))
    {
        loop.paused = !loop.paused;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
    {
        loop.restart();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Return to the configured phase; keep pause state.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", loop.paused ? "Paused" : "Running");
}

// Width of the controls that share the label's row, so the label column can
// stretch into whatever is left of it.
float trailingControlWidth(bool inlineToggle, bool allowAutomation)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    float width = 0.0f;
    if (inlineToggle)
    {
        width += ImGui::GetFrameHeight();
    }
    if (allowAutomation)
    {
        if (width > 0.0f)
        {
            width += style.ItemSpacing.x;
        }
        width += ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                 ImGui::CalcTextSize("Loop").x;
    }
    return width;
}

} // namespace

void drawParameters(ParameterSet& parameters, const char* idScope, bool allowAutomation)
{
    if (parameters.empty())
    {
        ImGui::TextDisabled("No parameters");
        return;
    }

    ImGui::PushID(idScope);

    for (Parameter& parameter : parameters.all())
    {
        ImGui::PushID(parameter.id.c_str());

        // A boolean has nothing worth a full-width row, so its toggle rides on
        // the label line. Everything else gets label above, control below.
        const bool inlineToggle = parameter.type == ParameterType::Bool;
        const bool automated    = allowAutomation && parameter.automation.enabled;

        // Set by whichever widget turned out to be this parameter's control,
        // and read after the row is closed: the reset gesture belongs to the
        // control, not to the table around it.
        bool controlHovered = false;

        // Every parameter gets this row, automated or not. That is the point:
        // a source parameter and an effect parameter should read the same way,
        // and before this they did not.
        const float trailing = trailingControlWidth(inlineToggle, allowAutomation);
        if (trailing <= 0.0f)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", parameter.label.c_str());
        }
        else if (ImGui::BeginTable("##header", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, trailing);

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", parameter.label.c_str());

            ImGui::TableNextColumn();
            if (inlineToggle)
            {
                bool value = parameter.asBool();
                ImGui::BeginDisabled(automated);
                if (ImGui::Checkbox("##on", &value))
                {
                    parameter.setBool(value);
                }
                ImGui::EndDisabled();
                controlHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
                if (allowAutomation)
                {
                    ImGui::SameLine();
                }
            }
            if (allowAutomation)
            {
                ImGui::Checkbox("Loop", &parameter.automation.enabled);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Repeat this parameter over time. Turning Loop off restores the manual value.");
                }
            }
            ImGui::EndTable();
        }

        if (!inlineToggle)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::BeginDisabled(automated);
            if (parameter.type == ParameterType::Int)
            {
                int value = parameter.asInt();
                if (ImGui::SliderInt("##value",
                                     &value,
                                     static_cast<int>(parameter.minValue),
                                     static_cast<int>(parameter.maxValue)))
                {
                    parameter.setInt(value);
                }
            }
            else
            {
                float value = parameter.asFloat();
                if (ImGui::SliderFloat("##value",
                                       &value,
                                       parameter.minValue,
                                       parameter.maxValue,
                                       "%.3f"))
                {
                    parameter.setFloat(value);
                }
            }
            ImGui::EndDisabled();
            controlHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        }

        // Right-click to reset: faster than hunting for the default value with
        // a mouse while something is on air.
        if (controlHovered)
        {
            ImGui::SetTooltip("%s\n%sRight-click to reset (%.3f)%s",
                              parameter.id.c_str(),
                              automated ? "Loop controls this value.\n" : "",
                              parameter.defaultValue,
                              allowAutomation ? " and remove loop" : "");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                parameter.reset();
            }
        }

        // A separator only where one earns its place: after the tall block a
        // loop panel adds. Between plain rows the spacing is enough.
        if (automated)
        {
            drawLoopSettings(parameter);
            ImGui::Spacing();
            ImGui::Separator();
        }
        ImGui::Spacing();

        ImGui::PopID();
    }

    ImGui::PopID();
}

} // namespace atemfx
