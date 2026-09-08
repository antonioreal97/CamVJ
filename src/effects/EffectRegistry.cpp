#include "effects/EffectRegistry.h"

#include "core/Log.h"

namespace atemfx {

EffectRegistry& EffectRegistry::instance()
{
    static EffectRegistry registry;
    return registry;
}

void EffectRegistry::add(Factory factory)
{
    if (!factory)
    {
        return;
    }

    const std::unique_ptr<Effect> probe = factory();
    if (!probe)
    {
        ATEMFX_LOG_ERROR("Effect factory returned nothing; not registered");
        return;
    }

    const EffectDescriptor& descriptor = probe->descriptor();
    if (find(descriptor.typeId) != nullptr)
    {
        ATEMFX_LOG_ERROR("Duplicate effect type id '%s'; not registered", descriptor.typeId.c_str());
        return;
    }

    entries_.push_back({descriptor, std::move(factory)});
}

std::unique_ptr<Effect> EffectRegistry::create(std::string_view typeId) const
{
    for (const Entry& entry : entries_)
    {
        if (entry.descriptor.typeId == typeId)
        {
            return entry.factory();
        }
    }
    return nullptr;
}

const EffectDescriptor* EffectRegistry::find(std::string_view typeId) const
{
    for (const Entry& entry : entries_)
    {
        if (entry.descriptor.typeId == typeId)
        {
            return &entry.descriptor;
        }
    }
    return nullptr;
}

} // namespace atemfx
