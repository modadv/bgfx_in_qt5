#pragma once

#include "render/pipeline/render_feature.h"
#include "render/terrain/terrain_render_pipeline.h"

class TerrainRenderFeature : public IRenderFeature
{
public:
    const char* name() const override { return "TerrainRenderFeature"; }

    bool initialize(uint32_t width, uint32_t height) override;
    void shutdown() override;
    void resize(uint32_t width, uint32_t height) override;
    void setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer) override;
    void setupFrame(const RenderFramePacket& packet) override;
    void registerPasses(RenderGraph& graph) override;

    bool loadHeightFieldFromFile(const char* localPath) { return m_renderer.loadHeightFieldFromFile(localPath); }
    bool loadDiffuseFromFile(const char* localPath) { return m_renderer.loadDiffuseFromFile(localPath); }
    void clearHeightField() { m_renderer.clearHeightField(); }
    void clearDiffuse() { m_renderer.clearDiffuse(); }
    void reloadTextures() { m_renderer.reloadTextures(); }
    void setWireframe(bool on) { m_renderer.setWireframe(on); }
    void setCulling(bool on) { m_renderer.setCulling(on); }
    void setFreeze(bool on) { m_renderer.setFreeze(on); }
    void setGpuSubdivision(int level) { m_renderer.setGpuSubdivision(level); }
    void setOverlayRects(const std::vector<OverlayRect>& rects) { m_renderer.setOverlayRects(rects); }
    void clearOverlayRects() { m_renderer.clearOverlayRects(); }
    void setOverlayUseScreenSpace(bool enabled) { m_renderer.setOverlayUseScreenSpace(enabled); }
    void setOverlayDebugAxes(bool enabled) { m_renderer.setOverlayDebugAxes(enabled); }
    void setOverlayPixelScale(float scale) { m_renderer.setOverlayPixelScale(scale); }
    void setImageTransform(float rotationDeg, float scaleX, float scaleY) { m_renderer.setImageTransform(rotationDeg, scaleX, scaleY); }
    void requestOverlayMaxReadback() { m_renderer.requestOverlayMaxReadback(); }
    bool processOverlayMaxReadback(uint32_t frameId) { return m_renderer.processOverlayMaxReadback(frameId); }
    bool overlayMaxReady() const { return m_renderer.overlayMaxReady(); }
    bool hasOverlayRects() const { return m_renderer.hasOverlayRects(); }
    int pickOverlayRect(float sx, float sy) const { return m_renderer.pickOverlayRect(sx, sy); }
    bool getOverlayRectNearestEdgeTargetYaw(int rectId, float& outYawDeg) const
    {
        return m_renderer.getOverlayRectNearestEdgeTargetYaw(rectId, outYawDeg);
    }
    bool getAlgorithmDenseSideTargetYaw(float& outYawDeg, int& outRectId) const
    {
        return m_renderer.getAlgorithmDenseSideTargetYaw(outYawDeg, outRectId);
    }
    bool isTerrainDataReady() const { return m_renderer.isTerrainDataReady(); }
    float terrainAspectRatio() const { return m_renderer.terrainAspectRatio(); }
    float dmapScale() const { return m_renderer.dmapScale(); }
    float imageScaleX() const { return m_renderer.imageScaleX(); }
    float imageScaleY() const { return m_renderer.imageScaleY(); }
    float loadTimeMs() const { return m_renderer.getLoadTime(); }
    float cpuSmapTimeMs() const { return m_renderer.getCpuSmapTime(); }
    float gpuSmapTimeMs() const { return m_renderer.getGpuSmapTime(); }
    uint16_t terrainWidth() const { return m_renderer.heightFieldWidth(); }
    uint16_t terrainHeight() const { return m_renderer.heightFieldHeight(); }

private:
    TerrainRenderPipeline m_renderer;
    RenderFrameContext m_frameCtx;
    bool m_shouldRenderTerrain = true;
};
