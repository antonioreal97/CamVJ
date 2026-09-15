#include "ui/UiLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "core/Version.h"
#include "effects/EffectParameters.h"
#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Inspector.h"
#include "ui/Theme.h"
#include "video/FrameTiming.h"
#include "video/VideoSource.h"

namespace atemfx {

namespace {

// Authored at 100%. theme::scaled() applies the platform scale at use, so a
// Windows machine at 150% gets panels that grow with the font rather than
// clipping it.
constexpr float kHeaderHeight = 48.0f;
constexpr float kLeftWidth    = 392.0f;  // expanded; collapsed uses theme::sidebarRailWidth()
constexpr float kStatsHeight  = 204.0f;

// The parameters face of the bottom panel may take more than the stats strip,
// never more than this much of the body: the preview is what the operator is
// judging the effect on, and an inspector that swallows it defeats itself.
constexpr float kInspectorBodyShare = 0.45f;

// SOURCE and OUTPUT are sized from what they actually contain rather than
// from a constant. A fixed height only holds until the font changes or an
// input arrives with a different number of parameters, and then it silently
// hides the controls at the bottom of the panel.
//
// Must agree with drawParameterRow: a boolean rides on its label row,
// everything else adds a control row, each row ends with a Spacing, and every
// parameter but the first is preceded by a rule. Counting the rows and
// forgetting the rules is what cut the last slider off the SOURCE panel.
float parameterBlockHeight(const ParameterSet& parameters)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float       frame = ImGui::GetFrameHeightWithSpacing();

    float height = 0.0f;
    bool  first  = true;
    for (const Parameter& parameter : parameters.all())
    {
        if (!first)
        {
            height += 1.0f + style.ItemSpacing.y * 2.0f;
        }
        height += parameter.type == ParameterType::Bool ? frame : frame * 2.0f;
        height += style.ItemSpacing.y;
        first = false;
    }
    return height;
}

// One action row: the button plus the spacing that follows it.
float actionRow()
{
    return theme::actionHeight() + ImGui::GetStyle().ItemSpacing.y;
}

float sourcePanelHeight(const UiFrameState& state)
{
    if (!theme::panelOpen(theme::PanelSection::Source))
    {
        return theme::foldedPanelHeight();
    }

    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float text  = ImGui::GetTextLineHeightWithSpacing();

    // Header, then one input row — the combo and its rescan share it — then
    // source and tracking status, which wrap to two lines often enough to
    // budget for it.
    float height = theme::foldedPanelHeight() + frame + text * 4.0f;
    if (state.trackingAvailable)
    {
        // Pick UI: the Pick subject button, or a cancel row plus a short
        // person list once the operator is choosing.
        height += actionRow() + text * 4.0f;
    }
    if (state.source)
    {
        height += parameterBlockHeight(state.source->parameters());
    }
    return height;
}

// PROGRAM has no fold of its own: what goes to air must be one reach away at
// any time, so the panel is always exactly its caption plus the mode row.
float programPanelHeight()
{
    return ImGui::GetStyle().WindowPadding.y * 2.0f + programControlsHeight();
}

float outputPanelHeight(const UiFrameState& state)
{
    const float folded = theme::foldedPanelHeight();
    if (!theme::panelOpen(theme::PanelSection::Output))
    {
        return folded;
    }

    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float text  = ImGui::GetTextLineHeightWithSpacing();

    // Display row, the stop button, then status. A live send replaces that one
    // line with two plus a wrapped message.
    float height = folded + frame + actionRow();
    height += state.outputActive ? text * 3.0f : text;

    // Webcam: its group label, its button, and two lines of state — a live
    // send shows counters, a failed start shows why it failed.
    height += text + actionRow() + text * 2.0f;
    return height;
}

float presetsPanelHeight()
{
    if (!theme::panelOpen(theme::PanelSection::Presets))
    {
        return theme::foldedPanelHeight();
    }

    const float text = ImGui::GetTextLineHeightWithSpacing();
    // Header, FACTORY label + two rows of three, USER label + one empty line,
    // SAVE label + name field + save button.
    return theme::foldedPanelHeight() + text * 3.0f + actionRow() * 4.0f +
           ImGui::GetFrameHeightWithSpacing() + theme::scaled(12.0f);
}

struct SidebarHeights
{
    float source   = 0.0f;
    float output   = 0.0f;
    float presets  = 0.0f;
    float overlays = 0.0f;
    float effects  = 0.0f;
};

SidebarHeights allocateSidebarHeights(const UiFrameState& state, float bodyHeight,
                                      float programHeight)
{
    constexpr std::size_t kEffects = 4;
    const float folded = theme::foldedPanelHeight();
    const std::array<float, 5> wanted = {
        sourcePanelHeight(state),
        outputPanelHeight(state),
        presetsPanelHeight(),
        overlaysPanelHeight(),
        theme::panelOpen(theme::PanelSection::Effects) ? theme::scaled(180.0f) : folded,
    };

    std::array<float, 5> heights{};
    heights.fill(folded);

    const float available = std::max(0.0f, bodyHeight - programHeight);
    float wantedTotal = 0.0f;
    for (float height : wanted) wantedTotal += height;
    const float minimumTotal = folded * static_cast<float>(heights.size());

    if (available >= wantedTotal)
    {
        heights = wanted;
        // EFFECTS remains the elastic tail of the rack at comfortable window
        // sizes, preserving the long chain view operators already have.
        heights[kEffects] += available - wantedTotal;
    }
    else if (available > minimumTotal && wantedTotal > minimumTotal)
    {
        const float share = (available - minimumTotal) / (wantedTotal - minimumTotal);
        for (std::size_t i = 0; i < heights.size(); ++i)
        {
            heights[i] = folded + (wanted[i] - folded) * share;
        }
    }
    // If even five folded headers do not fit (for example 960x600 at 200%
    // Windows DPI), keep their real hit height. The containing sidebar scrolls
    // as one rack, so every section remains reachable without overlap.

    return {heights[0], heights[1], heights[2], heights[3], heights[4]};
}

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoSavedSettings;

void drawSendBadge(bool sending)
{
    const char* label = sending ? "SEND" : "OFF";
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetTextLineHeight();
    const float gap = theme::scaled(14.0f);
    const ImU32 colour = sending ? theme::kSplitMagentaU32 : theme::kRackGreyU32;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(ImVec2(origin.x + theme::scaled(4.0f), origin.y + height * 0.5f),
                              theme::scaled(4.0f), colour);
    drawList->AddText(ImVec2(origin.x + gap, origin.y), colour, label);
    ImGui::Dummy(ImVec2(gap + ImGui::CalcTextSize(label).x, height));
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", sending ? "Output is open. Input health is shown in SOURCE."
                                         : "No display output is open.");
    }
}

