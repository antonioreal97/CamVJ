#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "effects/Effect.h"

namespace atemfx {

// An ordered list of effects, executed as a ping-pong between two targets.
//
// The chain knows nothing about any particular effect and nothing about any
// graphics API. Adding a new effect type requires no change here — that is the
// whole point of the abstraction.
class EffectChain
{
public:
    // Reserves the dissolve's target and compiles its shader before the show,
    // because the first FX/Clean of the night must not be the frame that
    // allocates a 1920x1080 texture. Failing costs the dissolve, not start-up:
    // `process` falls back to the nearest end of the mix.
    void prepare(EffectContext& context);

    void shutdown();

    // Takes ownership and initialises the effect. Returns false and does not
    // add it if initialisation fails.
    bool add(std::unique_ptr<Effect> effect, EffectContext& context, std::string& error);

    // Creates by registered type id, then adds it.
    bool addByType(std::string_view typeId, EffectContext& context, std::string& error);

    void remove(std::size_t index);
    void move(std::size_t index, int delta);
    void clear();

    // Runs every enabled effect. Returns the texture holding the final image,
    // which is `input` itself when nothing was applied.
    //
    // `effectMix` is how much of the visual look reaches the output: 1 is the
    // full chain, 0 is Clean — framing only, exactly as if the visual nodes
    // were not there. In between, each visual node is dissolved back over the
    // picture it received, which is what makes leaving FX a mix rather than a
    // cut. Framing is never mixed: the shot must not drift while the look
    // fades. Costs one extra pass per visual node, and only while a
    // transition is running.
    // Calibration sources can bypass the entire chain without changing the
    // operator's effects. Their clocks keep running, just like bypassed nodes.
    GpuTexture& process(EffectContext& context, GpuTexture& input, float effectMix = 1.0f,
                        bool bypassEffects = false);

    // `process(context, input, true)` used to mean Clean and now means full
    // FX. Deleting the overload turns that silent inversion into a build
    // error rather than a look nobody ordered going to air.
    GpuTexture& process(EffectContext& context, GpuTexture& input, bool) = delete;

    std::size_t size() const { return effects_.size(); }
    std::size_t enabledCount() const;

    Effect&       at(std::size_t index) { return *effects_[index]; }
    const Effect& at(std::size_t index) const { return *effects_[index]; }

private:
    std::vector<std::unique_ptr<Effect>> effects_;
};

} // namespace atemfx
