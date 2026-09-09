#include "effects/BuiltinEffects.h"

#include "effects/EffectRegistry.h"

namespace atemfx {

void registerBuiltinEffects(EffectRegistry& registry)
{
    registry.add(&createPassthroughEffect);
    registry.add(&createRgbSplitEffect);
    registry.add(&createPixelateEffect);
    registry.add(&createFmRasterEffect);
    registry.add(&createSubpixelEffect);
    registry.add(&createShutterEffect);
    registry.add(&createFrameDelayEffect);
    registry.add(&createVhsEffect);
    registry.add(&createCrtEffect);
    registry.add(&createMirrorEffect);
    registry.add(&createAutoFrameEffect);
}

} // namespace atemfx
