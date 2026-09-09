#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "ui/Fonts.h"

namespace atemfx {
namespace theme {

namespace {

float g_contentScale = 1.0f;

} // namespace

float scale()
{
    return g_contentScale;
}

float scaled(float value)
{
    return std::round(value * g_contentScale);
}

void applyStyle(float contentScale)
{
    g_contentScale = contentScale > 0.0f ? contentScale : 1.0f;

    // Reset first. applyStyle runs again whenever the window changes display,
    // and ScaleAllSizes below multiplies every size in the struct — including
    // the ones this function does not set. Without the reset those compound.
    ImGuiStyle& style = ImGui::GetStyle();
    style             = ImGuiStyle();

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.GrabRounding      = 2.0f;
    style.PopupRounding     = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(12.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 12.0f;
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 12.0f;
    style.DisabledAlpha     = 0.45f;

    ImVec4* colours = style.Colors;
    colours[ImGuiCol_Text]                  = keyLight;
    colours[ImGuiCol_TextDisabled]          = rackGrey;
    colours[ImGuiCol_WindowBg]              = studioBlack;
    colours[ImGuiCol_ChildBg]               = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colours[ImGuiCol_PopupBg]               = surface;
    colours[ImGuiCol_Border]                = line;
    colours[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colours[ImGuiCol_FrameBg]               = surface;
    colours[ImGuiCol_FrameBgHovered]        = surfaceHover;
    colours[ImGuiCol_FrameBgActive]         = surfaceActive;
    colours[ImGuiCol_TitleBg]               = studioBlack;
    colours[ImGuiCol_TitleBgActive]         = studioBlack;
    colours[ImGuiCol_TitleBgCollapsed]      = studioBlack;
    colours[ImGuiCol_MenuBarBg]             = studioBlack;
    colours[ImGuiCol_ScrollbarBg]           = studioBlack;
    colours[ImGuiCol_ScrollbarGrab]         = line;
    colours[ImGuiCol_ScrollbarGrabHovered]  = rackGrey;
    colours[ImGuiCol_ScrollbarGrabActive]   = splitCyan;
    colours[ImGuiCol_CheckMark]             = splitCyan;
    colours[ImGuiCol_SliderGrab]            = splitCyan;
    colours[ImGuiCol_SliderGrabActive]      = keyLight;
    colours[ImGuiCol_Button]                = surface;
    colours[ImGuiCol_ButtonHovered]         = surfaceHover;
    colours[ImGuiCol_ButtonActive]          = surfaceActive;
    colours[ImGuiCol_Header]                = surface;
    colours[ImGuiCol_HeaderHovered]         = surfaceHover;
    colours[ImGuiCol_HeaderActive]          = surfaceActive;
    colours[ImGuiCol_Separator]             = line;
    colours[ImGuiCol_SeparatorHovered]      = splitCyan;
    colours[ImGuiCol_SeparatorActive]       = splitCyan;
    colours[ImGuiCol_ResizeGrip]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colours[ImGuiCol_ResizeGripHovered]     = splitCyanDim;
    colours[ImGuiCol_ResizeGripActive]      = splitCyan;
    colours[ImGuiCol_Tab]                       = surface;
    colours[ImGuiCol_TabHovered]                = surfaceHover;
    colours[ImGuiCol_TabSelected]               = surfaceActive;
    colours[ImGuiCol_TabSelectedOverline]       = splitCyan;
    colours[ImGuiCol_TabDimmed]                 = studioBlack;
    colours[ImGuiCol_TabDimmedSelected]         = surface;
    colours[ImGuiCol_TabDimmedSelectedOverline] = line;
    colours[ImGuiCol_PlotLines]             = splitCyan;
    colours[ImGuiCol_PlotLinesHovered]      = tungsten;
    colours[ImGuiCol_PlotHistogram]         = splitCyan;
    colours[ImGuiCol_PlotHistogramHovered]  = tungsten;
    colours[ImGuiCol_TableHeaderBg]         = surface;
    colours[ImGuiCol_TableBorderStrong]     = line;
    colours[ImGuiCol_TableBorderLight]      = line;
    colours[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colours[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    colours[ImGuiCol_TextSelectedBg]        = splitCyanDim;
    colours[ImGuiCol_DragDropTarget]        = splitCyan;
    colours[ImGuiCol_NavHighlight]          = splitCyan;
    colours[ImGuiCol_NavWindowingHighlight] = splitCyan;
    colours[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    colours[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    // Sizes above are authored at 100%. Apply the platform's scale last so
    // there is exactly one place where it happens.
    style.ScaleAllSizes(g_contentScale);
}

ImVec4 budgetColour(float milliseconds)
{
    if (milliseconds > kFrameBudgetMs)
    {
        return splitMagenta;
    }
    if (milliseconds > kFrameBudgetMs * 0.5f)
    {
        return tungsten;
    }
    return splitCyan;
}

void drawBrandGlyph(ImDrawList* drawList, ImVec2 centre, float size)
{
    const float inset = size * 0.10f;
    const float arm   = size * 0.22f;
    const float thick = std::max(scaled(1.5f), size * 0.07f);
    const float x0    = centre.x - size * 0.5f + inset;
    const float y0    = centre.y - size * 0.5f + inset;
    const float x1    = centre.x + size * 0.5f - inset;
    const float y1    = centre.y + size * 0.5f - inset;

    drawList->AddLine(ImVec2(x0, y0 + arm), ImVec2(x0, y0), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x0, y0), ImVec2(x0 + arm, y0), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x1 - arm, y0), ImVec2(x1, y0), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x1, y0), ImVec2(x1, y0 + arm), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x1, y1 - arm), ImVec2(x1, y1), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x1, y1), ImVec2(x1 - arm, y1), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x0 + arm, y1), ImVec2(x0, y1), kKeyLightU32, thick);
    drawList->AddLine(ImVec2(x0, y1), ImVec2(x0, y1 - arm), kKeyLightU32, thick);

    const float subjectRadius = size * 0.055f;
    const float headroom      = size * 0.08f;
    drawList->AddCircleFilled(ImVec2(centre.x, centre.y - headroom), subjectRadius, kTungstenU32);
}

namespace {

bool g_panelOpen[]       = {true, true, true};
bool g_sidebarCollapsed = false;

void drawChevron(ImDrawList* drawList, ImVec2 centre, bool open, ImU32 colour)
{
    const float s = scaled(3.5f);
    if (open)
    {
        drawList->AddTriangleFilled(ImVec2(centre.x - s, centre.y - s * 0.45f),
                                    ImVec2(centre.x + s, centre.y - s * 0.45f),
                                    ImVec2(centre.x, centre.y + s * 0.65f),
                                    colour);
    }
    else
    {
        drawList->AddTriangleFilled(ImVec2(centre.x - s * 0.45f, centre.y - s),
                                    ImVec2(centre.x + s * 0.65f, centre.y),
                                    ImVec2(centre.x - s * 0.45f, centre.y + s),
                                    colour);
    }
}

void drawRailItem(const char* title, ImU32 accent, int sectionIndex)
{
    ui::pushMono();
    ImGui::PushID(title);

    ImDrawList*  drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    const float  width    = ImGui::GetContentRegionAvail().x;
    const float  pad      = scaled(6.0f);
    const float  line     = ImGui::GetTextLineHeight();

    int letters = 0;
    for (const char* p = title; *p != '\0'; ++p)
    {
        ++letters;
    }

    const float  height = pad * 2.0f + line * static_cast<float>(letters);
    const ImVec2 max(origin.x + width, origin.y + height);

    ImGui::InvisibleButton("##open", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
    {
        g_sidebarCollapsed = false;
        if (sectionIndex >= 0)
        {
            g_panelOpen[sectionIndex] = true;
        }
    }
    if (hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Open %s", title);
    }

    if (hovered)
    {
        drawList->AddRectFilled(origin, max, kLineU32);
    }
    drawList->AddRectFilled(origin, ImVec2(origin.x + scaled(3.0f), max.y), accent);
    drawList->AddLine(ImVec2(origin.x, max.y), max, kLineU32, 1.0f);

    const ImU32 labelColour = hovered ? kKeyLightU32 : kRackGreyU32;
    const float glyphWidth  = ImGui::CalcTextSize("M").x;
    const float textX       = origin.x + (width - glyphWidth) * 0.5f;
    float       textY       = origin.y + pad;
    for (const char* p = title; *p != '\0'; ++p)
    {
        const char glyph[2] = {*p, '\0'};
        drawList->AddText(ImVec2(textX, textY), labelColour, glyph);
        textY += line;
    }

    ImGui::PopID();
    ui::popMono();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, max.y + scaled(4.0f)));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

} // namespace

bool panelOpen(PanelSection section)
{
    return g_panelOpen[static_cast<int>(section)];
}

bool sidebarCollapsed()
{
    return g_sidebarCollapsed;
}

void setSidebarCollapsed(bool collapsed)
{
    g_sidebarCollapsed = collapsed;
}

float sidebarRailWidth()
{
    // Accent bar, one mono glyph, and enough pad that the letters do not
    // sit on the preview's crop marks. Wider than a character, narrower
    // than a word — that is the point of stacking them.
    return 40.0f;
}

void drawSidebarRail(bool outputSending)
{
    const ImVec2 winMin  = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(winMin.x + winSize.x - 1.0f, winMin.y),
                                        ImVec2(winMin.x + winSize.x - 1.0f, winMin.y + winSize.y),
                                        kLineU32);

