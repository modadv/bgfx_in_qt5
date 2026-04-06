// terrain_scene.h
#pragma once

#include "render/scene/orbit_camera_controller.h"
#include "render/scene/view_input.h"
#include "render/features/terrain/terrain_render_feature.h"
#include "render/pipeline/render_pipeline.h"
#include <nlohmann/json.hpp>
#include <vector>

#include <QPointF>
#include <QString>

class TerrainScene
{
public:
    TerrainScene();
    ~TerrainScene();

    void resize(uint32_t w, uint32_t h);
    void update(float dt);
    void loadTerrainData(const QString& path);
    void loadDiffuse(const QString& path);
    void clearHeightField();
    void clearDiffuse();
    void setWireframe(bool on);
    void setCulling(bool on);
    void setFreeze(bool on);
    void setGpuSubdivision(int lvl);
    void reloadTextures();
    void setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer);
    void setOverlayRects(const std::vector<OverlayRect>& rects);
    void clearOverlayRects();
    void setOverlayUseScreenSpace(bool enabled);
    void setOverlayDebugAxes(bool enabled);
    void setOverlayPixelScale(float scale);
    void setImageTransform(float rotationDeg, float scaleX, float scaleY);
    void requestOverlayMaxReadback();
    bool processOverlayMaxReadback(uint32_t frameId);
    bool overlayMaxReady() const;
    bool hasOverlayRects() const;
    bool pickOverlayRect(const QPointF& pos, int& outId) const;
    bool focusOverlayRect(int rectId);
    bool isTerrainDataReady() const;
    float loadTimeMs() const;
    float cpuSmapTimeMs() const;
    float gpuSmapTimeMs() const;
    uint32_t terrainWidth() const;
    uint32_t terrainHeight() const;

    void handlePointerPress(const ViewPointerEvent& event);
    void handlePointerMove(const ViewPointerEvent& event);
    void handlePointerRelease(const ViewPointerEvent& event);
    void handleScroll(const ViewScrollEvent& event);

    void saveCameraConfig(const nlohmann::json& config);
    nlohmann::json getCameraConfig() const;
private:
    RenderPipeline    m_renderPipeline;
    TerrainRenderFeature m_terrainFeature;
    RenderScenePacket m_scenePacket;
    OrbitCameraController m_camera;
    bool              m_inited = false;
    uint32_t          m_viewWidth = 0;
    uint32_t          m_viewHeight = 0;
    bgfx::ViewId            m_viewId = 0;
    bgfx::FrameBufferHandle m_frameBuffer = BGFX_INVALID_HANDLE;
    QString           m_pendingTerrainDataPath;
    QString           m_pendingDiffusePath;
    bool              m_autoFitPending = false;
    
    bool m_cameraConfigLoaded = false;
    nlohmann::json m_cameraConfigCache;
    uint64_t m_frameIndex = 0;
    bool m_loggedUpdateSkippedNotInited = false;
    bool m_loggedUpdateSkippedMissingFramebuffer = false;
    bool m_loggedUpdateSkippedRenderDevice = false;
    bool m_loggedFirstFrameRendered = false;

    void buildScenePacket();
    void applyAutoFitIfNeeded();
};
