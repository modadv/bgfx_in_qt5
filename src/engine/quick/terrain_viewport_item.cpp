#include "terrain_viewport_item.h"
#include "render/core/render_device_bgfx.h"
#include "logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QColor>
#include <QFileInfo>
#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QMetaObject>
#include <QPointer>
#include <QSize>
#if defined(Q_OS_LINUX)
#include <QX11Info>
#include <GL/glx.h>
#endif
#include <algorithm>
#include <deque>
#include <limits>
#include <mutex>

namespace {
std::mutex g_orphanedMutex;
std::deque<PendingRead> g_orphanedReads;

ViewMouseButton toViewMouseButton(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton:
        return ViewMouseButton::Left;
    case Qt::MiddleButton:
        return ViewMouseButton::Middle;
    case Qt::RightButton:
        return ViewMouseButton::Right;
    default:
        return ViewMouseButton::None;
    }
}

ViewMouseButtons toViewMouseButtons(Qt::MouseButtons buttons)
{
    ViewMouseButtons result = 0;
    if (buttons.testFlag(Qt::LeftButton))
    {
        result |= toViewMouseButtons(ViewMouseButton::Left);
    }
    if (buttons.testFlag(Qt::MiddleButton))
    {
        result |= toViewMouseButtons(ViewMouseButton::Middle);
    }
    if (buttons.testFlag(Qt::RightButton))
    {
        result |= toViewMouseButtons(ViewMouseButton::Right);
    }
    return result;
}

ViewPointerEvent toViewPointerEvent(const QMouseEvent* event)
{
    ViewPointerEvent viewEvent;
    viewEvent.position = { float(event->localPos().x()), float(event->localPos().y()) };
    viewEvent.button = toViewMouseButton(event->button());
    viewEvent.buttons = toViewMouseButtons(event->buttons());
    return viewEvent;
}

ViewScrollEvent toViewScrollEvent(const QWheelEvent* event)
{
    ViewScrollEvent scrollEvent;
    scrollEvent.delta = float(event->angleDelta().y());
    return scrollEvent;
}

void stashOrphanedReads(std::deque<PendingRead>& pending)
{
    if (pending.empty())
        return;
    std::lock_guard<std::mutex> lock(g_orphanedMutex);
    while (!pending.empty())
    {
        g_orphanedReads.push_back(std::move(pending.front()));
        pending.pop_front();
    }
}

void releaseOrphanedReads(uint32_t frameId)
{
    if (frameId == std::numeric_limits<uint32_t>::max())
        return;
    std::lock_guard<std::mutex> lock(g_orphanedMutex);
    while (!g_orphanedReads.empty() && g_orphanedReads.front().frameId <= frameId)
    {
        g_orphanedReads.pop_front();
    }
}
} // namespace

static void flipImageVertical(QByteArray& data, int width, int height)
{
    if (width <= 0 || height <= 1)
        return;

    const int rowBytes = width * 4;
    if (data.size() < rowBytes * height)
        return;

    for (int y = 0; y < height / 2; ++y)
    {
        char* top = data.data() + y * rowBytes;
        char* bottom = data.data() + (height - 1 - y) * rowBytes;
        for (int x = 0; x < rowBytes; ++x)
        {
            std::swap(top[x], bottom[x]);
        }
    }
}

//=====================================TerrainViewportRenderer======================================

TerrainViewportRenderer::TerrainViewportRenderer(TerrainViewportItem* item)
        : m_item(item)
{
    m_timer.start();
}

QOpenGLFramebufferObject* TerrainViewportRenderer::createFramebufferObject(const QSize& size)
{
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setTextureTarget(GL_TEXTURE_2D);
    fmt.setInternalTextureFormat(GL_RGBA8);
    return new QOpenGLFramebufferObject(size, fmt);
}


