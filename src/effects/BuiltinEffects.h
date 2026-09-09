#pragma once

#include <memory>

namespace atemfx {

class Effect;
class EffectRegistry;

// Registration is explicit rather than a static-initialiser trick: the order
// here is the order shown in the UI, and explicit registration cannot be
// silently dropped by the linker.
//
// Adding an effect: one shader, one .cpp, one line in registerBuiltinEffects.
void registerBuiltinEffects(EffectRegistry& registry);

std::unique_ptr<Effect> createPassthroughEffect();
std::unique_ptr<Effect> createRgbSplitEffect();
std::unique_ptr<Effect> createPixelateEffect();
std::unique_ptr<Effect> createFmRasterEffect();
std::unique_ptr<Effect> createSubpixelEffect();
std::unique_ptr<Effect> createShutterEffect();
std::unique_ptr<Effect> createFrameDelayEffect();
std::unique_ptr<Effect> createCrtEffect();
std::unique_ptr<Effect> createVhsEffect();
std::unique_ptr<Effect> createMirrorEffect();
std::unique_ptr<Effect> createAutoFrameEffect();

} // namespace atemfx
