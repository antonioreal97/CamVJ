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

bool g_panelOpen[] = {true, true, true, true};

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

} // namespace

bool panelOpen(PanelSection section)
{
    return g_panelOpen[static_cast<int>(section)];
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

void drawLiveBadge(bool live)
{
    const char*  label    = live ? "LIVE" : "IDLE";
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float  height   = ImGui::GetTextLineHeight();
    const float  radius   = scaled(4.0f);
    const float  gap      = scaled(14.0f);
    const ImVec2 origin   = ImGui::GetCursorScreenPos();
    ImDrawList*  drawList = ImGui::GetWindowDrawList();

    drawList->AddCircleFilled(ImVec2(origin.x + radius, origin.y + height * 0.5f),
                              radius, live ? kTungstenU32 : kRackGreyU32);
    drawList->AddText(ImVec2(origin.x + gap, origin.y),
                      live ? kTungstenU32 : kRackGreyU32, label);
    ImGui::Dummy(ImVec2(gap + textSize.x, height));
}

} // namespace theme
} // namespace atemfx
