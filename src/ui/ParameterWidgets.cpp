#include "effects/EffectParameters.h"
#include "ui/Fonts.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdio>
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

// How many decimals a range deserves. "%.3f" turns "Hold (s) 2.000" into
// noise, and one decimal on an offset of +-0.4 would hide half the control.
const char* valueFormat(const Parameter& parameter)
{
    if (parameter.type == ParameterType::Int)
    {
        return "%.0f";
    }
    const float span = std::abs(parameter.maxValue - parameter.minValue);
    if (span >= 20.0f) return "%.1f";
    if (span >= 2.0f)  return "%.2f";
    return "%.3f";
}

// Where the shipped default sits on the track. Calibration is mostly the
// question "how far have I strayed", and answering it used to mean reading a
// tooltip; right-click already snaps back, and now it has a visible target.
void drawDefaultTick(const Parameter& parameter)
{
    const float span = parameter.maxValue - parameter.minValue;
    if (!(std::abs(span) > 1.0e-6f))
    {
        return;
    }

    const ImVec2 min  = ImGui::GetItemRectMin();
    const ImVec2 max  = ImGui::GetItemRectMax();
    const float  grab = ImGui::GetStyle().GrabMinSize * 0.5f + theme::scaled(2.0f);
    const float  x0   = min.x + grab;
    const float  x1   = max.x - grab;
    if (x1 <= x0)
    {
        return;
    }

    const float t = std::clamp((parameter.defaultValue - parameter.minValue) / span, 0.0f, 1.0f);
    const float x = x0 + (x1 - x0) * t;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y + theme::scaled(3.0f)),
                                        ImVec2(x, max.y - theme::scaled(3.0f)),
                                        IM_COL32(107, 114, 128, 130), theme::scaled(1.0f));
}

// Everything the operator needs to aim this control, in the one place that
// costs no screen space: what it is, where it can go, where it started, and
// the two gestures that are not discoverable by looking.
void parameterTooltip(const Parameter& parameter, bool automated, bool allowAutomation)
{
    char body[320];
    const char* number = valueFormat(parameter);
    char        ranges[160];
    std::snprintf(ranges, sizeof(ranges), "%s   %s .. %s   default %s", "%s", number, number,
                  number);
    std::snprintf(body, sizeof(body), "%%s\n%s\n%s%sRight-click to reset%s.", ranges,
                  automated ? "Loop drives this value.\n" : "",
                  parameter.type == ParameterType::Bool
                      ? ""
                      : "Drag the number for fine steps, Ctrl-click it to type.\n",
                  allowAutomation ? " and remove the loop" : "");
    ImGui::SetTooltip(body, parameter.label.c_str(), parameter.id.c_str(), parameter.minValue,
                      parameter.maxValue, parameter.defaultValue);
}

