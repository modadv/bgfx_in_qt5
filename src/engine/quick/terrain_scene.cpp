// terrain_scene.cpp
#include "terrain_scene.h"
#include "render/core/render_device_bgfx.h"
#include "logger.h"
#include <QDir>
#include <QUrl>
#include <QFileInfo>
#include <QtMath>
#include <QDebug>

static QString normalizeLocalPath(const QString& in)
{
    if (in.isEmpty())
        return QString();

    QString local = in;
    const QUrl url(local);
    if (url.isValid() && url.isLocalFile())
    {
        local = url.toLocalFile();
    }
    else if (url.isValid() && url.scheme() == QStringLiteral("qrc"))
    {
        const QString qrcPath = url.path();
        QString candidate = qrcPath;
        if (!candidate.isEmpty() && QFileInfo::exists(candidate))
        {
            local = candidate;
            LOG_I("[TerrainScene] Resolved qrc path to local file: {} -> {}",
                  in.toStdString(), local.toStdString());
        }
        else if (candidate.startsWith('/') && QFileInfo::exists(candidate.mid(1)))
        {
            local = candidate.mid(1);
            LOG_I("[TerrainScene] Resolved qrc path to local file: {} -> {}",
                  in.toStdString(), local.toStdString());
        }
    }
    
    local = QDir::fromNativeSeparators(local);
    
    if (local.isEmpty() || local == "." || local == ".." || local == "/")
    {
        LOG_W("[TerrainScene] Invalid path after normalization: {}", in.toStdString());
        return QString();
    }
    
    if (!QFileInfo::exists(local))
    {
        LOG_W("[TerrainScene] File does not exist: {}", local.toStdString());
        return QString();
    }
    
    return local;
}

// ---------------- TerrainScene ----------------

TerrainScene::TerrainScene()
    : m_renderPipeline()
    , m_terrainFeature()
    , m_camera()
    , m_inited(false)
    , m_cameraConfigLoaded(false)
{
    m_renderPipeline.registerFeature(&m_terrainFeature);

    m_cameraConfigCache = {
        {"distance", 2.5},
        {"fovY", 60.0},
        {"pitch", 30.6},
        {"target", {
            {"x", 0.02},
            {"y", -0.122},
            {"z", -0.073}
        }},
        {"yaw", 181.136}
    };

    m_camera.setOnConfigChanged([this]() {
        const nlohmann::json config = m_camera.exportConfig();
        saveCameraConfig(config);
    });
}

TerrainScene::~TerrainScene()
{
    if (m_inited)
    {
        m_renderPipeline.shutdown();
        m_inited = false;
    }
}

void TerrainScene::saveCameraConfig(const nlohmann::json& config)
{
    if (config.is_object())
    {
        m_cameraConfigCache = config;
    }
}

nlohmann::json TerrainScene::getCameraConfig() const
{
    return m_cameraConfigCache;
}

void TerrainScene::resize(uint32_t w, uint32_t h)
{
    m_viewWidth = w;
    m_viewHeight = h;
    m_camera.resize(w, h);
    LOG_I("[TerrainScene] resize width={} height={} framebufferValid={} inited={} pendingTerrain={} pendingDiffuse={}",
          w,
          h,
          bgfx::isValid(m_frameBuffer),
          m_inited,
          m_pendingTerrainDataPath.toStdString(),
          m_pendingDiffusePath.toStdString());

    if (!bgfx::isValid(m_frameBuffer))
    {
        LOG_W("[TerrainScene] resize skipped render target bind: framebuffer invalid");
        return;
    }

    m_renderPipeline.setRenderTarget(m_viewId, m_frameBuffer);

    if (!m_inited)
    {
        if (!m_renderPipeline.initialize(w, h))
        {
            LOG_E("[TerrainScene] Failed to initialize render pipeline");
            return;
        }
        m_inited = true;
        LOG_I("[TerrainScene] render pipeline initialized width={} height={}", w, h);

        if (!m_cameraConfigLoaded) {
            const nlohmann::json cameraConfig = getCameraConfig();
            if (cameraConfig.is_object()) {
                m_camera.loadConfig(cameraConfig);
                m_cameraConfigLoaded = true;
                LOG_I("[TerrainScene] Camera config applied from cached settings");
            } else {
                LOG_W("[TerrainScene] Camera config missing/invalid, keep retrying on next resize");
            }
        }

        if (!m_pendingTerrainDataPath.isEmpty())
        {
            const QString local = normalizeLocalPath(m_pendingTerrainDataPath);
            if (!local.isEmpty())
            {
                LOG_I("[TerrainScene] Loading pending heightmap: {}", local.toStdString());
                m_terrainFeature.loadHeightFieldFromFile(local.toUtf8().constData());
            }
        }
        if (!m_pendingDiffusePath.isEmpty())
        {
            const QString local = normalizeLocalPath(m_pendingDiffusePath);
            if (!local.isEmpty())
            {
                LOG_I("[TerrainScene] Loading pending diffuse: {}", local.toStdString());
                m_terrainFeature.loadDiffuseFromFile(local.toUtf8().constData());
            }
        }
    }
    else
    {
        m_renderPipeline.resize(w, h);
        LOG_I("[TerrainScene] render pipeline resized width={} height={}", w, h);
    }
}