void TerrainViewportRenderer::synchronize(QQuickFramebufferObject* qitem)
{
    QMutexLocker locker(&m_item->m_lock);
    m_scene = &m_item->m_scene;
    auto* item = static_cast<TerrainViewportItem*>(qitem);
    ++m_syncCount;
    if (item && item->window())
    {
        // Ensure native window handle is created
        item->window()->winId();
        m_nativeWindowHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(item->window()->winId()));
        m_scene->setOverlayPixelScale(float(item->window()->devicePixelRatio()));
    }

    auto toLocalPath = [](const QUrl& url) -> QString {
        if (url.isLocalFile())
            return url.toLocalFile();
        const QString asString = QDir::fromNativeSeparators(url.toString());
        if (QFileInfo::exists(asString))
            return asString;
        return asString;
    };

    if (!m_item->terrainSource().isEmpty())
    {
        const QUrl url = m_item->terrainSource();
        const QString path = toLocalPath(url);
        if (path != m_lastTerrainPath)
        {
            LOG_I("[TerrainViewportItem] terrainSource changed previousPath={} newPath={} exists={} syncCount={}",
                  m_lastTerrainPath.toStdString(),
                  path.toStdString(),
                  QFileInfo::exists(path),
                  m_syncCount);
            m_lastTerrainPath = path;
            resetBenchmarkStats(path);
            LOG_I("[TerrainViewportItem] terrainSource url={} localPath={} exists={}",
                  url.toString().toStdString(),
                  path.toStdString(),
                  QFileInfo::exists(path));
            m_scene->loadTerrainData(path);
        }
    }
    else if (!m_lastTerrainPath.isEmpty())
    {
        m_lastTerrainPath.clear();
        m_scene->clearHeightField();
    }

    if (!m_item->diffuseSource().isEmpty())
    {
        const QUrl url = m_item->diffuseSource();
        const QString path = toLocalPath(url);
        if (path != m_lastDiffusePath)
        {
            m_lastDiffusePath = path;
            LOG_I("[TerrainViewportItem] diffuseSource url={} localPath={} exists={}",
                  url.toString().toStdString(),
                  path.toStdString(),
                  QFileInfo::exists(path));
            m_scene->loadDiffuse(path);
        }
    }
    else if (!m_lastDiffusePath.isEmpty())
    {
        m_lastDiffusePath.clear();
        m_scene->clearDiffuse();
    }

    if (m_item->m_overlayDirty)
    {
        m_scene->setOverlayRects(m_item->m_pendingOverlayRects);
        m_item->m_overlayDirty = false;
    }

    if (m_item->m_transformDirty)
    {
        m_scene->setImageTransform(
            float(m_item->m_imageRotation),
            float(m_item->m_imageScaleX),
            float(m_item->m_imageScaleY)
        );
        m_item->m_transformDirty = false;
    }

    if (m_item->m_pickPending)
    {
        m_pickPending = true;
        m_pickPos = m_item->m_pickPos;
        m_item->m_pickPending = false;
    }

    if (m_item->m_focusPending)
    {
        if (m_scene->focusOverlayRect(m_item->m_focusRectId))
        {
            m_item->m_focusPending = false;
            m_item->m_focusRectId = -1;
        }
    }
}

