#include "ui/Inspector.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "effects/EffectChain.h"
#include "effects/EffectParameters.h"
#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Panels.h"
#include "ui/Theme.h"

namespace atemfx {

namespace {

ui::InspectorKind g_inspectorKind = ui::InspectorKind::Stats;
Effect*           g_inspected     = nullptr;
uint64_t          g_overlayLayer  = 0;
std::string       g_overlayAsset;
bool              g_framingGrid   = true;

// Narrower than this and a label wraps to three lines while its slider shrinks
// to a nub, which is exactly the sidebar problem this panel exists to fix.
constexpr float kMinColumnWidth = 250.0f;
constexpr int   kMaxColumns     = 4;

// One click / key of the compose pad. Coarse enough to feel on an LED wall,
// fine enough that Offset X's ±0.40 range is about forty steps.
constexpr float kComposeStep = 0.02f;

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

// COMPOSE label, three D-pad rows, then the separator that sits under the pad
// before the parameter columns. Must agree with drawFramingComposePad.
float framingComposePadHeight()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float       glyph = ImGui::GetFrameHeight();
    return ImGui::GetTextLineHeight() + style.ItemSpacing.y +
           glyph * 3.0f + style.ItemSpacing.y * 3.0f +
           separatorHeight();
}

void nudgeParam(ParameterSet& parameters, std::string_view id, float delta)
{
    Parameter* parameter = parameters.find(id);
    if (!parameter)
    {
        return;
    }

    const float low  = std::min(parameter->minValue, parameter->maxValue);
    const float high = std::max(parameter->minValue, parameter->maxValue);
    parameter->setFloat(std::clamp(parameter->value + delta, low, high));
}

void drawFramingComposePad(Effect& effect)
{
    ParameterSet& parameters = effect.parameters();
    const bool    follow     = parameters.valueOr("follow", 1.0f) >= 0.5f;
    const float   glyph      = ImGui::GetFrameHeight();
    const float   gap        = theme::scaled(4.0f);

    theme::drawGroupLabel("COMPOSE", follow ? nullptr : "FOLLOW OFF");

    const char* blocked = "Follow Subject must be on.\n"
                          "The pad nudges Offset X and Headroom while tracking.";
    // Arrows move the crop on SOURCE (and the picture on the wall), not the
    // subject marker. Positive Offset X / more Headroom shift the crop the
    // other way in framing.cpp, so the pad signs are the inverse of the
    // parameter labels.
    const char* tipLeft =
        follow ? "Move framing left (Offset X)" : blocked;
    const char* tipRight =
        follow ? "Move framing right (Offset X)" : blocked;
    const char* tipUp =
        follow ? "Move framing up (more Headroom)" : blocked;
    const char* tipDown =
        follow ? "Move framing down (less Headroom)" : blocked;

    if (!follow)
    {
        ImGui::BeginDisabled();
    }

    // D-pad: empty | Up | empty / Left | spacer | Right / empty | Down | empty.
    // Indent matches one button so Up and Down sit over the centre gap.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glyph + gap);
    if (theme::glyphButton("##compose_up", theme::Glyph::Up, glyph, tipUp,
                           theme::ButtonAccent::Cyan))
    {
        nudgeParam(parameters, "headroom", kComposeStep);
    }

    if (theme::glyphButton("##compose_left", theme::Glyph::Left, glyph, tipLeft,
                           theme::ButtonAccent::Cyan))
    {
        nudgeParam(parameters, "offset_x", kComposeStep);
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::Dummy(ImVec2(glyph, glyph));
    ImGui::SameLine(0.0f, gap);
    if (theme::glyphButton("##compose_right", theme::Glyph::Right, glyph, tipRight,
                           theme::ButtonAccent::Cyan))
    {
        nudgeParam(parameters, "offset_x", -kComposeStep);
    }

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + glyph + gap);
    if (theme::glyphButton("##compose_down", theme::Glyph::Down, glyph, tipDown,
                           theme::ButtonAccent::Cyan))
    {
        nudgeParam(parameters, "headroom", -kComposeStep);
    }

    if (!follow)
    {
        ImGui::EndDisabled();
    }

    // Keyboard matches the pad while this panel is composing and nothing is
    // capturing text (Ctrl-click on a DragFloat opens an input box).
    if (follow && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
        {
            nudgeParam(parameters, "offset_x", kComposeStep);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
        {
            nudgeParam(parameters, "offset_x", -kComposeStep);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        {
            nudgeParam(parameters, "headroom", kComposeStep);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        {
            nudgeParam(parameters, "headroom", -kComposeStep);
        }
    }

    ImGui::Separator();
}

} // namespace

namespace ui {

InspectorKind inspectorKind()
{
    return g_inspectorKind;
}

Effect* inspectedEffect()
{
    return g_inspectorKind == InspectorKind::Effect ? g_inspected : nullptr;
}

void inspectEffect(Effect* effect)
{
    if (!effect)
    {
        closeInspector();
        return;
    }
    g_inspectorKind = InspectorKind::Effect;
    g_inspected = effect;
    g_overlayLayer = 0;
    g_overlayAsset.clear();
}

uint64_t inspectedOverlayLayerId()
{
    return g_inspectorKind == InspectorKind::OverlayLayer ? g_overlayLayer : 0;
}

std::string_view inspectedOverlayAssetId()
{
    return g_inspectorKind == InspectorKind::OverlayLibrary ? std::string_view(g_overlayAsset)
                                                             : std::string_view{};
}

void inspectOverlayLayer(uint64_t layerId)
{
    if (layerId == 0)
    {
        closeInspector();
        return;
    }
    g_inspectorKind = InspectorKind::OverlayLayer;
    g_inspected     = nullptr;
    g_overlayLayer  = layerId;
    g_overlayAsset.clear();
}

void inspectOverlayLibrary(std::string_view selectedAssetId)
{
    g_inspectorKind = InspectorKind::OverlayLibrary;
    g_inspected     = nullptr;
    g_overlayLayer  = 0;
    g_overlayAsset.assign(selectedAssetId.data(), selectedAssetId.size());
}

void selectOverlayLibraryAsset(std::string_view assetId)
{
    if (g_inspectorKind == InspectorKind::OverlayLibrary)
    {
        g_overlayAsset.assign(assetId.data(), assetId.size());
    }
}

void closeInspector()
{
    g_inspectorKind = InspectorKind::Stats;
    g_inspected     = nullptr;
    g_overlayLayer  = 0;
    g_overlayAsset.clear();
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

void validateInspector(const EffectChain* chain, const OverlayPanelSnapshot* overlays)
{
    if (g_inspectorKind == InspectorKind::Stats)
    {
        return;
    }

    if (g_inspectorKind == InspectorKind::Effect)
    {
        if (!g_inspected || !chain)
        {
            closeInspector();
            return;
        }

        for (std::size_t i = 0; i < chain->size(); ++i)
        {
            if (&chain->at(i) == g_inspected)
            {
                return;
            }
        }
        closeInspector();
        return;
    }

    if (!overlays)
    {
        closeInspector();
        return;
    }

    if (g_inspectorKind == InspectorKind::OverlayLayer)
    {
        if (overlays->layers)
        {
            for (const OverlayLayerUiState& layer : *overlays->layers)
            {
                if (layer.id == g_overlayLayer) return;
            }
        }
        closeInspector();
        return;
    }

    // A library with no selected asset is still a valid face. If a remove or
    // rescan invalidates only the selection, keep the operator in the library
    // and return its detail pane to the empty state.
    if (g_overlayAsset.empty()) return;
    if (overlays->assets)
    {
        for (const OverlayAssetUiState& asset : *overlays->assets)
        {
            if (asset.id == g_overlayAsset) return;
        }
    }
    g_overlayAsset.clear();
}

int inspectorColumns(float width)
{
    const float usable = width - ImGui::GetStyle().WindowPadding.x * 2.0f;
    const int   fit    = static_cast<int>(usable / theme::scaled(kMinColumnWidth));
    return std::clamp(fit, 1, kMaxColumns);
}

float inspectorHeight(float width, float minimum, float maximum)
{
    if (g_inspectorKind == InspectorKind::Stats)
    {
        return minimum;
    }
    if (g_inspectorKind == InspectorKind::OverlayLibrary)
    {
        return maximum;
    }
    if (g_inspectorKind == InspectorKind::OverlayLayer)
    {
        return std::clamp(theme::scaled(300.0f), minimum, maximum);
    }
    if (!g_inspected) return minimum;

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
    // slider of the tallest column. Framing adds the COMPOSE pad above the
    // columns; leave it out and the last slider disappears again.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float compose =
        g_inspected->role() == EffectRole::Framing ? framingComposePadHeight() : 0.0f;
    const float wanted = style.WindowPadding.y * 2.0f + style.CellPadding.y * 2.0f +
                         headerHeight() + compose + tallest;
    return std::clamp(wanted, minimum, maximum);
}

} // namespace ui

void drawInspectorPanel(UiFrameState& state)
{
    if (ui::inspectorKind() == ui::InspectorKind::OverlayLayer ||
        ui::inspectorKind() == ui::InspectorKind::OverlayLibrary)
    {
        drawOverlayInspectorPanel(state);
        return;
    }

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
    if (effect->role() == EffectRole::Framing)
    {
        drawFramingComposePad(*effect);
    }
    drawParametersColumns(effect->parameters(), effect->descriptor().typeId.c_str(),
                          ui::inspectorColumns(ImGui::GetWindowWidth()), true);
    ImGui::PopID();
}

} // namespace atemfx