    // Stacked letters, not rotated text: ImGui will not turn a string on its
    // side without rewriting vertices, and a rack label already reads this way.
    drawRailItem("PROGRAM", kSplitMagentaU32, -1);
    drawRailItem("SOURCE", kSplitCyanU32, static_cast<int>(PanelSection::Source));
    drawRailItem("OUTPUT", outputSending ? kSplitMagentaU32 : kRackGreyU32,
                 static_cast<int>(PanelSection::Output));
    drawRailItem("EFFECTS", kSplitCyanU32, static_cast<int>(PanelSection::Effects));
}

bool drawPanelHeader(const char* title, ImU32 accent, PanelSection section, const char* meta)
{
    // Mono throughout: a panel header is a rack label, and IDENTIDADE.md puts
    // the mono face on the technical line and all secondary labelling.
    ui::pushMono();

    ImGui::PushID(static_cast<int>(section));

    ImDrawList*  drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    const float  width    = ImGui::GetContentRegionAvail().x;
    const float  pad      = scaled(4.0f);
    const float  height   = ImGui::GetTextLineHeight() + 2.0f * pad;
    const ImVec2 max(origin.x + width, origin.y + height);

    // One hit target for the whole label. Title and meta are drawn on the
    // list so they cannot steal the click.
    ImGui::InvisibleButton("##fold", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    bool&      open    = g_panelOpen[static_cast<int>(section)];
    if (ImGui::IsItemClicked())
    {
        open = !open;
    }
    if (hovered)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (hovered)
    {
        drawList->AddRectFilled(origin, max, kLineU32);
    }
    drawList->AddRectFilled(origin, ImVec2(origin.x + scaled(3.0f), max.y), accent);
    drawList->AddLine(ImVec2(origin.x, max.y), max, kLineU32, 1.0f);

    const ImU32 labelColour = hovered ? kKeyLightU32 : kRackGreyU32;
    const float titleX      = origin.x + scaled(10.0f);
    const float titleY      = origin.y + pad;
    drawList->AddText(ImVec2(titleX, titleY), labelColour, title);

    const float titleWidth = ImGui::CalcTextSize(title).x;
    const float chevronS   = scaled(3.5f);
    drawChevron(drawList,
                ImVec2(titleX + titleWidth + scaled(8.0f) + chevronS, origin.y + height * 0.5f),
                open, labelColour);

    if (meta && meta[0] != '\0')
    {
        const float metaWidth = ImGui::CalcTextSize(meta).x;
        drawList->AddText(ImVec2(origin.x + width - metaWidth - scaled(2.0f), titleY),
                          kRackGreyU32, meta);
    }

    ImGui::PopID();
    ui::popMono();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, max.y + scaled(8.0f)));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return open;
}