void TerrainViewportRenderer::render()
{
    if (!m_scene)
    {
        if (!m_loggedMissingScene)
        {
            LOG_W("[TerrainViewportItem] render skipped: scene not synchronized yet");
            m_loggedMissingScene = true;
        }
        return;
    }
    m_loggedMissingScene = false;

    QOpenGLFramebufferObject* fbo = framebufferObject();
    if (!fbo)
    {
        if (!m_loggedMissingFramebuffer)
        {
            LOG_W("[TerrainViewportItem] render skipped: missing framebuffer object");
            m_loggedMissingFramebuffer = true;
        }
        return;
    }
    m_loggedMissingFramebuffer = false;
    const QSize sz = fbo->size();
    if (sz.width() <= 0 || sz.height() <= 0)
    {
        if (!m_loggedInvalidFramebufferSize)
        {
            LOG_W("[TerrainViewportItem] render skipped: invalid framebuffer size {}x{}", sz.width(), sz.height());
            m_loggedInvalidFramebufferSize = true;
        }
        return;
    }
    m_loggedInvalidFramebufferSize = false;

    ++m_renderCount;
    if (!m_loggedFirstRender)
    {
        LOG_I("[TerrainViewportItem] render start count={} size={}x{} runtimeInited={} sceneInited={} lastTerrainPath={}",
              m_renderCount,
              sz.width(),
              sz.height(),
              m_runtimeInited,
              m_sceneInited,
              m_lastTerrainPath.toStdString());
        m_loggedFirstRender = true;
    }

    QQuickWindow* win = m_item ? m_item->window() : nullptr;
    const bool hasExternal = win != nullptr;
    if (hasExternal)
    {
        win->beginExternalCommands();
    }
    auto finishExternal = [&]() {
        if (!hasExternal)
            return;
        win->endExternalCommands();
        win->resetOpenGLState();
    };

    if (!ensureSurface(sz))
    {
        if (!m_loggedEnsureSurfaceFailure)
        {
            LOG_W("[TerrainViewportItem] render skipped: ensureSurface failed size={}x{} renderCount={} window={} currentContext={}",
                  sz.width(),
                  sz.height(),
                  m_renderCount,
                  static_cast<const void*>(win),
                  static_cast<const void*>(QOpenGLContext::currentContext()));
            m_loggedEnsureSurfaceFailure = true;
        }
        finishExternal();
        return;
    }
    m_loggedEnsureSurfaceFailure = false;

    processCompletedReadbacks();
    scheduleReadbacksFromQueue();

    const float dt = frameDeltaSeconds();
    m_scene->update(dt);
    publishBenchmarkStats(sz, dt);

    if (hasReadbackInFlight())
    {
        const uint32_t currentFrame = RenderDeviceBgfx::instance().endFrame();
        m_lastFrameId = currentFrame;
        processCompletedReadbacks();
        if (m_scene)
        {
            m_scene->processOverlayMaxReadback(currentFrame);
        }
        ++m_frameIndex;
        update();
        finishExternal();
        return;
    }
    queueSurfaceReadback(sz);

    const uint32_t currentFrame = RenderDeviceBgfx::instance().endFrame();
    m_lastFrameId = currentFrame;

    releaseOrphanedReads(currentFrame);
    processCompletedReadbacks();

    if (m_scene)
    {
        m_scene->processOverlayMaxReadback(currentFrame);
    }
    handlePostFrameInteractions();

    ++m_frameIndex;
    const bool keepRendering = hasReadbackInFlight()
        || m_pickPending
        || (!m_lastTerrainPath.isEmpty() && !m_scene->isTerrainDataReady());
    if (keepRendering)
    {
        update();
    }
    finishExternal();
}

float TerrainViewportRenderer::frameDeltaSeconds()
{
    return float(m_timer.restart()) / 1000.0f;
}

void TerrainViewportRenderer::resetBenchmarkStats(const QString& terrainPath)
{
    m_benchmarkTimer.restart();
    m_readyFrameSumMs = 0.0;
    m_readyFrameMaxMs = 0.0;
    m_readySeen = false;

    if (m_item == nullptr)
    {
        return;
    }

    // Called from synchronize() while m_item->m_lock is already held.
    m_item->m_benchmarkStats = ViewportBenchmarkStats{};
    m_item->m_benchmarkStats.terrainPath = terrainPath;
}

void TerrainViewportRenderer::publishBenchmarkStats(const QSize& size, float deltaSeconds)
{
    if (m_item == nullptr || m_scene == nullptr)
    {
        return;
    }

    QMutexLocker locker(&m_item->m_lock);
    ViewportBenchmarkStats& stats = m_item->m_benchmarkStats;
    stats.viewportWidth = uint32_t(qMax(0, size.width()));
    stats.viewportHeight = uint32_t(qMax(0, size.height()));
    stats.terrainWidth = m_scene->terrainWidth();
    stats.terrainHeight = m_scene->terrainHeight();
    stats.terrainReady = m_scene->isTerrainDataReady();
    stats.totalFrameCount += 1;
    stats.lastFrameMs = double(deltaSeconds) * 1000.0;
    stats.loadTimeMs = m_scene->loadTimeMs();
    stats.cpuSmapTimeMs = m_scene->cpuSmapTimeMs();
    stats.gpuSmapTimeMs = m_scene->gpuSmapTimeMs();

    if (!stats.terrainReady)
    {
        return;
    }

    if (!m_readySeen)
    {
        m_readySeen = true;
        stats.readyElapsedMs = double(m_benchmarkTimer.elapsed());
    }

    stats.readyFrameCount += 1;
    m_readyFrameSumMs += stats.lastFrameMs;
    m_readyFrameMaxMs = qMax(m_readyFrameMaxMs, stats.lastFrameMs);
    stats.readyFrameAverageMs = m_readyFrameSumMs / double(stats.readyFrameCount);
    stats.readyFrameMaxMs = m_readyFrameMaxMs;
}

bool TerrainViewportRenderer::hasReadbackInFlight() const
{
    if (!m_pendingReads.empty() || !m_readyForRead.empty())
    {
        return true;
    }

    for (bool inUse : m_readbackInUse)
    {
        if (inUse)
        {
            return true;
        }
    }
    return false;
}

