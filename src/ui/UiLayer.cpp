#include "ui/UiLayer.h"

#include <algorithm>
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

// Header row plus the gap drawPanelHeader leaves under the label. Used when
// SOURCE / OUTPUT are folded so EFFECTS inherits the freed space.
float collapsedPanelHeight()
{
    const float pad    = theme::scaled(4.0f);
    const float header = ImGui::GetTextLineHeight() + 2.0f * pad;
    return ImGui::GetStyle().WindowPadding.y * 2.0f + header + theme::scaled(8.0f);
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
        return collapsedPanelHeight();
    }

    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float text  = ImGui::GetTextLineHeightWithSpacing();

    // Header, then one input row — the combo and its rescan share it — then
    // source and tracking status, which wrap to two lines often enough to
    // budget for it.
    float height = collapsedPanelHeight() + frame + text * 4.0f;
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
    const float folded = collapsedPanelHeight();
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
        const float progress =
            headerMode == ProgramMode::Clean ? 1.0f - state.programMix : state.programMix;
        std::snprintf(modeLabel, sizeof(modeLabel), "PROGRAM  %s %.0f%%",
                      programModeName(headerMode), static_cast<double>(progress) * 100.0);
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
    const bool source  = theme::panelOpen(theme::PanelSection::Source);
    const bool output  = theme::panelOpen(theme::PanelSection::Output);
    const bool effects = theme::panelOpen(theme::PanelSection::Effects);

    // Opening another section, or folding EFFECTS away, is the operator saying
    // they are done with the effect: the panel under the preview goes back to
    // the numbers, which is what it shows by default.
    const bool leftEffects  = effectsOpen_ && !effects;
    const bool tookOverSource = !sourceOpen_ && source;
    const bool tookOverOutput = !outputOpen_ && output;
    if (leftEffects || tookOverSource || tookOverOutput)
    {
        ui::closeInspector();
    }

    sourceOpen_  = source;
    outputOpen_  = output;
    effectsOpen_ = effects;
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

    // Before anything reads the inspected effect, including the layout pass
    // below: the chain can lose an effect between frames.
    ui::validateInspector(state.chain);
    syncInspectorToSections();

    const float headerHeight = theme::scaled(kHeaderHeight);
    const bool  rail         = theme::sidebarCollapsed();
    const float leftWidth    = theme::scaled(rail ? theme::sidebarRailWidth() : kLeftWidth);
    const float statsHeight  = theme::scaled(kStatsHeight);
    const float outputHeight  = rail ? 0.0f : std::round(outputPanelHeight(state));
    const float programHeight = rail ? 0.0f : std::round(programPanelHeight());

    const float rightWidth = size.x - leftWidth;
    const float bodyHeight = size.y - headerHeight;
    // Asked for before the panels draw, so a selection made this frame moves
    // the split on the next one. A frame of lag on a panel edge is invisible;
    // measuring it twice would not be.
    const float inspectorHeight =
        std::round(ui::inspectorHeight(rightWidth, statsHeight,
                                       std::max(statsHeight, bodyHeight * kInspectorBodyShare)));
    // Long source diagnostics and person lists scroll in their own panel;
    // they cannot push routing or the effect list off-screen.
    // What EFFECTS keeps no matter how much SOURCE would like: its rack label,
    // the Add effect button and a few chain rows. It used to be 240 because
    // EFFECTS also carried the PARAMETERS section; that moved to the inspector
    // under the preview, so the reserve came down and SOURCE got the space
    // back - which is what pays for the rules between its parameters.
    const float sourceLimit = std::max(collapsedPanelHeight(),
                                       bodyHeight - programHeight - outputHeight -
                                           theme::scaled(180.0f));
    const float sourceHeight = std::round(std::min(sourcePanelHeight(state), sourceLimit));
    const float previewH   = bodyHeight - inspectorHeight;
    const float effectsH   = bodyHeight - programHeight - sourceHeight - outputHeight;

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
        ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, programHeight));
        if (ImGui::Begin("##program", nullptr, kPanelFlags | ImGuiWindowFlags_NoScrollbar))
        {
            drawProgramPanel(state);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight + programHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, sourceHeight));
        if (ImGui::Begin("##source", nullptr, kPanelFlags))
        {
            drawSourcePanel(state);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(
            ImVec2(origin.x, origin.y + headerHeight + programHeight + sourceHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, outputHeight));
        if (ImGui::Begin("##output", nullptr, kPanelFlags))
        {
            drawOutputPanel(state);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(origin.x,
                                       origin.y + headerHeight + programHeight + sourceHeight +
                                           outputHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, effectsH));
        if (ImGui::Begin("##effects", nullptr, kPanelFlags))
        {
            drawEffectsPanel(state);
        }
        ImGui::End();
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
