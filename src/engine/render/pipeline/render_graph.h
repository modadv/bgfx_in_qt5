#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class RenderGraph
{
public:
    using PassCallback = std::function<void()>;

    enum class ResourceLifetime : uint8_t
    {
        External = 0,
        Transient = 1,
        History = 2
    };

    enum class PassStage : uint8_t
    {
        Prepare = 0,
        Geometry = 1,
        Lighting = 2,
        PostProcess = 3,
        Present = 4
    };

    struct PassDesc
    {
        std::string name;
        PassStage stage = PassStage::Geometry;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        PassCallback callback;
    };

    struct ResourceDesc
    {
        std::string name;
        ResourceLifetime lifetime = ResourceLifetime::Transient;
    };

    struct GraphResource
    {
        std::string name;
        ResourceLifetime lifetime = ResourceLifetime::Transient;
    };

    void clear();
    void declareResource(ResourceDesc desc);
    void declareExternalResource(const std::string& name);
    void declareTransientResource(const std::string& name);
    void declareHistoryResource(const std::string& name);
    bool hasResource(const std::string& name) const;
    bool isValid() const;
    void addPass(PassDesc desc);
    bool execute();

    struct GraphPass
    {
        std::string name;
        PassStage stage = PassStage::Geometry;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        PassCallback callback;
        size_t insertionOrder = 0;
    };

private:
    std::vector<GraphResource> m_resources;
    std::vector<GraphPass> m_passes;
    size_t m_nextInsertionOrder = 0;
};