void TerrainViewportRenderer::queueSurfaceReadback(const QSize& size)
{
    const uint8_t readbackCount = RenderDeviceBgfx::instance().readbackCount();
    uint8_t dstIdx = readbackCount;
    for (uint8_t i = 0; i < readbackCount; ++i)
    {
        const uint8_t candidate = uint8_t((m_nextReadbackIndex + i) % readbackCount);
        if (!m_readbackInUse[candidate])
        {
            dstIdx = candidate;
            m_nextReadbackIndex = uint8_t((candidate + 1) % readbackCount);
            break;
        }
    }

    if (dstIdx == readbackCount)
    {
        return;
    }

    bgfx::TextureHandle dstHandle = m_surface.readbackTex[dstIdx];
    bgfx::blit(
        m_surface.blitViewId,
        dstHandle,
        0, 0,
        m_surface.colorTex
    );

    BlitRecord blitRec;
    blitRec.handle = dstHandle;
    blitRec.frameIndex = m_frameIndex;
    blitRec.width = size.width();
    blitRec.height = size.height();
    blitRec.texIndex = dstIdx;
    m_readyForRead.push_back(blitRec);
    m_readbackInUse[dstIdx] = true;
}

void TerrainViewportRenderer::handlePostFrameInteractions()
{
    if (!m_scene)
    {
        return;
    }

    if (!m_pickPending)
    {
        return;
    }

    int rectId = -1;
    const bool hit = m_scene->pickOverlayRect(m_pickPos, rectId);
    if (hit)
    {
        QPointer<TerrainViewportItem> item = m_item;
        QMetaObject::invokeMethod(m_item, [item, rectId]() {
            if (item)
            {
                emit item->overlayRectClicked(rectId);
            }
        }, Qt::QueuedConnection);
        m_pickPending = false;
        return;
    }

    if (!m_scene->overlayMaxReady())
    {
        if (!m_scene->hasOverlayRects())
        {
            m_pickPending = false;
        }
        else
        {
            m_scene->requestOverlayMaxReadback();
        }
        return;
    }

    m_pickPending = false;
}

TerrainViewportRenderer::~TerrainViewportRenderer()
{
    waitForPendingReadbacks();
    stashOrphanedReads(m_pendingReads);
    m_readyForRead.clear();

    if (m_runtimeInited)
    {
        RenderDeviceBgfx::instance().destroySurface(m_surface);
        RenderDeviceBgfx::instance().release();
        m_runtimeInited = false;
    }
}

