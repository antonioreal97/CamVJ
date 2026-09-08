#include "effects/EffectChain.h"

#include <utility>

#include "core/Log.h"
#include "effects/EffectRegistry.h"

namespace atemfx {

void EffectChain::shutdown()
{
    for (std::unique_ptr<Effect>& effect : effects_)
    {
        effect->shutdown();
    }
    effects_.clear();
}

bool EffectChain::add(std::unique_ptr<Effect> effect, EffectContext& context, std::string& error)
{
    if (!effect)
    {
        error = "Null effect";
        return false;
    }

    if (!effect->initialize(context))
    {
        error = effect->lastError().empty() ? "Effect initialisation failed" : effect->lastError();
        ATEMFX_LOG_ERROR("Effect '%s' failed to initialise: %s",
                         effect->descriptor().typeId.c_str(),
                         error.c_str());
        effect->shutdown();
        return false;
    }

    ATEMFX_LOG_INFO("Added effect '%s'", effect->descriptor().typeId.c_str());
    effects_.push_back(std::move(effect));
    return true;
}

bool EffectChain::addByType(std::string_view typeId, EffectContext& context, std::string& error)
{
    std::unique_ptr<Effect> effect = EffectRegistry::instance().create(typeId);
    if (!effect)
    {
        error = "Unknown effect type: " + std::string(typeId);
        return false;
    }
    return add(std::move(effect), context, error);
}

void EffectChain::remove(std::size_t index)
{
    if (index >= effects_.size())
    {
        return;
    }

    effects_[index]->shutdown();
    effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(index));
}

void EffectChain::move(std::size_t index, int delta)
{
    if (index >= effects_.size() || delta == 0)
    {
        return;
    }

    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(index) + delta;
    if (target < 0 || target >= static_cast<std::ptrdiff_t>(effects_.size()))
    {
        return;
    }

    std::swap(effects_[index], effects_[static_cast<std::size_t>(target)]);
}

void EffectChain::clear()
{
    shutdown();
}

std::size_t EffectChain::enabledCount() const
{
    std::size_t count = 0;
    for (const std::unique_ptr<Effect>& effect : effects_)
    {
        if (effect->enabled())
        {
            ++count;
        }
    }
    return count;
}

GpuTexture& EffectChain::process(EffectContext& context, GpuTexture& input)
{
    // Every node keeps its clock while bypassed or hidden in the UI. Advance
    // once before processing so all readers see the same parameter values.
    for (const auto& effect : effects_)
    {
        effect->parameters().advanceAutomations(context.deltaTime);
    }

    GpuTexture* current     = &input;
    std::size_t targetIndex = 0;

    for (std::unique_ptr<Effect>& effect : effects_)
    {
        if (!effect->enabled())
        {
            continue;
        }

        GpuTexture& destination = context.targets->scratch(targetIndex);
        if (!destination.valid())
        {
            break;
        }

        // An effect that declines to run leaves the image and the ping-pong
        // index untouched, so a broken shader is a no-op rather than a black
        // frame.
        if (effect->process(context, *current, destination))
        {
            current = &destination;
            ++targetIndex;
        }
    }

    return *current;
}

} // namespace atemfx
