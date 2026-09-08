#include "ui/UiLayer.h"

#include <cmath>
#include <cstdio>

#include "effects/EffectParameters.h"
#include "imgui.h"
#include "ui/Fonts.h"
#include "ui/Theme.h"
#include "video/FrameTiming.h"
#include "video/VideoSource.h"

namespace atemfx {

namespace {

// Authored at 100%. theme::scaled() applies the platform scale at use, so a
// Windows machine at 150% gets panels that grow with the font rather than
// clipping it.
constexpr float kHeaderHeight = 48.0f;
constexpr float kLeftWidth    = 392.0f;
constexpr float kStatsHeight  = 204.0f;

// SOURCE and OUTPUT are sized from what they actually contain rather than
// from a constant. A fixed height only holds until the font changes or an
// input arrives with a different number of parameters, and then it silently
// hides the controls at the bottom of the panel.
float parameterRows(const ParameterSet& parameters)
{
    float rows = 0.0f;
    for (const Parameter& parameter : parameters.all())
    {
        // A boolean rides on its label row; everything else adds a control row.
        rows += parameter.type == ParameterType::Bool ? 1.0f : 2.0f;
    }
    return rows;
}

// Header row plus the gap drawPanelHeader leaves under the label. Used when
// SOURCE / OUTPUT are folded so EFFECTS inherits the freed space.
float collapsedPanelHeight()
{
    const float pad    = theme::scaled(4.0f);
    const float header = ImGui::GetTextLineHeight() + 2.0f * pad;
    return ImGui::GetStyle().WindowPadding.y * 2.0f + header + theme::scaled(8.0f);
}

float sourcePanelHeight(const UiFrameState& state)
{
    if (!theme::panelOpen(theme::PanelSection::Source))
    {
        return collapsedPanelHeight();
    }

    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float text  = ImGui::GetTextLineHeightWithSpacing();

    // Panel header, input combo, rescan button, then the source and tracking
    // status lines, which wrap to two lines often enough to budget for it.
    float height = ImGui::GetStyle().WindowPadding.y * 2.0f + frame * 3.0f + text * 4.0f;
    if (state.source)
    {
        height += frame * parameterRows(state.source->parameters());
    }
    return height;
}

float outputPanelHeight(const UiFrameState& state)
{
    if (!theme::panelOpen(theme::PanelSection::Output))
    {
        return collapsedPanelHeight();
    }

    const float frame = ImGui::GetFrameHeightWithSpacing();
    const float text  = ImGui::GetTextLineHeightWithSpacing();

    // Header, display combo, the stop/rescan button row, and the status line.
    // A live send replaces that one line with two plus a wrapped message.
    float height = ImGui::GetStyle().WindowPadding.y * 2.0f + frame * 3.0f + text;
    height += state.outputActive ? text * 3.0f : text;
    return height;
}

constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoSavedSettings;

void drawHeader(UiFrameState& state, ImVec2 origin, ImVec2 size)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(ImVec2(origin.x, origin.y + size.y - 1.0f),
                      ImVec2(origin.x + size.x, origin.y + size.y - 1.0f),
                      theme::kLineU32);

    const float  glyphSize = theme::scaled(22.0f);
    const ImVec2 glyphCentre(origin.x + theme::scaled(22.0f), origin.y + size.y * 0.5f);
    theme::drawBrandGlyph(drawList, glyphCentre, glyphSize);

    ImGui::SetCursorPos(ImVec2(theme::scaled(40.0f),
                               (size.y - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted("CamVJ");

    char meta[96];
    std::snprintf(meta, sizeof(meta), "%ux%u    %s",
                  state.processingWidth, state.processingHeight, state.backendName);
    ImGui::SameLine(0.0f, theme::scaled(16.0f));
    ui::pushMono();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::rackGrey);
    ImGui::TextUnformatted(meta);
    ImGui::PopStyleColor();
    ui::popMono();

    const float fps = state.timing ? state.timing->fps() : 0.0f;
    const float frameMs = state.timing ? state.timing->averageFrameMs() : 0.0f;
    char right[48];
    if (state.timing)
    {
        std::snprintf(right, sizeof(right), "%.1f fps", fps);
    }
    else
    {
        std::snprintf(right, sizeof(right), "-- fps");
    }

    ui::pushMono();
    const float badgeWidth =
        ImGui::CalcTextSize(state.outputActive ? "LIVE" : "IDLE").x + theme::scaled(22.0f);
    const float fpsWidth = ImGui::CalcTextSize(right).x;
    const float gap      = theme::scaled(16.0f);
    ImGui::SameLine();
    ImGui::SetCursorPosX(size.x - badgeWidth - fpsWidth - gap - theme::scaled(14.0f));
    ImGui::SetCursorPosY((size.y - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, state.timing ? theme::budgetColour(frameMs) : theme::rackGrey);
    ImGui::TextUnformatted(right);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, gap);
    theme::drawLiveBadge(state.outputActive);
    ui::popMono();
}

} // namespace

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

    const float headerHeight = theme::scaled(kHeaderHeight);
    const float leftWidth    = theme::scaled(kLeftWidth);
    const float statsHeight  = theme::scaled(kStatsHeight);
    const float sourceHeight = std::round(sourcePanelHeight(state));
    const float outputHeight = std::round(outputPanelHeight(state));

    const float rightWidth = size.x - leftWidth;
    const float bodyHeight = size.y - headerHeight;
    const float previewH   = bodyHeight - statsHeight;
    const float effectsH   = bodyHeight - sourceHeight - outputHeight;

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(ImVec2(size.x, headerHeight));
    if (ImGui::Begin("##header", nullptr, kPanelFlags | ImGuiWindowFlags_NoScrollbar))
    {
        drawHeader(state, origin, ImVec2(size.x, headerHeight));
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, sourceHeight));
    if (ImGui::Begin("##source", nullptr, kPanelFlags))
    {
        drawSourcePanel(state);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y + headerHeight + sourceHeight));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, outputHeight));
    if (ImGui::Begin("##output", nullptr, kPanelFlags))
    {
        drawOutputPanel(state);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(
        ImVec2(origin.x, origin.y + headerHeight + sourceHeight + outputHeight));
    ImGui::SetNextWindowSize(ImVec2(leftWidth, effectsH));
    if (ImGui::Begin("##effects", nullptr, kPanelFlags))
    {
        drawEffectsPanel(state);
    }
    ImGui::End();

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
    ImGui::SetNextWindowSize(ImVec2(rightWidth, statsHeight));
    if (ImGui::Begin("##stats", nullptr, kPanelFlags))
    {
        drawStatsPanel(state);
    }
    ImGui::End();
}

} // namespace atemfx
