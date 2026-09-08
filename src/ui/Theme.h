#pragma once

#include "imgui.h"

namespace atemfx {
namespace theme {

// CamVJ palette. Light on black; the amber is the locked subject and nothing
// else. See assets/files/IDENTIDADE.md.

inline const ImVec4 studioBlack{10.0f / 255.0f, 11.0f / 255.0f, 13.0f / 255.0f, 1.0f};
inline const ImVec4 previewBlack{5.0f / 255.0f, 6.0f / 255.0f, 7.0f / 255.0f, 1.0f};
inline const ImVec4 surface{18.0f / 255.0f, 20.0f / 255.0f, 26.0f / 255.0f, 1.0f};
inline const ImVec4 surfaceHover{28.0f / 255.0f, 32.0f / 255.0f, 40.0f / 255.0f, 1.0f};
inline const ImVec4 surfaceActive{23.0f / 255.0f, 48.0f / 255.0f, 64.0f / 255.0f, 1.0f};
inline const ImVec4 line{28.0f / 255.0f, 32.0f / 255.0f, 40.0f / 255.0f, 1.0f};
inline const ImVec4 keyLight{242.0f / 255.0f, 244.0f / 255.0f, 247.0f / 255.0f, 1.0f};
inline const ImVec4 rackGrey{107.0f / 255.0f, 114.0f / 255.0f, 128.0f / 255.0f, 1.0f};
inline const ImVec4 tungsten{1.0f, 155.0f / 255.0f, 61.0f / 255.0f, 1.0f};
inline const ImVec4 splitCyan{23.0f / 255.0f, 169.0f / 255.0f, 224.0f / 255.0f, 1.0f};
inline const ImVec4 splitMagenta{230.0f / 255.0f, 36.0f / 255.0f, 102.0f / 255.0f, 1.0f};
inline const ImVec4 splitCyanDim{23.0f / 255.0f, 169.0f / 255.0f, 224.0f / 255.0f, 0.28f};
inline const ImVec4 splitMagentaDim{230.0f / 255.0f, 36.0f / 255.0f, 102.0f / 255.0f, 0.45f};

constexpr ImU32 kStudioBlackU32  = IM_COL32(10, 11, 13, 255);
constexpr ImU32 kKeyLightU32     = IM_COL32(242, 244, 247, 255);
constexpr ImU32 kRackGreyU32     = IM_COL32(107, 114, 128, 255);
constexpr ImU32 kTungstenU32     = IM_COL32(255, 155, 61, 255);
constexpr ImU32 kSplitCyanU32    = IM_COL32(23, 169, 224, 255);
constexpr ImU32 kSplitMagentaU32 = IM_COL32(230, 36, 102, 255);
constexpr ImU32 kLineU32         = IM_COL32(28, 32, 40, 255);

// One frame at the primary target rate. Shared by the header fps and the
// stats strip so the two never disagree about what "over budget" means.
constexpr float kFrameBudgetMs = 16.68f;

// `contentScale` is how much larger the UI must be drawn in ImGui units: 1.0
// on macOS, where a unit is a point and the renderer handles Retina, and
// dpi/96 on Windows, where a unit is already a pixel. Every size below is
// multiplied by it, so the panels grow with the font instead of clipping it.
void applyStyle(float contentScale);

// The scale the style was built with. Layout constants that ImGui's style
// does not own — panel widths, fixed panel heights — have to apply it too.
float scale();

// `value` scaled and rounded to whole units. Half-pixel panel edges are what
// make a fixed layout shimmer when the window moves between displays.
float scaled(float value);

ImVec4 budgetColour(float milliseconds);

// Four crop-mark corners plus the tungsten subject, 8% above geometric
// centre — the Auto Frame headroom rule drawn as the mark itself. No
// chromatic fringe: that version is for ≥32 px lockups, not a title bar.
void drawBrandGlyph(ImDrawList* drawList, ImVec2 centre, float size);

// Sidebar sections that fold when the operator clicks the rack label.
// State lives for the session; imgui.ini is disabled on purpose.
enum class PanelSection : int
{
    Source     = 0,
    Output     = 1,
    Effects    = 2,
    Parameters = 3,
};

bool panelOpen(PanelSection section);

// Rack-label header: accent bar, uppercase title, optional right-aligned meta.
// Click toggles the section. Returns true when the body should be drawn.
bool drawPanelHeader(const char* title, ImU32 accent, PanelSection section,
                     const char* meta = nullptr);

void drawLiveBadge(bool live);

} // namespace theme
} // namespace atemfx
