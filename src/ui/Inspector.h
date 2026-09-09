#pragma once

namespace atemfx {

class Effect;
class EffectChain;

namespace ui {

// What the wide panel under the preview is showing.
//
// Selection and inspection are one act: the operator clicks an effect to work
// on it, the panel swaps the stats strip for that effect's parameters, and it
// goes back to the numbers when they are done. The parameters used to live in
// the sidebar, where a column 392 units wide had to carry a list and thirteen
// sliders at once; here they get the width they need and the stats strip pays
// for it only while an effect is open.
//
// One pointer, because there is exactly one such panel. The chain owns the
// effects, so the pointer is checked against it once per frame before anybody
// reads it.
Effect* inspectedEffect();

// Clicking an effect row. Passing nullptr is closeInspector().
void inspectEffect(Effect* effect);

// Back to the stats strip: the panel's own close control, EFFECTS folding
// away, or another sidebar section being opened.
void closeInspector();

// Drops the pointer when the effect it names is no longer in the chain.
// Called once at the top of the frame, before the layout pass reads it: a
// chain can be edited between frames as well as during them, and a stale
// pointer would be dereferenced to size the panel before any panel draws.
void validateInspector(const EffectChain* chain);

// True while a framing node's parameters are open. Framing can only be
// adjusted there, so this is exactly "the operator is composing the shot" -
// which is the only time an alignment grid has anything to align.
bool adjustingFraming();

// The alignment grid over the preview monitors. Preview only: it is drawn on
// the UI's own draw list, never into the chain, so it cannot reach the output
// surface or the webcam. On by default - the operator asks for it by opening
// the framing parameters, and turns it off in the same panel.
bool framingGridEnabled();
void setFramingGridEnabled(bool enabled);

// Columns the parameters are dealt across in a panel `width` wide.
int inspectorColumns(float width);

// Height the panel wants for the inspected effect, clamped into
// [minimum, maximum]. The layout pass asks before the panel draws, so the
// arithmetic lives here instead of being guessed in two places. Must be
// called inside a frame: it measures the current font.
float inspectorHeight(float width, float minimum, float maximum);

} // namespace ui
} // namespace atemfx