bool TerrainViewportRenderer::ensureSurface(const QSize& sz)
{
    if (!m_runtimeInited)
    {
        QOpenGLContext* currentContext = QOpenGLContext::currentContext();
        if (!currentContext)
        {
            if (!m_loggedMissingContext)
            {
                LOG_W("[TerrainViewportItem] Missing current OpenGL context, defer bgfx init size={}x{} windowHandle={} renderCount={}",
                      sz.width(),
                      sz.height(),
                      m_nativeWindowHandle,
                      m_renderCount);
                m_loggedMissingContext = true;
            }
            return false;
        }
        m_loggedMissingContext = false;

        bgfx::PlatformData pd{};
#if defined(Q_OS_LINUX)
        pd.ndt          = QX11Info::display();
        pd.context      = reinterpret_cast<void*>(glXGetCurrentContext());
#elif defined(Q_OS_WIN)
        pd.ndt = nullptr;
        pd.context = nullptr;
#else
        pd.ndt          = nullptr;
#endif
        pd.nwh          = m_nativeWindowHandle;
#if !defined(Q_OS_LINUX) && !defined(Q_OS_WIN)
        pd.context      = nullptr;
#endif
        pd.backBuffer   = nullptr;
        pd.backBufferDS = nullptr;
        LOG_I("[TerrainViewportItem] bgfx init request size={}x{} nativeWindowHandle={} currentContext={} platformContext={} ndt={}",
              sz.width(),
              sz.height(),
              m_nativeWindowHandle,
              static_cast<const void*>(currentContext),
              pd.context,
              pd.ndt);
        if (!pd.context)
        {
            const bool contextRequired =
#if defined(Q_OS_LINUX)
                true;
#else
                false;
#endif
            if (contextRequired)
            {
            bool nativeHandleValid = true;
            LOG_W("[TerrainViewportItem] Missing GL context for bgfx PlatformData nativeWindowHandle={} currentContext={} variantValid={}",
                  m_nativeWindowHandle,
                  static_cast<const void*>(currentContext),
                  nativeHandleValid);
            return false;
            }
        }
        RenderDeviceBgfx::instance().setPlatformData(pd);

        RenderDeviceBgfx::instance().acquire(uint32_t(sz.width()), uint32_t(sz.height()));
        if (!RenderDeviceBgfx::instance().isInitialized())
        {
            LOG_W("[TerrainViewportItem] bgfx acquire did not initialize runtime size={}x{}", sz.width(), sz.height());
            return false;
        }
        m_runtimeInited = true;
        LOG_I("[TerrainViewportItem] bgfx runtime initialized generation={}", RenderDeviceBgfx::instance().generation());
    }

    const uint64_t currentGen = RenderDeviceBgfx::instance().generation();
    if (m_surface.generation != 0 && m_surface.generation != currentGen)
    {
        LOG_W("[TerrainViewportItem] surface generation mismatch old={} new={}, recreating", m_surface.generation, currentGen);
        waitForPendingReadbacks();
        stashOrphanedReads(m_pendingReads);
        RenderDeviceBgfx::instance().destroySurface(m_surface);
        m_sceneInited = false;
        resetReadbackState();
    }

    const bool hasSurface = bgfx::isValid(m_surface.framebuffer) &&
        m_surface.generation == currentGen;

    const bool sizeChanged = m_surface.width != uint32_t(sz.width()) ||
                             m_surface.height != uint32_t(sz.height());

    bool recreated = false;

    if (!hasSurface)
    {
        if (!RenderDeviceBgfx::instance().createSurface(uint32_t(sz.width()), uint32_t(sz.height()), m_surface))
        {
            LOG_W("[TerrainViewportItem] createSurface failed size={}x{} generation={}", sz.width(), sz.height(), currentGen);
            return false;
        }
        recreated = true;
        LOG_I("[TerrainViewportItem] surface created size={}x{} renderViewId={} blitViewId={} framebufferValid={}",
              sz.width(),
              sz.height(),
              m_surface.renderViewId,
              m_surface.blitViewId,
              bgfx::isValid(m_surface.framebuffer));
    }
    else if (sizeChanged)
    {
        waitForPendingReadbacks();
        stashOrphanedReads(m_pendingReads);
        if (!RenderDeviceBgfx::instance().resizeSurface(uint32_t(sz.width()), uint32_t(sz.height()), m_surface))
        {
            LOG_W("[TerrainViewportItem] resizeSurface failed old={}x{} new={}x{}",
                  m_surface.width,
                  m_surface.height,
                  sz.width(),
                  sz.height());
            return false;
        }
        recreated = true;
        LOG_I("[TerrainViewportItem] surface resized old={}x{} new={}x{} framebufferValid={}",
              m_lastSize.width(),
              m_lastSize.height(),
              sz.width(),
              sz.height(),
              bgfx::isValid(m_surface.framebuffer));
    }

    if (recreated)
    {
        resetReadbackState();
    }

    if (m_scene)
    {
        m_scene->setRenderTarget(m_surface.renderViewId, m_surface.framebuffer);
        if (recreated || !m_sceneInited || sizeChanged)
        {
            m_scene->resize(uint32_t(sz.width()), uint32_t(sz.height()));
            m_sceneInited = true;
        }
    }

    m_lastSize = sz;
    if (!m_loggedSurfaceReady)
    {
        LOG_I("[TerrainViewportItem] ensureSurface ready size={}x{} runtimeInited={} sceneInited={} framebufferValid={}",
              sz.width(),
              sz.height(),
              m_runtimeInited,
              m_sceneInited,
              bgfx::isValid(m_surface.framebuffer));
        m_loggedSurfaceReady = true;
    }
    return true;
}

void TerrainViewportRenderer::resetReadbackState()
{
    stashOrphanedReads(m_pendingReads);
    m_readyForRead.clear();
    m_lastFrameId = std::numeric_limits<uint32_t>::max();
    m_frameIndex  = 0;
    m_readbackInUse.fill(false);
    m_nextReadbackIndex = 0;
}