namespace {

ImVec4 accentColour(ButtonAccent accent)
{
    return accent == ButtonAccent::Magenta ? splitMagenta : splitCyan;
}

ImVec4 accentFill(ButtonAccent accent)
{
    return accent == ButtonAccent::Magenta ? splitMagentaDim : splitCyanDim;
}

} // namespace

float actionHeight()
{
    return ImGui::GetFrameHeight() + scaled(6.0f);
}

float rowButtonWidth(int count)
{
    const int   slots   = count > 0 ? count : 1;
    const float spacing = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(slots - 1);
    const float width   = (ImGui::GetContentRegionAvail().x - spacing) / static_cast<float>(slots);
    // A panel narrower than its own controls is a layout bug, not something to
    // render as negative-width buttons that swallow the rest of the row.
    return std::max(scaled(24.0f), width);
}

bool actionButton(const char* label, ButtonAccent accent, ImVec2 size, bool active)
{
    if (size.y <= 0.0f)
    {
        size.y = actionHeight();
    }

    int pushed = 0;
    int vars   = 0;
    if (accent != ButtonAccent::Neutral)
    {
        const ImVec4 colour = accentColour(accent);
        const ImVec4 fill   = accentFill(accent);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? fill : surface);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? colour : fill);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, colour);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? keyLight : colour);
        pushed = 4;

        if (!active)
        {
            // Surface sits four values off the window background, so an
            // unfilled button reads as a line of coloured text. One hairline
            // in the accent is enough to make it a target — and it is a rule,
            // not a glow, so the flat look survives.
            ImGui::PushStyleColor(ImGuiCol_Border, fill);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ++pushed;
            ++vars;
        }
    }

    const bool pressed = ImGui::Button(label, size);

    if (vars > 0)
    {
        ImGui::PopStyleVar(vars);
    }
    if (pushed > 0)
    {
        ImGui::PopStyleColor(pushed);
    }
    return pressed;
}

