#pragma once

struct ImFont;

namespace atemfx {
namespace ui {

// How the platform's pixels relate to ImGui's coordinate unit.
//
// The two backends disagree, and the disagreement is the whole reason this
// struct exists. macOS reports the window in points and lets the renderer
// multiply by the backing factor, so one ImGui unit is two pixels on Retina
// and the layout must not grow. Windows is per-monitor DPI aware, so ImGui is
// already working in pixels and the layout itself has to grow instead.
//
// Getting this wrong is what makes an ImGui app look blurry: an atlas built
// for one unit per pixel and then drawn at two.
struct DisplayScale
{
    float pixelDensity = 1.0f;  // framebuffer pixels per ImGui unit
    float contentScale = 1.0f;  // how much larger the UI should be drawn

    bool operator==(const DisplayScale& other) const
    {
        return pixelDensity == other.pixelDensity && contentScale == other.contentScale;
    }
    bool operator!=(const DisplayScale& other) const { return !(*this == other); }
};

// Sans for anything an operator reads as a word, mono for anything read as a
// measurement. IDENTIDADE.md: "Geist Mono na linha técnica e em toda a
// rotulagem secundária."
struct FontSet
{
    ImFont* ui   = nullptr;
    ImFont* mono = nullptr;
};

// Builds the atlas at physical resolution and sets io.FontGlobalScale so the
// glyphs land back at their intended size. Walks a cascade per role and never
// fails: ImGui's built-in font is the last entry, so a machine with no fonts
// at all still gets a readable UI.
//
// Call after ImGui::CreateContext and before the first NewFrame, and again
// whenever the display's density changes. It clears the atlas first, so the
// renderer backend's font texture must be recreated after it returns.
void loadFonts(const DisplayScale& scale);

const FontSet& fonts();

// Scoped push of the mono face. A no-op when the cascade fell through to the
// built-in font, which has no mono variant to switch to.
void pushMono();
void popMono();

} // namespace ui
} // namespace atemfx