void drawHeader(UiFrameState& state, ImVec2 origin, ImVec2 size)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(ImVec2(origin.x, origin.y + size.y - 1.0f),
                      ImVec2(origin.x + size.x, origin.y + size.y - 1.0f),
                      theme::kLineU32);

    const float chevronSize = theme::scaled(22.0f);
    const float chevronX    = theme::scaled(8.0f);
    ImGui::SetCursorPos(ImVec2(chevronX, (size.y - chevronSize) * 0.5f));
    const bool collapsed = theme::sidebarCollapsed();
    if (theme::glyphButton("##sidebar",
                           collapsed ? theme::Glyph::Expand : theme::Glyph::Collapse, chevronSize,
                           collapsed ? "Show the control column"
                                     : "Hide the control column so SOURCE and PROGRAM can grow"))
    {
        theme::setSidebarCollapsed(!collapsed);
    }

    const float  glyphSize = theme::scaled(22.0f);
    const float  glyphX    = chevronX + chevronSize + theme::scaled(8.0f);
    const ImVec2 glyphCentre(origin.x + glyphX + glyphSize * 0.5f, origin.y + size.y * 0.5f);
    theme::drawBrandGlyph(drawList, glyphCentre, glyphSize);

    ImGui::SetCursorPos(ImVec2(glyphX + glyphSize + theme::scaled(6.0f),
                               (size.y - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted("CamVJ");

    // Version first in the grey strip: the operator reads it off the screen
    // when someone asks which build is on the machine.
    char meta[96];
    std::snprintf(meta, sizeof(meta), "v%s    %ux%u    %s",
                  kVersion, state.processingWidth, state.processingHeight, state.backendName);
    ImGui::SameLine(0.0f, theme::scaled(16.0f));
    ui::pushMono();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted(meta);
    ImGui::PopStyleColor();
    ui::popMono();

    if (state.operationLocked)
    {
        ImGui::SameLine(0.0f, theme::scaled(24.0f));
        ImGui::SetCursorPosY((size.y - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::Checkbox("Operation lock", state.operationLocked);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Protect routing and chain structure. Parameters, Pick and program modes stay available. Escape still closes output.");
        }
    }

    const float fps = state.timing ? state.timing->fps() : 0.0f;
    const float frameMs = state.timing ? state.timing->averageFrameMs() : 0.0f;
    char right[48];
    if (state.timing)
    {
        std::snprintf(right, sizeof(right), "%.1f render fps", fps);
    }
    else
    {
        std::snprintf(right, sizeof(right), "-- render fps");
    }

    ui::pushMono();
    const ProgramMode headerMode = state.programMode ? *state.programMode : ProgramMode::Effects;
    char modeLabel[32];
    if (state.programMixing)
    {
        std::snprintf(modeLabel, sizeof(modeLabel), "PROGRAM  %s %.0f%%",
                      programModeName(headerMode),
                      static_cast<double>(state.programProgress) * 100.0);
    }
    else
    {
        std::snprintf(modeLabel, sizeof(modeLabel), "PROGRAM  %s", programModeName(headerMode));
    }
    const float badgeWidth =
        ImGui::CalcTextSize(state.outputActive ? "SEND" : "OFF").x + theme::scaled(14.0f);
    const float fpsWidth = ImGui::CalcTextSize(right).x;
    const float modeWidth = ImGui::CalcTextSize(modeLabel).x;
    const float gap      = theme::scaled(16.0f);
    ImGui::SameLine();
    ImGui::SetCursorPosX(size.x - badgeWidth - fpsWidth - modeWidth - gap * 2.0f - theme::scaled(14.0f));
    ImGui::SetCursorPosY((size.y - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextColored(theme::splitMagenta, "%s", modeLabel);
    ImGui::SameLine(0.0f, gap);
    ImGui::PushStyleColor(ImGuiCol_Text, state.timing ? theme::budgetColour(frameMs) : theme::rackGrey);
    ImGui::TextUnformatted(right);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, gap);
    drawSendBadge(state.outputActive);
    ui::popMono();
}

} // namespace

void UiLayer::syncInspectorToSections()
{
    const bool source   = theme::panelOpen(theme::PanelSection::Source);
    const bool output   = theme::panelOpen(theme::PanelSection::Output);
    const bool presets  = theme::panelOpen(theme::PanelSection::Presets);
    const bool overlays = theme::panelOpen(theme::PanelSection::Overlays);
    const bool effects  = theme::panelOpen(theme::PanelSection::Effects);

    const ui::InspectorKind inspector = ui::inspectorKind();
    const bool overlayInspector = inspector == ui::InspectorKind::OverlayLayer ||
                                  inspector == ui::InspectorKind::OverlayLibrary;
    const bool effectInspector = inspector == ui::InspectorKind::Effect;

    // Folding the owner closes its inspector. Opening any routing/look panel
    // also returns to stats; EFFECTS and OVERLAYS take ownership from one
    // another when their rack header is reopened.
    const bool leftEffects     = effectsOpen_ && !effects && effectInspector;
    const bool leftOverlays    = overlaysOpen_ && !overlays && overlayInspector;
    const bool tookOverSource  = !sourceOpen_ && source;
    const bool tookOverOutput  = !outputOpen_ && output;
    const bool tookOverPresets = !presetsOpen_ && presets;
    const bool tookOverEffects = !effectsOpen_ && effects && overlayInspector;
    const bool tookOverOverlays = !overlaysOpen_ && overlays && effectInspector;
    if (leftEffects || leftOverlays || tookOverSource || tookOverOutput ||
        tookOverPresets || tookOverEffects || tookOverOverlays)
    {
        ui::closeInspector();
    }

    sourceOpen_   = source;
    outputOpen_   = output;
    presetsOpen_  = presets;
    overlaysOpen_ = overlays;
    effectsOpen_  = effects;
}

void UiLayer::configure(const ui::DisplayScale& scale)
{
    ImGuiIO& io = ImGui::GetIO();

    // The layout is computed every frame, so there is nothing worth persisting
    // and an imgui.ini would only get in the way.
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    scale_ = scale;
    theme::applyStyle(scale.contentScale);
    ui::loadFonts(scale);
}

bool UiLayer::updateScale(const ui::DisplayScale& scale)
{
    if (scale == scale_)
    {
        return false;
    }

    scale_ = scale;
    theme::applyStyle(scale.contentScale);
    ui::loadFonts(scale);
    return true;
}

void UiLayer::draw(UiFrameState& state)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2         origin   = viewport->Pos;
    const ImVec2         size     = viewport->Size;

    // Before the layout reads the current face: a chain can lose an effect and
    // an asynchronous library commit can replace the overlay vectors between
    // frames. Both selections are revalidated against stable ownership here.
    ui::validateInspector(state.chain, state.overlays);
    syncInspectorToSections();

    const float headerHeight = theme::scaled(kHeaderHeight);
    const bool  rail         = theme::sidebarCollapsed();
    const float leftWidth    = theme::scaled(rail ? theme::sidebarRailWidth() : kLeftWidth);
    const float statsHeight  = theme::scaled(kStatsHeight);
    const float programHeight = rail ? 0.0f : std::round(programPanelHeight());

    const float rightWidth = size.x - leftWidth;
    const float bodyHeight = size.y - headerHeight;
    // Asked for before the panels draw, so a selection made this frame moves
    // the split on the next one. A frame of lag on a panel edge is invisible;
    // measuring it twice would not be.
    const float inspectorHeight =
        std::round(ui::inspectorHeight(rightWidth, statsHeight,
                                       std::max(statsHeight, bodyHeight * kInspectorBodyShare)));
    const SidebarHeights sidebar = rail
        ? SidebarHeights{}
        : allocateSidebarHeights(state, bodyHeight, programHeight);
    const float previewH = std::max(0.0f, bodyHeight - inspectorHeight);

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(size.x, headerHeight));
    if (ImGui::Begin("##header", nullptr, kPanelFlags | ImGuiWindowFlags_NoScrollbar))
    {
        drawHeader(state, origin, ImVec2(size.x, headerHeight));
    }
    ImGui::End();

    if (rail)
    {
        ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, theme::scaled(8.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##sidebar_rail", nullptr, kPanelFlags | ImGuiWindowFlags_NoScrollbar))
        {
            theme::drawSidebarRail(state.outputActive);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    else
    {
        // One scrollable rack contains individually scrollable sections. At
        // normal sizes the allocator deals the exact body height and the outer
        // rack never moves. At very short/high-DPI sizes it carries the real
        // folded hit height for every section and becomes the safe fallback.
        ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, bodyHeight));
        const ImVec2 sectionPadding = ImGui::GetStyle().WindowPadding;
        const ImVec2 sectionSpacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("##sidebar", nullptr, kPanelFlags))
        {
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 extent = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(min.x + extent.x - 1.0f, min.y),
                ImVec2(min.x + extent.x - 1.0f, min.y + extent.y), theme::kLineU32);

            auto drawSection = [&](const char* id, float height, bool fixed,
                                   auto&& drawContents) {
                const ImGuiWindowFlags flags = fixed ? ImGuiWindowFlags_NoScrollbar
                                                     : ImGuiWindowFlags_None;
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sectionPadding);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, sectionSpacing);
                if (ImGui::BeginChild(id, ImVec2(0.0f, height),
                                      ImGuiChildFlags_AlwaysUseWindowPadding, flags))
                {
                    drawContents();
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
            };

            drawSection("##program", programHeight, true, [&] { drawProgramPanel(state); });
            drawSection("##source", sidebar.source, false, [&] { drawSourcePanel(state); });
            drawSection("##output", sidebar.output, false, [&] { drawOutputPanel(state); });
            drawSection("##presets", sidebar.presets, false, [&] { drawPresetsPanel(state); });
            drawSection("##overlays", sidebar.overlays, false,
                        [&] { drawOverlaysPanel(state); });
            drawSection("##effects", sidebar.effects, false, [&] { drawEffectsPanel(state); });
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    ImGui::SetNextWindowPos(ImVec2(origin.x + leftWidth, origin.y + headerHeight));
    ImGui::SetNextWindowSize(ImVec2(rightWidth, previewH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::previewBlack);
    if (ImGui::Begin("##preview", nullptr, kPanelFlags | ImGuiWindowFlags_NoScrollbar))
    {
        drawPreviewPanel(state);
    }
    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(ImVec2(origin.x + leftWidth, origin.y + headerHeight + previewH));
    ImGui::SetNextWindowSize(ImVec2(rightWidth, inspectorHeight));
    if (ImGui::Begin("##inspector", nullptr, kPanelFlags))
    {
        drawInspectorPanel(state);
    }
    ImGui::End();
}

} // namespace atemfx