bool glyphButton(const char* id, Glyph glyph, float size, const char* tooltip,
                 ButtonAccent accent, bool active)
{
    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const bool   pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool   hovered = ImGui::IsItemHovered();

    ImDrawList*  drawList = ImGui::GetWindowDrawList();
    const ImVec4 tint     = accent == ButtonAccent::Neutral ? keyLight : accentColour(accent);
    // GetColorU32 folds in the style alpha, so a button inside BeginDisabled
    // draws dimmed like every other widget instead of at full strength.
    ImU32 stroke = ImGui::GetColorU32(hovered ? tint : rackGrey);
    const ImU32 ground = ImGui::GetColorU32(accent == ButtonAccent::Neutral ? surfaceHover
                                                                            : accentFill(accent));

    if (active)
    {
        // Filled, with the glyph knocked out of it: an operator scanning the
        // panel has to see which parameters are moving on their own without
        // reading anything.
        drawList->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size),
                                ImGui::GetColorU32(tint), ImGui::GetStyle().FrameRounding);
        stroke = ImGui::GetColorU32(studioBlack);
    }
    else if (hovered)
    {
        drawList->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), ground,
                                ImGui::GetStyle().FrameRounding);
    }

    const ImVec2 centre(origin.x + size * 0.5f, origin.y + size * 0.5f);
    const float  arm   = std::max(scaled(3.0f), size * 0.20f);
    const float  thick = std::max(scaled(1.0f), size * 0.08f);

    switch (glyph)
    {
    case Glyph::Up:
        drawList->AddTriangleFilled(ImVec2(centre.x - arm, centre.y + arm * 0.55f),
                                    ImVec2(centre.x + arm, centre.y + arm * 0.55f),
                                    ImVec2(centre.x, centre.y - arm * 0.65f), stroke);
        break;
    case Glyph::Down:
        drawList->AddTriangleFilled(ImVec2(centre.x - arm, centre.y - arm * 0.55f),
                                    ImVec2(centre.x + arm, centre.y - arm * 0.55f),
                                    ImVec2(centre.x, centre.y + arm * 0.65f), stroke);
        break;
    case Glyph::Collapse:
        drawList->AddTriangleFilled(ImVec2(centre.x + arm * 0.55f, centre.y - arm),
                                    ImVec2(centre.x + arm * 0.55f, centre.y + arm),
                                    ImVec2(centre.x - arm * 0.65f, centre.y), stroke);
        break;
    case Glyph::Expand:
        drawList->AddTriangleFilled(ImVec2(centre.x - arm * 0.55f, centre.y - arm),
                                    ImVec2(centre.x + arm * 0.65f, centre.y),
                                    ImVec2(centre.x - arm * 0.55f, centre.y + arm), stroke);
        break;
    case Glyph::Close:
        drawList->AddLine(ImVec2(centre.x - arm * 0.7f, centre.y - arm * 0.7f),
                          ImVec2(centre.x + arm * 0.7f, centre.y + arm * 0.7f), stroke, thick);
        drawList->AddLine(ImVec2(centre.x - arm * 0.7f, centre.y + arm * 0.7f),
                          ImVec2(centre.x + arm * 0.7f, centre.y - arm * 0.7f), stroke, thick);
        break;
    case Glyph::Loop:
    {
        // One cycle of a sine: the shape of the thing it turns on, which reads
        // at this size where the word "Loop" needed a whole column.
        constexpr int kPoints = 13;
        ImVec2        wave[kPoints];
        for (int i = 0; i < kPoints; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kPoints - 1);
            wave[i] = ImVec2(centre.x - arm + 2.0f * arm * t,
                             centre.y - arm * 0.62f * std::sin(t * 2.0f * 3.14159265f));
        }
        drawList->AddPolyline(wave, kPoints, stroke, ImDrawFlags_None, thick);
        break;
    }
    }

    // A greyed-out arrow still owes the operator an explanation of what it
    // would have done, so the tooltip survives BeginDisabled.
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