void TerrainScene::update(float dt)
{
    if (!m_inited)
    {
        if (!m_loggedUpdateSkippedNotInited)
        {
            LOG_W("[TerrainScene] update skipped: scene not initialized dt={:.4f}", dt);
            m_loggedUpdateSkippedNotInited = true;
        }
        return;
    }
    m_loggedUpdateSkippedNotInited = false;

    if (!m_cameraConfigLoaded) {
        const nlohmann::json cameraConfig = getCameraConfig();
        if (cameraConfig.is_object()) {
            m_camera.loadConfig(cameraConfig);
            m_cameraConfigLoaded = true;
            LOG_I("[TerrainScene] Camera config applied during update");
        }
    }

    if (!bgfx::isValid(m_frameBuffer))
    {
        if (!m_loggedUpdateSkippedMissingFramebuffer)
        {
            LOG_W("[TerrainScene] update skipped: framebuffer invalid dt={:.4f}", dt);
            m_loggedUpdateSkippedMissingFramebuffer = true;
        }
        return;
    }
    m_loggedUpdateSkippedMissingFramebuffer = false;

    if (!RenderDeviceBgfx::instance().isInitialized())
    {
        if (!m_loggedUpdateSkippedRenderDevice)
        {
            LOG_W("[TerrainScene] update skipped: RenderDeviceBgfx not initialized dt={:.4f}", dt);
            m_loggedUpdateSkippedRenderDevice = true;
        }
        return;
    }
    m_loggedUpdateSkippedRenderDevice = false;
    m_camera.updateMatrices();
    buildScenePacket();

    RenderFramePacket packet;
    packet.frame.deltaTime = dt;
    packet.frame.viewMtx = m_camera.viewData();
    packet.frame.projMtx = m_camera.projData();
    packet.frame.viewportWidth = m_viewWidth;
    packet.frame.viewportHeight = m_viewHeight;
    packet.frame.frameIndex = m_frameIndex++;
    packet.scene = &m_scenePacket;

    m_renderPipeline.renderFrame(packet);
    if (!m_loggedFirstFrameRendered)
    {
        LOG_I("[TerrainScene] first frame rendered view={} size={}x{} frameIndex={} terrainReady={} terrainSize={}x{}",
              m_viewId,
              m_viewWidth,
              m_viewHeight,
              packet.frame.frameIndex,
              m_terrainFeature.isTerrainDataReady(),
              m_terrainFeature.terrainWidth(),
              m_terrainFeature.terrainHeight());
        m_loggedFirstFrameRendered = true;
    }
    applyAutoFitIfNeeded();
}

void TerrainScene::buildScenePacket()
{
    m_scenePacket.clear();
    m_scenePacket.reserve(1);

    RenderProxy terrainProxy = RenderProxy::makeTerrain();
    terrainProxy.visible = true;
    m_scenePacket.push(terrainProxy);
}

void TerrainScene::setRenderTarget(bgfx::ViewId viewId, bgfx::FrameBufferHandle framebuffer)
{
    const bool changed = m_viewId != viewId
        || m_frameBuffer.idx != framebuffer.idx;
    m_viewId = viewId;
    m_frameBuffer = framebuffer;
    if (changed)
    {
        LOG_I("[TerrainScene] setRenderTarget viewId={} framebufferValid={}", m_viewId, bgfx::isValid(m_frameBuffer));
    }
    m_renderPipeline.setRenderTarget(viewId, framebuffer);
}

void TerrainScene::setOverlayRects(const std::vector<OverlayRect>& rects)
{
    m_terrainFeature.setOverlayRects(rects);
}

void TerrainScene::clearOverlayRects()
{
    m_terrainFeature.clearOverlayRects();
}

void TerrainScene::setOverlayUseScreenSpace(bool enabled)
{
    m_terrainFeature.setOverlayUseScreenSpace(enabled);
}

void TerrainScene::setOverlayDebugAxes(bool enabled)
{
    m_terrainFeature.setOverlayDebugAxes(enabled);
}

void TerrainScene::setOverlayPixelScale(float scale)
{
    m_terrainFeature.setOverlayPixelScale(scale);
}

