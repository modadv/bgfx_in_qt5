#include "render_pipeline.h"

void RenderPipeline::registerFeature(IRenderFeature* feature)
{
    if (feature == nullptr)
    {
        return;
    }
    m_features.push_back(feature);
}

bool RenderPipeline::initialize(uint32_t width, uint32_t height)
{
    for (IRenderFeature* feature : m_features)
    {
        if (!feature->initialize(width, height))
        {
            return false;
        }
    }
    m_initialized = true;
    return true;
}

void RenderPipeline::shutdown()
{
    for (IRenderFeature* feature : m_features)
    {
        feature->shutdown();
    }
    m_initialized = false;
}

void RenderPipeline::resize(uint32_t width, uint32_t height)
{
    for (IRenderFeature* feature : m_features)
    {
        feature->resize(width, height);
    }
}

void RenderPipeline::setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer)
{
    for (IRenderFeature* feature : m_features)
    {
        feature->setRenderTarget(viewId, framebuffer);
    }
}

bool RenderPipeline::renderFrame(const RenderFramePacket& packet)
{
    if (!m_initialized)
    {
        return false;
    }

    m_graph.clear();
    for (IRenderFeature* feature : m_features)
    {
        feature->setupFrame(packet);
        feature->registerPasses(m_graph);
    }
    return m_graph.execute();
}
