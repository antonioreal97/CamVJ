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
    GpuTexture& process(EffectContext& context, GpuTexture& input);

    std::size_t size() const { return effects_.size(); }
    std::size_t enabledCount() const;

    Effect&       at(std::size_t index) { return *effects_[index]; }
    const Effect& at(std::size_t index) const { return *effects_[index]; }

private:
    std::vector<std::unique_ptr<Effect>> effects_;
};

} // namespace atemfx
