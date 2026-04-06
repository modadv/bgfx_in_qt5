#pragma once

#include "render/scene/render_scene_packet.h"

#include <bgfx/bgfx.h>
#include <cstdint>

class RenderGraph;

struct RenderFrameContext
{
    float deltaTime = 0.0f;
    const float* viewMtx = nullptr;
    const float* projMtx = nullptr;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    uint64_t frameIndex = 0;
};

struct RenderFramePacket
{
    RenderFrameContext frame;
    const RenderScenePacket* scene = nullptr;
};

class IRenderFeature
{
public:
    virtual ~IRenderFeature() = default;

    virtual const char* name() const = 0;
    virtual bool initialize(uint32_t width, uint32_t height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
    virtual void setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer) = 0;
    virtual void setupFrame(const RenderFramePacket& packet) = 0;
    virtual void registerPasses(RenderGraph& graph) = 0;
};