void drawGroupLabel(const char* label, const char* value, ImU32 valueColour)
{
    ui::pushMono();

    ImDrawList*  drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    const float  width    = ImGui::GetContentRegionAvail().x;
    const float  height   = ImGui::GetTextLineHeight();

    drawList->AddText(origin, kRackGreyU32, label);
    if (value && value[0] != '\0')
    {
        const float valueWidth = ImGui::CalcTextSize(value).x;
        drawList->AddText(ImVec2(origin.x + width - valueWidth, origin.y), valueColour, value);
    }

    ImGui::Dummy(ImVec2(width, height));
    ui::popMono();
}

void drawLiveBadge(OutputStatus status)
{
    const char* label = "IDLE";
    ImU32 colour = kRackGreyU32;

    switch (status)
    {
    case OutputStatus::Live:
        label = "LIVE";
        colour = kTungstenU32;
        break;
    case OutputStatus::Frozen:
        label = "FROZEN";
        colour = kSplitMagentaU32;
        break;
    case OutputStatus::Idle:
    default:
        break;
    }

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float  height   = ImGui::GetTextLineHeight();
    const float  radius   = scaled(4.0f);
    const float  gap      = scaled(14.0f);
    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    ImDrawList*  drawList = ImGui::GetWindowDrawList();

    drawList->AddCircleFilled(ImVec2(origin.x + radius, origin.y + height * 0.5f),
                              radius, colour);
    drawList->AddText(ImVec2(origin.x + gap, origin.y),
                      colour, label);
    ImGui::Dummy(ImVec2(gap + textSize.x, height));
}

} // namespace theme
} // namespace atemfx