void TerrainViewportRenderer::processCompletedReadbacks()
{
    if (m_lastFrameId == std::numeric_limits<uint32_t>::max())
        return;

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return;

    QOpenGLFunctions* gl = ctx->functions();
    if (!gl)
        return;

    QOpenGLFramebufferObject* fbo = framebufferObject();
    if (!fbo)
        return;

    const QSize fboSize = fbo->size();
    const GLuint targetTexture = fbo->texture();
    if (targetTexture == 0)
        return;

    const bgfx::Caps* caps = bgfx::getCaps();
    const bool needFlip = (caps && !caps->originBottomLeft);

    while (!m_pendingReads.empty() && m_pendingReads.front().frameId <= m_lastFrameId)
    {
        PendingRead ready = std::move(m_pendingReads.front());
        m_pendingReads.pop_front();

        if (ready.texIndex < m_readbackInUse.size())
        {
            m_readbackInUse[ready.texIndex] = false;
        }

        if (ready.width != fboSize.width() || ready.height != fboSize.height())
        {
            continue;
        }

        if (ready.width <= 0 || ready.height <= 0)
        {
            continue;
        }

        const size_t expectedBytes = size_t(ready.width) * size_t(ready.height) * 4u;
        if (expectedBytes == 0 || ready.pixels.size() < int(expectedBytes))
        {
            continue;
        }

        if (needFlip)
        {
            flipImageVertical(ready.pixels, ready.width, ready.height);
        }

        gl->glBindTexture(GL_TEXTURE_2D, targetTexture);
        gl->glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            ready.width,
            ready.height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            ready.pixels.constData()
        );
    }
}

void TerrainViewportRenderer::scheduleReadbacksFromQueue()
{
    if (m_frameIndex == 0)
        return;

    while (!m_readyForRead.empty() && (m_frameIndex - m_readyForRead.front().frameIndex) >= 1)
    {
        BlitRecord record = m_readyForRead.front();
        m_readyForRead.pop_front();

        if (record.width <= 0 || record.height <= 0)
        {
            if (record.texIndex < m_readbackInUse.size())
            {
                m_readbackInUse[record.texIndex] = false;
            }
            continue;
        }

        const size_t expectedBytes = size_t(record.width) * size_t(record.height) * 4u;
        if (expectedBytes == 0 || expectedBytes > size_t(std::numeric_limits<int>::max()))
        {
            if (record.texIndex < m_readbackInUse.size())
            {
                m_readbackInUse[record.texIndex] = false;
            }
            continue;
        }

        PendingRead pending;
        pending.width  = record.width;
        pending.height = record.height;
        pending.pixels.resize(int(expectedBytes));
        pending.texIndex = record.texIndex;

        const uint32_t readyFrame = bgfx::readTexture(
            record.handle,
            pending.pixels.data()
        );

        if (readyFrame != std::numeric_limits<uint32_t>::max())
        {
            pending.frameId = readyFrame;
            m_pendingReads.push_back(std::move(pending));
        }
        else if (record.texIndex < m_readbackInUse.size())
        {
            m_readbackInUse[record.texIndex] = false;
        }
    }
}

void TerrainViewportRenderer::waitForPendingReadbacks()
{
    uint32_t maxFrameId = 0;
    bool hasPending = false;
    for (const auto& pending : m_pendingReads)
    {
        if (pending.frameId == std::numeric_limits<uint32_t>::max())
            continue;
        maxFrameId = std::max(maxFrameId, pending.frameId);
        hasPending = true;
    }

    if (!hasPending || m_lastFrameId == std::numeric_limits<uint32_t>::max())
        return;

    uint32_t current = m_lastFrameId;
    int guard = 0;
    while (current < maxFrameId && guard < 16)
    {
        current = RenderDeviceBgfx::instance().endFrame();
        ++guard;
    }
    m_lastFrameId = current;
}


//======================================TerrainViewportItem=====================================


TerrainViewportItem::TerrainViewportItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFiltersChildMouseEvents(true);
}

QQuickFramebufferObject::Renderer* TerrainViewportItem::createRenderer() const
{
    return new TerrainViewportRenderer(const_cast<TerrainViewportItem*>(this));
}

QUrl TerrainViewportItem::terrainSource() const
{
    return m_terrainSource;
}

QUrl TerrainViewportItem::diffuseSource() const
{
    return m_diffuseSource;
}

double TerrainViewportItem::imageRotation() const
{
    return m_imageRotation;
}

double TerrainViewportItem::imageScaleX() const
{
    return m_imageScaleX;
}

