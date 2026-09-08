#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "effects/Effect.h"

namespace atemfx {

// Maps a stable type id to a factory, so effects can be created by name from a
// preset file or from the "Add effect" menu without the caller knowing the
// concrete types.
class EffectRegistry
{
public:
    using Factory = std::function<std::unique_ptr<Effect>()>;

    struct Entry
    {
        EffectDescriptor descriptor;
        Factory          factory;
    };

    static EffectRegistry& instance();

    // The descriptor is read from a probe instance rather than passed in, so
    // it can never drift from what the effect actually reports.
    void add(Factory factory);

    std::unique_ptr<Effect> create(std::string_view typeId) const;
    const EffectDescriptor* find(std::string_view typeId) const;

    // Registration order. This is the order the UI lists effects in.
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

} // namespace atemfx
