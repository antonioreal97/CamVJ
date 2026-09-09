#pragma once

#include "ui/Fonts.h"
#include "ui/Panels.h"

namespace atemfx {

class GraphicsDevice;
class Window;

// Dear ImGui setup and the fixed panel layout.
//
// One class, two implementations: the layout and styling live in UiLayer.cpp
// and are shared, while initialise / new-frame / render live in the backend
// file for the platform being built (ImGui's Win32+DX11 or OSX+Metal
// backends). Virtual dispatch would buy nothing — exactly one of them is ever
// compiled in.
//
// The layout is deliberately fixed rather than dockable: an operator should
// find the same controls in the same place every time, and a window dragged
// off-screen mid-show is a real failure mode.
class UiLayer
{
public:
    bool initialize(Window& window, GraphicsDevice& device);
    void shutdown();

    void beginFrame(Window& window, GraphicsDevice& device);
    void draw(UiFrameState& state);
    void render(GraphicsDevice& device);

private:
    // The sidebar decides what the panel under the preview shows: opening
    // another section, or folding EFFECTS away, sends it back to the stats
    // strip. Kept here because the layout already owns which section is open,
    // and the fold state itself carries no history to compare against.
    void syncInspectorToSections();

    // Context setup that does not depend on the graphics API: ImGui flags,
    // the style and the font atlas. The backends differ only in how they
    // measure the display, so that measurement is what they pass in.
    void configure(const ui::DisplayScale& scale);

    // Rebuilds the style and the font atlas when the window lands on a display
    // of a different density. Returns true when it did, which is the backend's
    // cue to recreate its font texture. Allocating here does not violate the
    // no-allocation-in-the-hot-path rule: a display change is not steady state,
    // and it happens once per drag, not once per frame.
    bool updateScale(const ui::DisplayScale& scale);

    bool             initialized_ = false;
    ui::DisplayScale scale_;

    // Last frame's fold state, so an edge can be told from a steady state.
    bool sourceOpen_  = true;
    bool outputOpen_  = true;
    bool effectsOpen_ = true;
};

} // namespace atemfx