double TerrainViewportItem::imageScaleY() const
{
    return m_imageScaleY;
}

ViewportBenchmarkStats TerrainViewportItem::benchmarkStats() const
{
    QMutexLocker locker(&m_lock);
    return m_benchmarkStats;
}

void TerrainViewportItem::setTerrainSource(const QUrl& url)
{
    if (m_terrainSource == url)
        return;

    const QString localPath = url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(url.toString());
    LOG_D("[TerrainViewportItem] setTerrainSource url={} scheme={} isLocal={}",
          url.toString().toStdString(),
          url.scheme().toStdString(),
          url.isLocalFile());
    LOG_I("[TerrainViewportItem] setTerrainSource localPath={} exists={}",
          localPath.toStdString(),
          QFileInfo::exists(localPath));
    {
        QMutexLocker locker(&m_lock);
        m_terrainSource = url;
    }

    emit terrainSourceChanged();
    update();
}

void TerrainViewportItem::setDiffuseSource(const QUrl& url)
{
    if (m_diffuseSource == url)
        return;

    const QString localPath = url.isLocalFile() ? url.toLocalFile() : QDir::fromNativeSeparators(url.toString());
    LOG_D("[TerrainViewportItem] setDiffuseSource url={} scheme={} isLocal={}",
          url.toString().toStdString(),
          url.scheme().toStdString(),
          url.isLocalFile());
    LOG_I("[TerrainViewportItem] setDiffuseSource localPath={} exists={}",
          localPath.toStdString(),
          QFileInfo::exists(localPath));
    {
        QMutexLocker locker(&m_lock);
        m_diffuseSource = url;
    }

    emit diffuseSourceChanged();
    update();
}

void TerrainViewportItem::setImageRotation(double rotation)
{
    if (qFuzzyCompare(m_imageRotation + 1.0, rotation + 1.0))
    {
        return;
    }

    {
        QMutexLocker locker(&m_lock);
        m_imageRotation = rotation;
        m_transformDirty = true;
    }

    emit imageRotationChanged();
    update();
}

void TerrainViewportItem::setImageScaleX(double scale)
{
    if (qFuzzyCompare(m_imageScaleX + 1.0, scale + 1.0))
    {
        return;
    }

    {
        QMutexLocker locker(&m_lock);
        m_imageScaleX = scale;
        m_transformDirty = true;
    }

    emit imageRotationChanged();
    update();
}

void TerrainViewportItem::setImageScaleY(double scale)
{
    if (qFuzzyCompare(m_imageScaleY + 1.0, scale + 1.0))
    {
        return;
    }

    {
        QMutexLocker locker(&m_lock);
        m_imageScaleY = scale;
        m_transformDirty = true;
    }

    emit imageRotationChanged();
    update();
}

void TerrainViewportItem::setOverlayRects(const QVariantList& rects)
{
    std::vector<OverlayRect> parsed;
    parsed.reserve(rects.size());

    for (const QVariant& item : rects)
    {
        const QVariantMap map = item.toMap();
        if (map.isEmpty())
        {
            continue;
        }

        OverlayRect rect;
        rect.id = map.value(QStringLiteral("id"), -1).toInt();
        rect.x = map.value(QStringLiteral("x")).toFloat();
        rect.y = map.value(QStringLiteral("y")).toFloat();
        rect.width = map.contains(QStringLiteral("width"))
            ? map.value(QStringLiteral("width")).toFloat()
            : map.value(QStringLiteral("w")).toFloat();
        rect.height = map.contains(QStringLiteral("height"))
            ? map.value(QStringLiteral("height")).toFloat()
            : map.value(QStringLiteral("h")).toFloat();

        QColor color = map.value(QStringLiteral("color")).value<QColor>();
        if (!color.isValid())
        {
            color = QColor(map.value(QStringLiteral("color")).toString());
        }
        if (!color.isValid())
        {
            color = QColor(255, 0, 0, 255);
        }
        rect.color[0] = float(color.redF());
        rect.color[1] = float(color.greenF());
        rect.color[2] = float(color.blueF());
        rect.color[3] = float(color.alphaF());

        rect.lineWidth = map.value(QStringLiteral("lineWidth"), 1.0).toFloat();
        rect.dashLength = map.value(QStringLiteral("dashLength"), 0.0).toFloat();
        rect.dashGap = map.value(QStringLiteral("dashGap"), 0.0).toFloat();
        rect.blinkPeriod = map.value(QStringLiteral("blinkPeriod"), 0.0).toFloat();
        rect.blinkDuty = map.value(QStringLiteral("blinkDuty"), 0.5).toFloat();
        rect.angle = map.value(QStringLiteral("angle"), 0.0).toFloat();
        rect.imageWidth = map.value(QStringLiteral("imageWidth"), 0.0).toFloat();
        rect.imageHeight = map.value(QStringLiteral("imageHeight"), 0.0).toFloat();

        const QString coordType = map.value(QStringLiteral("coordType")).toString().toLower();
        if (coordType == QStringLiteral("pixel_center"))
        {
            rect.coordType = OverlayCoordType::PixelCenter;
        }
        else if (coordType == QStringLiteral("pixel_top_left"))
        {
            rect.coordType = OverlayCoordType::TopLeftPixels;
        }
        else if (coordType == QStringLiteral("normalized_center") || coordType == QStringLiteral("normalized"))
        {
            rect.coordType = OverlayCoordType::NormalizedCenter;
        }

        parsed.push_back(rect);
    }

    {
        QMutexLocker locker(&m_lock);
        m_pendingOverlayRects = std::move(parsed);
        m_overlayDirty = true;
    }

    update();
}

