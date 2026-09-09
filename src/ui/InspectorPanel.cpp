#include "ui/Inspector.h"

#include <algorithm>
#include <cstddef>

#include "effects/EffectChain.h"
#include "effects/EffectParameters.h"
#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

namespace atemfx {

namespace {

Effect* g_inspected     = nullptr;
bool    g_framingGrid   = true;

// Narrower than this and a label wraps to three lines while its slider shrinks
// to a nub, which is exactly the sidebar problem this panel exists to fix.
constexpr float kMinColumnWidth = 250.0f;
constexpr int   kMaxColumns     = 4;

// One parameter's block, in the shape drawParameters gives it: a boolean rides
// on its label row, everything else puts its control underneath.
float parameterHeight(const Parameter& parameter)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float       row   = ImGui::GetFrameHeightWithSpacing();

    float height = parameter.type == ParameterType::Bool ? row : row * 2.0f;
    height += style.ItemSpacing.y;

    if (parameter.automation.enabled)
    {
        // The loop panel: five settings rows, the cycle preview, the transport
        // buttons and the separator it earns.
        height += row * 5.0f + theme::scaled(58.0f) + row + style.ItemSpacing.y * 3.0f;
    }
    return height;
}

// The rule drawn above every parameter but the first in its column, plus the
// gap under it. Counted here or the panel clips its last track.
float separatorHeight()
{
    return 1.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
}

float headerHeight()
{
    return ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
}

} // namespace

namespace ui {

Effect* inspectedEffect()
{
    return g_inspected;
}

void inspectEffect(Effect* effect)
{
    g_inspected = effect;
}

void closeInspector()
{
    g_inspected = nullptr;
}

bool adjustingFraming()
{
    return g_inspected != nullptr && g_inspected->role() == EffectRole::Framing;
}

bool framingGridEnabled()
{
    return g_framingGrid;
}

void setFramingGridEnabled(bool enabled)
{
    g_framingGrid = enabled;
}

void validateInspector(const EffectChain* chain)
{
    if (!g_inspected)
    {
        return;
    }
    if (!chain)
    {
        g_inspected = nullptr;
        return;
    }

    for (std::size_t i = 0; i < chain->size(); ++i)
    {
        if (&chain->at(i) == g_inspected)
        {
            return;
        }
    }
    g_inspected = nullptr;
}

int inspectorColumns(float width)
{
    const float usable = width - ImGui::GetStyle().WindowPadding.x * 2.0f;
    const int   fit    = static_cast<int>(usable / theme::scaled(kMinColumnWidth));
    return std::clamp(fit, 1, kMaxColumns);
}

float inspectorHeight(float width, float minimum, float maximum)
{
    if (!g_inspected)
    {
        return minimum;
    }

    const ParameterSet& parameters = g_inspected->parameters();
    const int           columns    = inspectorColumns(width);
    const std::size_t   span       = parameterColumnSpan(parameters.all().size(), columns);

    // The tallest column decides the panel, and the columns are filled in the
    // order the renderer fills them, so the two cannot disagree about which
    // parameter landed where.
    float tallest = 0.0f;
    float column  = 0.0f;
    std::size_t index = 0;
    for (const Parameter& parameter : parameters.all())
    {
        if (span > 0 && index > 0 && index % span == 0)
        {
            tallest = std::max(tallest, column);
            column  = 0.0f;
        }
        if (column > 0.0f)
        {
            column += separatorHeight();
        }
        column += parameterHeight(parameter);
        ++index;
    }
    tallest = std::max(tallest, column);

    // Plus the table's own cell padding, which the per-parameter arithmetic
    // above does not see and which is enough on its own to clip the last
    // slider of the tallest column.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float wanted = style.WindowPadding.y * 2.0f + style.CellPadding.y * 2.0f +
                         headerHeight() + tallest;
    return std::clamp(wanted, minimum, maximum);
}

} // namespace ui

void drawInspectorPanel(UiFrameState& state)
{
    Effect* effect = ui::inspectedEffect();
    if (!effect)
    {
        drawStatsPanel(state);
        return;
    }

    const float glyph     = ImGui::GetFrameHeight();
    const float rowStartX = ImGui::GetCursorPosX();
    const float rowWidth  = ImGui::GetContentRegionAvail().x;

    ui::pushMono();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted("PARAMETERS");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, theme::scaled(10.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::splitCyan);
    ImGui::TextUnformatted(effect->descriptor().displayName.c_str());
    ImGui::PopStyleColor();
    ui::popMono();

    // The bypass lives beside the name because this panel is where the
    // operator is looking while they judge whether the effect earns its place.
    ImGui::SameLine(0.0f, theme::scaled(18.0f));
    bool enabled = effect->enabled();
    if (ImGui::Checkbox("Enabled", &enabled))
    {
        effect->setEnabled(enabled);
    }

    // The grid belongs to this panel because this panel is where framing is
    // set. It is not an effect parameter: it changes no pixel the chain
    // produces, and a parameter that does nothing to the image would be a lie
    // in every preset that ever saves one.
    if (effect->role() == EffectRole::Framing)
    {
        ImGui::SameLine(0.0f, theme::scaled(18.0f));
        bool grid = ui::framingGridEnabled();
        if (ImGui::Checkbox("Grid", &grid))
        {
            ui::setFramingGridEnabled(grid);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Thirds and centre over the monitors while you set the framing.\n"
                              "Preview only: it is drawn by the interface, never by the chain,\n"
                              "so it cannot reach the output or the webcam. It disappears when\n"
                              "this panel closes.");
        }
    }

    const std::size_t loopCount = effect->parameters().activeAutomationCount();
    if (loopCount > 0)
    {
        ImGui::SameLine(0.0f, theme::scaled(18.0f));
        ImGui::TextDisabled("%zu loop%s", loopCount, loopCount == 1 ? "" : "s");
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(rowStartX + rowWidth - glyph);
    if (theme::glyphButton("##close_inspector", theme::Glyph::Close, glyph,
                           "Close and show the stats strip"))
    {
        ui::closeInspector();
    }

    ImGui::Separator();

    if (!effect->lastError().empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::splitMagenta);
        ImGui::TextWrapped("%s", effect->lastError().c_str());
        ImGui::PopStyleColor();
    }

    ImGui::PushID(effect);
    drawParametersColumns(effect->parameters(), effect->descriptor().typeId.c_str(),
                          ui::inspectorColumns(ImGui::GetWindowWidth()), true);
    ImGui::PopID();
}

} // namespace atemfx
