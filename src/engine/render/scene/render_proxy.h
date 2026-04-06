#pragma once

#include <array>
#include <cstdint>

enum class RenderProxyType : uint8_t
{
    Unknown = 0,
    Terrain = 1,
    StaticMesh = 2,
    Light = 3,
    ParticleEmitter = 4
};

namespace render_feature_bits
{
static constexpr uint32_t Terrain = 1u << 0;
static constexpr uint32_t StaticMesh = 1u << 1;
static constexpr uint32_t Light = 1u << 2;
static constexpr uint32_t ParticleEmitter = 1u << 3;
}

namespace render_pass_bits
{
static constexpr uint32_t Main = 1u << 0;
static constexpr uint32_t Shadow = 1u << 1;
static constexpr uint32_t Velocity = 1u << 2;
}

struct RenderBounds
{
    std::array<float, 3> center = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> extents = { 0.0f, 0.0f, 0.0f };
};

struct RenderProxy
{
    RenderProxyType type = RenderProxyType::Unknown;
    uint32_t id = 0;
    uint32_t featureMask = 0;
    uint32_t passMask = render_pass_bits::Main;
    uint64_t meshId = 0;
    uint64_t materialId = 0;
    std::array<float, 16> modelMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    RenderBounds bounds;
    bool visible = true;

    bool supportsFeature(uint32_t requiredFeatureMask) const
    {
        return requiredFeatureMask == 0 || (featureMask & requiredFeatureMask) != 0;
    }

    bool supportsPass(uint32_t requiredPassMask) const
    {
        return requiredPassMask == 0 || (passMask & requiredPassMask) != 0;
    }

    bool matches(uint32_t requiredFeatureMask, uint32_t requiredPassMask) const
    {
        return visible
            && supportsFeature(requiredFeatureMask)
            && supportsPass(requiredPassMask);
    }

    bool matchesType(RenderProxyType requiredType, uint32_t requiredPassMask = 0) const
    {
        return visible
            && type == requiredType
            && supportsPass(requiredPassMask);
    }

    static RenderProxy makeTerrain(uint32_t proxyId = 0)
    {
        RenderProxy proxy;
        proxy.type = RenderProxyType::Terrain;
        proxy.id = proxyId;
        proxy.featureMask = render_feature_bits::Terrain;
        proxy.passMask = render_pass_bits::Main;
        return proxy;
    }
};