void TerrainViewportItem::clearOverlayRects()
{
    {
        QMutexLocker locker(&m_lock);
        m_pendingOverlayRects.clear();
        m_overlayDirty = true;
    }

    update();
}

void TerrainViewportItem::setOverlayUseScreenSpace(bool enabled)
{
    QMutexLocker locker(&m_lock);
    m_scene.setOverlayUseScreenSpace(enabled);
    update();
}

void TerrainViewportItem::setOverlayDebugAxes(bool enabled)
{
    QMutexLocker locker(&m_lock);
    m_scene.setOverlayDebugAxes(enabled);
    update();
}

void TerrainViewportItem::focusOverlayRect(int rectId)
{
    {
        QMutexLocker locker(&m_lock);
        m_focusPending = true;
        m_focusRectId = rectId;
    }
    LOG_D("[TerrainViewportItem] Focus overlay rect request id={}", rectId);
    update();
}

void TerrainViewportItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        {
            QMutexLocker locker(&m_lock);
            m_leftDown = true;
            m_leftDragging = false;
            m_leftPressPos = event->localPos();
        }
        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        m_scene.handlePointerPress(toViewPointerEvent(event));
        update();
        event->accept();
        return;
    }

    event->ignore();
}

void TerrainViewportItem::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        bool startRotate = false;
        {
            QMutexLocker locker(&m_lock);
            if (m_leftDown)
            {
                const qreal dx = event->localPos().x() - m_leftPressPos.x();
                const qreal dy = event->localPos().y() - m_leftPressPos.y();
                if (!m_leftDragging && (dx * dx + dy * dy) >= 9.0)
                {
                    m_leftDragging = true;
                    startRotate = true;
                }
            }
        }

        if (startRotate)
        {
            m_scene.handlePointerPress(toViewPointerEvent(event));
        }

        if (m_leftDragging)
        {
            m_scene.handlePointerMove(toViewPointerEvent(event));
            update();
            event->accept();
            return;
        }
    }

    if (event->buttons() & Qt::MiddleButton || event->buttons() & Qt::RightButton)
    {
        m_scene.handlePointerMove(toViewPointerEvent(event));
        update();
        event->accept();
        return;
    }

    event->ignore();
}

void TerrainViewportItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        bool wasDragging = false;
        {
            QMutexLocker locker(&m_lock);
            wasDragging = m_leftDragging;
            m_leftDown = false;
            m_leftDragging = false;
        }

        if (wasDragging)
        {
            m_scene.handlePointerRelease(toViewPointerEvent(event));
        }
        else
        {
            {
                QMutexLocker locker(&m_lock);
                m_pickPending = true;
                const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
                m_pickPos = QPointF(event->localPos().x() * dpr, event->localPos().y() * dpr);
            }
            m_scene.requestOverlayMaxReadback();
        }

        update();
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        m_scene.handlePointerRelease(toViewPointerEvent(event));
        update();
        event->accept();
        return;
    }

    event->ignore();
}

void TerrainViewportItem::wheelEvent(QWheelEvent* event)
{
    m_scene.handleScroll(toViewScrollEvent(event));
    update();
}