void TerrainScene::setImageTransform(float rotationDeg, float scaleX, float scaleY)
{
    m_terrainFeature.setImageTransform(rotationDeg, scaleX, scaleY);
    // Scale updates can change visible model size significantly; retry auto-fit once.
    m_autoFitPending = true;
}

void TerrainScene::requestOverlayMaxReadback()
{
    m_terrainFeature.requestOverlayMaxReadback();
}

bool TerrainScene::processOverlayMaxReadback(uint32_t frameId)
{
    return m_terrainFeature.processOverlayMaxReadback(frameId);
}

bool TerrainScene::overlayMaxReady() const
{
    return m_terrainFeature.overlayMaxReady();
}

bool TerrainScene::hasOverlayRects() const
{
    return m_terrainFeature.hasOverlayRects();
}

bool TerrainScene::pickOverlayRect(const QPointF& pos, int& outId) const
{
    outId = m_terrainFeature.pickOverlayRect(float(pos.x()), float(pos.y()));
    return outId != -1;
}

bool TerrainScene::focusOverlayRect(int rectId)
{
    float targetYaw = 0.0f;
    int resolvedRectId = rectId;
    bool allowOppositeFace = true;
    if (rectId == -1)
    {
        if (!m_terrainFeature.getAlgorithmDenseSideTargetYaw(targetYaw, resolvedRectId))
        {
            LOG_D("[TerrainScene] focusOverlayRect: no valid dense algorithm side target");
            return false;
        }
        // Auto dense-side alignment must face the chosen side directly,
        // not the opposite side selected by shortest-angle heuristic.
        allowOppositeFace = false;
    }
    else if (!m_terrainFeature.getOverlayRectNearestEdgeTargetYaw(rectId, targetYaw))
    {
        LOG_D("[TerrainScene] focusOverlayRect: no valid target yaw for rect id={}", rectId);
        return false;
    }

    const nlohmann::json cameraConfig = getCameraConfig();
    if (cameraConfig.is_object())
    {
        m_camera.loadConfig(cameraConfig);
    }
    else
    {
        LOG_W("[TerrainScene] focusOverlayRect: camera config missing, keep current camera");
    }

    const nlohmann::json current = m_camera.exportConfig();
    const float baseYaw = current.value("yaw", 0.0f);
    const float yawA = targetYaw;
    const float yawB = targetYaw + 180.0f;

    auto normalizeDeg = [](float a) -> float {
        while (a > 180.0f) a -= 360.0f;
        while (a < -180.0f) a += 360.0f;
        return a;
    };
    auto absDelta = [&](float to) -> float {
        return std::fabs(normalizeDeg(to - baseYaw));
    };

    const float resolvedYaw = allowOppositeFace
        ? (absDelta(yawA) <= absDelta(yawB) ? yawA : yawB)
        : yawA;

    nlohmann::json updated = current;
    updated["yaw"] = resolvedYaw;
    m_camera.loadConfig(updated);

    LOG_I("[TerrainScene] focusOverlayRect: id={}, resolvedRectId={}, allowOppositeFace={}, baseYaw={:.3f}, yawA={:.3f}, yawB={:.3f}, resolvedYaw={:.3f}",
          rectId, resolvedRectId, allowOppositeFace ? 1 : 0, baseYaw, yawA, yawB, resolvedYaw);
    return true;
}

bool TerrainScene::isTerrainDataReady() const
{
    return m_terrainFeature.isTerrainDataReady();
}

float TerrainScene::loadTimeMs() const
{
    return m_terrainFeature.loadTimeMs();
}

float TerrainScene::cpuSmapTimeMs() const
{
    return m_terrainFeature.cpuSmapTimeMs();
}

float TerrainScene::gpuSmapTimeMs() const
{
    return m_terrainFeature.gpuSmapTimeMs();
}

uint32_t TerrainScene::terrainWidth() const
{
    return m_terrainFeature.terrainWidth();
}

uint32_t TerrainScene::terrainHeight() const
{
    return m_terrainFeature.terrainHeight();
}

void TerrainScene::loadTerrainData(const QString& path)
{
    if (path.isEmpty())
    {
        LOG_I("[TerrainScene] loadTerrainData received empty path, clearing terrain");
        clearHeightField();
        return;
    }

    m_pendingTerrainDataPath = path;
    m_autoFitPending = true;
    LOG_I("[TerrainScene] loadTerrainData request path={} inited={}", path.toStdString(), m_inited);

    if (!m_inited)
    {
        LOG_I("[TerrainScene] Terrain data pending (not inited): {}", path.toStdString());
        return;
    }

    const QString local = normalizeLocalPath(path);
    if (local.isEmpty())
    {
        LOG_W("[TerrainScene] loadTerrainData: invalid path after normalization: {}", path.toStdString());
        return;
    }

    LOG_I("[TerrainScene] Loading heightmap: {}", local.toStdString());
    m_terrainFeature.loadHeightFieldFromFile(local.toUtf8().constData());
}

