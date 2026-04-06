#pragma once

#include "render_feature.h"
#include "render_graph.h"

#include <bgfx/bgfx.h>
#include <cstdint>
#include <vector>

class RenderPipeline
{
public:
    void registerFeature(IRenderFeature* feature);

    bool initialize(uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint32_t width, uint32_t height);
    void setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer);
    bool renderFrame(const RenderFramePacket& packet);

private:
    std::vector<IRenderFeature*> m_features;
    RenderGraph m_graph;
    bool m_initialized = false;
};
