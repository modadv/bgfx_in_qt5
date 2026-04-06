#include "terrain_render_feature.h"

#include "render/pipeline/render_graph.h"

bool TerrainRenderFeature::initialize(uint32_t width, uint32_t height)
{
    return m_renderer.init(width, height);
}

void TerrainRenderFeature::shutdown()
{
    m_renderer.shutdown();
}

void TerrainRenderFeature::resize(uint32_t width, uint32_t height)
{
    m_renderer.resize(width, height);
}

void TerrainRenderFeature::setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer)
{
    m_renderer.setRenderTarget(viewId, framebuffer);
}

void TerrainRenderFeature::setupFrame(const RenderFramePacket& packet)
{
    m_frameCtx = packet.frame;
    m_shouldRenderTerrain = true;
    if (packet.scene != nullptr && !packet.scene->empty())
    {
        m_shouldRenderTerrain = packet.scene->containsVisibleFeature(
            render_feature_bits::Terrain,
            render_pass_bits::Main
        );
    }
}

void TerrainRenderFeature::registerPasses(RenderGraph& graph)
{
    if (!m_shouldRenderTerrain)
    {
        return;
    }

    graph.declareExternalResource("scene.color");
    graph.declareExternalResource("scene.depth");

    RenderGraph::PassDesc pass;
    pass.name = "terrain.main";
    pass.stage = RenderGraph::PassStage::Geometry;
    pass.writes = { "scene.color", "scene.depth" };
    pass.callback = [this]() {
        m_renderer.update(m_frameCtx.deltaTime, m_frameCtx.viewMtx, m_frameCtx.projMtx);
    };
    graph.addPass(pass);
}