void TerrainScene::loadDiffuse(const QString& path)
{
    if (path.isEmpty())
    {
        LOG_I("[TerrainScene] loadDiffuse received empty path, clearing diffuse");
        clearDiffuse();
        return;
    }

    m_pendingDiffusePath = path;
    LOG_I("[TerrainScene] loadDiffuse request path={} inited={}", path.toStdString(), m_inited);

    if (!m_inited)
    {
        LOG_I("[TerrainScene] Diffuse pending (not inited): {}", path.toStdString());
        return;
    }

    const QString local = normalizeLocalPath(path);
    if (local.isEmpty())
    {
        LOG_W("[TerrainScene] loadDiffuse: invalid path after normalization: {}", path.toStdString());
        return;
    }

    LOG_I("[TerrainScene] Loading diffuse: {}", local.toStdString());
    m_terrainFeature.loadDiffuseFromFile(local.toUtf8().constData());
}

void TerrainScene::clearHeightField()
{
    LOG_I("[TerrainScene] clearHeightField previousPendingPath={}", m_pendingTerrainDataPath.toStdString());
    m_pendingTerrainDataPath.clear();
    m_autoFitPending = false;
    m_terrainFeature.clearHeightField();
}

void TerrainScene::clearDiffuse()
{
    LOG_I("[TerrainScene] clearDiffuse previousPendingPath={}", m_pendingDiffusePath.toStdString());
    m_pendingDiffusePath.clear();
    m_terrainFeature.clearDiffuse();
}


void TerrainScene::setWireframe(bool on)
{
    if (!m_inited)
        return;
    m_terrainFeature.setWireframe(on);
}

void TerrainScene::setCulling(bool on)
{
    if (!m_inited)
        return;
    m_terrainFeature.setCulling(on);
}

void TerrainScene::setFreeze(bool on)
{
    if (!m_inited)
        return;
    m_terrainFeature.setFreeze(on);
}

void TerrainScene::setGpuSubdivision(int lvl)
{
    if (!m_inited)
        return;
    m_terrainFeature.setGpuSubdivision(lvl);
}

void TerrainScene::reloadTextures()
{
    if (!m_inited)
        return;
    m_terrainFeature.reloadTextures();
}

void TerrainScene::handlePointerPress(const ViewPointerEvent& event)
{
    m_camera.handlePointerPress(event);
}

void TerrainScene::handlePointerMove(const ViewPointerEvent& event)
{
    m_camera.handlePointerMove(event);
}

void TerrainScene::handlePointerRelease(const ViewPointerEvent& event)
{
    m_camera.handlePointerRelease(event);
}

void TerrainScene::handleScroll(const ViewScrollEvent& event)
{
    m_camera.handleScroll(event);
}

void TerrainScene::applyAutoFitIfNeeded()
{
    if (!m_autoFitPending || !m_terrainFeature.isTerrainDataReady())
    {
        return;
    }

    const float sx = std::fabs(m_terrainFeature.imageScaleX());
    const float sy = std::fabs(m_terrainFeature.imageScaleY());
    const float halfX = qMax(0.02f, m_terrainFeature.terrainAspectRatio() * sx);
    const float halfY = qMax(0.02f, sy);
    const float halfZ = qMax(0.05f, m_terrainFeature.dmapScale());

    // Keep orbit pivot anchored to the terrain model center instead of world origin.
    m_camera.setTarget({ 0.0f, halfZ * 0.5f, 0.0f }, false);

    const float fitDistance = m_camera.computeFitDistance(halfX, halfY, halfZ, 0.96f);
    const float currentDistance = m_camera.distance();

    // Only move closer when the model is too small in viewport.
    if (fitDistance + 0.01f < currentDistance)
    {
        m_camera.setDistance(fitDistance, false);
        LOG_I("[TerrainScene] Auto-fit camera distance: current={:.3f}, fit={:.3f}, aspect={:.4f}, scale=({:.4f},{:.4f}), halfExtents=({:.3f}, {:.3f}, {:.3f})",
              currentDistance, fitDistance, m_terrainFeature.terrainAspectRatio(), sx, sy, halfX, halfY, halfZ);
    }
    else
    {
        LOG_D("[TerrainScene] Auto-fit skipped: current={:.3f}, fit={:.3f}, aspect={:.4f}, scale=({:.4f},{:.4f})",
              currentDistance, fitDistance, m_terrainFeature.terrainAspectRatio(), sx, sy);
    }
    m_autoFitPending = false;
}