// One parameter, drawn the same way wherever it appears: the sidebar's single
// column and the inspector's several share this so a source parameter and an
// effect parameter never read differently.
//
// Two lines, laid out like a rack strip. The name on the left; the value on
// the right as a control the operator can drag by the digit or type into, with
// the loop toggle after it; the track underneath, carrying position in the
// range and the default mark rather than a number repeated from the line
// above. Before this the number lived centred inside the slider - unreadable
// down a column of fourteen - and the only way to set an exact value was a
// Ctrl-click that nothing advertised.
void drawParameterRow(Parameter& parameter, bool allowAutomation, bool separatorAbove)
{
    // A rule between parameters, never above the first one in a column: two
    // lines of controls with nothing between them made it easy to read a track
    // as belonging to the name below it rather than the name above it. The
    // rule closes each parameter off, so the pair that moves together is
    // visibly one thing.
    if (separatorAbove)
    {
        ImGui::Separator();
        ImGui::Spacing();
    }

    const bool inlineToggle = parameter.type == ParameterType::Bool;
    const bool isChoice     = parameter.type == ParameterType::Int && !parameter.choices.empty();
    const bool automated    = allowAutomation && parameter.automation.enabled;

    const ImGuiStyle& style     = ImGui::GetStyle();
    const float       rowHeight = ImGui::GetFrameHeight();

    ui::pushMono();
    const float valueWidth = ImGui::CalcTextSize("-0000.000").x + style.FramePadding.x * 2.0f;
    ui::popMono();

    float trailing = inlineToggle ? rowHeight : (isChoice ? 0.0f : valueWidth);
    if (allowAutomation)
    {
        trailing += (trailing > 0.0f ? style.ItemSpacing.x : 0.0f) + rowHeight;
    }

    // Set by whichever widget turned out to be this parameter's control, and
    // read after the row is closed: the reset gesture belongs to the control,
    // not to the table around it.
    bool controlHovered = false;

    if (ImGui::BeginTable("##header", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
                                std::max(trailing, 1.0f));

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
        }
        else if (!isChoice)
        {
            // A track 250 units wide spends one pixel on 1/250th of the range,
            // which is not a calibration control. This is: drag it for fine
            // steps, Ctrl-click it to type an exact value. While a loop runs it
            // shows what the loop is doing rather than the manual value the
            // operator can no longer see.
            ui::pushMono();
            ImGui::SetNextItemWidth(valueWidth);
            ImGui::BeginDisabled(automated);
            float       value = automated ? parameter.currentValue() : parameter.asFloat();
            const float speed =
                std::max(std::abs(parameter.maxValue - parameter.minValue) / 300.0f, 1.0e-4f);
            if (parameter.type == ParameterType::Int)
            {
                int shown = static_cast<int>(value);
                if (ImGui::DragInt("##number", &shown, std::max(speed, 0.05f),
                                   static_cast<int>(parameter.minValue),
                                   static_cast<int>(parameter.maxValue), "%d",
                                   ImGuiSliderFlags_AlwaysClamp))
                {
                    parameter.setInt(shown);
                }
            }
            else if (ImGui::DragFloat("##number", &value, speed, parameter.minValue,
                                      parameter.maxValue, valueFormat(parameter),
                                      ImGuiSliderFlags_AlwaysClamp))
            {
                parameter.setFloat(value);
            }
            ImGui::EndDisabled();
            controlHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            ui::popMono();
        }

        if (allowAutomation)
        {
            if (trailing > rowHeight)
            {
                ImGui::SameLine();
            }
            if (theme::glyphButton("##loop", theme::Glyph::Loop, rowHeight,
                                   "Repeat this parameter over time.\n"
                                   "Turning it off restores the manual value.",
                                   theme::ButtonAccent::Cyan, parameter.automation.enabled))
            {
                parameter.automation.enabled = !parameter.automation.enabled;
            }
        }
        ImGui::EndTable();
    }

    if (!inlineToggle)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::BeginDisabled(automated);
        if (isChoice)
        {
            int        value = parameter.asInt();
            const bool valid = value >= 0 && value < static_cast<int>(parameter.choices.size());
            if (ImGui::BeginCombo("##value", valid ? parameter.choices[value].c_str() : "Select"))
            {
                for (int i = 0; i < static_cast<int>(parameter.choices.size()); ++i)
                {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(parameter.choices[i].c_str(), i == value))
                    {
                        parameter.setInt(i);
                    }
                    if (i == value) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            controlHovered =
                controlHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        }
        else
        {
            // The track carries position in the range and nothing else: the
            // number is on the line above, and NoInput stops a Ctrl-click here
            // from opening an edit box that has no text in it to edit.
            float      value = automated ? parameter.currentValue() : parameter.asFloat();
            const auto flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput;
            if (ImGui::SliderFloat("##value", &value, parameter.minValue, parameter.maxValue, "",
                                   flags) &&
                !automated)
            {
                if (parameter.type == ParameterType::Int)
                {
                    parameter.setInt(static_cast<int>(std::lround(value)));
                }
                else
                {
                    parameter.setFloat(value);
                }
            }
            controlHovered =
                controlHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            drawDefaultTick(parameter);
        }
        ImGui::EndDisabled();
    }

    // Right-click to reset: faster than hunting for the default value with a
    // mouse while something is on air.
    if (controlHovered)
    {
        parameterTooltip(parameter, automated, allowAutomation);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            parameter.reset();
        }
    }

    // The loop panel is part of the parameter that owns it, so it sits inside
    // the same pair of rules rather than closing itself off with one more.
    if (automated)
    {
        drawLoopSettings(parameter);
    }
    ImGui::Spacing();
}

} // namespace

std::size_t parameterColumnSpan(std::size_t count, int columns)
{
    if (columns <= 1 || count == 0)
    {
        return count;
    }
    const std::size_t wide = static_cast<std::size_t>(columns);
    return (count + wide - 1) / wide;
}

void drawParameters(ParameterSet& parameters, const char* idScope, bool allowAutomation)
{
    if (parameters.empty())
    {
        ImGui::TextDisabled("No parameters");
        return;
    }

    ImGui::PushID(idScope);

    bool first = true;
    for (Parameter& parameter : parameters.all())
    {
        ImGui::PushID(parameter.id.c_str());
        drawParameterRow(parameter, allowAutomation, !first);
        ImGui::PopID();
        first = false;
    }

    ImGui::PopID();
}

void drawParametersColumns(ParameterSet& parameters, const char* idScope, int columns,
                           bool allowAutomation)
{
    if (columns <= 1)
    {
        drawParameters(parameters, idScope, allowAutomation);
        return;
    }
    if (parameters.empty())
    {
        ImGui::TextDisabled("No parameters");
        return;
    }

    const std::size_t span = parameterColumnSpan(parameters.all().size(), columns);

    ImGui::PushID(idScope);
    // One row of cells rather than a grid: a parameter that grows a loop panel
    // must lengthen its own column, not push every neighbour on its row down
    // with it.
    if (ImGui::BeginTable("##parameter_columns", columns,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        int         column = 0;
        std::size_t index  = 0;
        for (Parameter& parameter : parameters.all())
        {
            bool firstInColumn = index == 0;
            if (index > 0 && index % span == 0 && column + 1 < columns)
            {
                ++column;
                ImGui::TableSetColumnIndex(column);
                firstInColumn = true;
            }
            ImGui::PushID(parameter.id.c_str());
            drawParameterRow(parameter, allowAutomation, !firstInColumn);
            ImGui::PopID();
            ++index;
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

} // namespace atemfx
