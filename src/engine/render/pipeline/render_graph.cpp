#include "render_graph.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
bool shouldRunBefore(const RenderGraph::GraphPass& lhs, const RenderGraph::GraphPass& rhs)
{
    if (lhs.stage != rhs.stage)
    {
        return static_cast<uint8_t>(lhs.stage) < static_cast<uint8_t>(rhs.stage);
    }

    return lhs.insertionOrder < rhs.insertionOrder;
}
}

void RenderGraph::clear()
{
    m_resources.clear();
    m_passes.clear();
    m_nextInsertionOrder = 0;
}

void RenderGraph::declareResource(ResourceDesc desc)
{
    if (desc.name.empty() || hasResource(desc.name))
    {
        return;
    }

    GraphResource resource;
    resource.name = std::move(desc.name);
    resource.lifetime = desc.lifetime;
    m_resources.push_back(std::move(resource));
}

void RenderGraph::declareExternalResource(const std::string& name)
{
    declareResource({ name, ResourceLifetime::External });
}

void RenderGraph::declareTransientResource(const std::string& name)
{
    declareResource({ name, ResourceLifetime::Transient });
}

void RenderGraph::declareHistoryResource(const std::string& name)
{
    declareResource({ name, ResourceLifetime::History });
}

bool RenderGraph::hasResource(const std::string& name) const
{
    for (const GraphResource& resource : m_resources)
    {
        if (resource.name == name)
        {
            return true;
        }
    }
    return false;
}

bool RenderGraph::isValid() const
{
    for (const GraphPass& pass : m_passes)
    {
        for (const std::string& resourceName : pass.reads)
        {
            if (!hasResource(resourceName))
            {
                return false;
            }
        }
        for (const std::string& resourceName : pass.writes)
        {
            if (!hasResource(resourceName))
            {
                return false;
            }
        }
    }
    return true;
}

void RenderGraph::addPass(PassDesc desc)
{
    GraphPass pass;
    pass.name = std::move(desc.name);
    pass.stage = desc.stage;
    pass.reads = std::move(desc.reads);
    pass.writes = std::move(desc.writes);
    pass.callback = std::move(desc.callback);
    pass.insertionOrder = m_nextInsertionOrder++;
    m_passes.push_back(std::move(pass));
}

bool RenderGraph::execute()
{
    const size_t passCount = m_passes.size();
    if (passCount == 0)
    {
        return true;
    }

    if (!isValid())
    {
        return false;
    }

    std::vector<std::vector<size_t>> edges(passCount);
    std::vector<size_t> indegree(passCount, 0);
    for (size_t i = 0; i < passCount; ++i)
    {
        for (size_t j = 0; j < passCount; ++j)
        {
            if (i == j || !shouldRunBefore(m_passes[i], m_passes[j]))
            {
                continue;
            }

            edges[i].push_back(j);
            ++indegree[j];
        }
    }

    std::vector<size_t> ready;
    ready.reserve(passCount);
    for (size_t i = 0; i < passCount; ++i)
    {
        if (indegree[i] == 0)
        {
            ready.push_back(i);
        }
    }

    auto sortReady = [&]() {
        std::sort(ready.begin(), ready.end(), [this](size_t lhs, size_t rhs) {
            const GraphPass& a = m_passes[lhs];
            const GraphPass& b = m_passes[rhs];
            if (a.stage != b.stage)
            {
                return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
            }
            return a.insertionOrder < b.insertionOrder;
        });
    };

    sortReady();

    std::vector<bool> wasExecuted(passCount, false);
    size_t executed = 0;
    while (!ready.empty())
    {
        const size_t index = ready.front();
        ready.erase(ready.begin());

        const GraphPass& pass = m_passes[index];
        if (pass.callback)
        {
            pass.callback();
        }
        wasExecuted[index] = true;
        ++executed;

        for (size_t next : edges[index])
        {
            if (--indegree[next] == 0)
            {
                ready.push_back(next);
            }
        }
        sortReady();
    }

    if (executed == passCount)
    {
        return true;
    }

    std::vector<const GraphPass*> fallback;
    fallback.reserve(passCount);
    for (const GraphPass& pass : m_passes)
    {
        fallback.push_back(&pass);
    }
    std::sort(fallback.begin(), fallback.end(), [](const GraphPass* lhs, const GraphPass* rhs) {
        if (lhs->stage != rhs->stage)
        {
            return static_cast<uint8_t>(lhs->stage) < static_cast<uint8_t>(rhs->stage);
        }
        return lhs->insertionOrder < rhs->insertionOrder;
    });

    for (const GraphPass* pass : fallback)
    {
        if (wasExecuted[pass->insertionOrder])
        {
            continue;
        }
        if (pass->callback)
        {
            pass->callback();
        }
    }

    return false;
}
