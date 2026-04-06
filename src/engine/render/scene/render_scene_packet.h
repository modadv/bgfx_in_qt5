#pragma once

#include "render/scene/render_proxy.h"

#include <cstddef>
#include <vector>

class RenderScenePacket
{
public:
    void clear() { m_items.clear(); }
    void reserve(size_t count) { m_items.reserve(count); }
    void push(const RenderProxy& proxy) { m_items.push_back(proxy); }
    const std::vector<RenderProxy>& items() const { return m_items; }
    bool empty() const { return m_items.empty(); }
    size_t size() const { return m_items.size(); }

    bool containsVisibleType(RenderProxyType type, uint32_t requiredPassMask = 0) const
    {
        for (const RenderProxy& proxy : m_items)
        {
            if (proxy.matchesType(type, requiredPassMask))
            {
                return true;
            }
        }
        return false;
    }

    bool containsVisibleFeature(uint32_t requiredFeatureMask, uint32_t requiredPassMask = 0) const
    {
        for (const RenderProxy& proxy : m_items)
        {
            if (proxy.matches(requiredFeatureMask, requiredPassMask))
            {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<RenderProxy> m_items;
};
