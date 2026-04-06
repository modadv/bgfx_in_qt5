#pragma once
#include "terrain_scene.h"
#include "render/core/render_device_bgfx.h"

#include <QQuickFramebufferObject>
#include <QUrl>
#include <QMutex>
#include <QElapsedTimer>
#include <QVariant>
#include <QPointF>
#include <QString>
#include <array>
#include <vector>

struct BlitRecord
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint64_t frameIndex = 0;
    int width = 0;
    int height = 0;
    uint8_t texIndex = 0;
};

struct PendingRead
{
    uint32_t frameId = std::numeric_limits<uint32_t>::max();
    int width = 0;
    int height = 0;
    QByteArray pixels;
    uint8_t texIndex = 0;
};

struct ViewportBenchmarkStats
{
    QString terrainPath;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    uint32_t terrainWidth = 0;
    uint32_t terrainHeight = 0;
    bool terrainReady = false;
    uint64_t totalFrameCount = 0;
    uint64_t readyFrameCount = 0;
    double lastFrameMs = 0.0;
    double readyFrameAverageMs = 0.0;
    double readyFrameMaxMs = 0.0;
    double readyElapsedMs = 0.0;
    double loadTimeMs = 0.0;
    double cpuSmapTimeMs = 0.0;
    double gpuSmapTimeMs = 0.0;
};


class TerrainViewportItem : public QQuickFramebufferObject
{
    Q_OBJECT
    friend class TerrainViewportRenderer;

    Q_PROPERTY(QUrl terrainSource READ terrainSource WRITE setTerrainSource NOTIFY terrainSourceChanged)
    Q_PROPERTY(QUrl diffuseSource   READ diffuseSource   WRITE setDiffuseSource   NOTIFY diffuseSourceChanged)
    Q_PROPERTY(double imageRotation READ imageRotation WRITE setImageRotation NOTIFY imageRotationChanged)
    Q_PROPERTY(double imageScaleX READ imageScaleX WRITE setImageScaleX NOTIFY imageRotationChanged)
    Q_PROPERTY(double imageScaleY READ imageScaleY WRITE setImageScaleY NOTIFY imageRotationChanged)

public:
    explicit TerrainViewportItem(QQuickItem* parent = nullptr);
    ~TerrainViewportItem() override = default;

    Renderer* createRenderer() const override;

    QUrl terrainSource() const;
    QUrl diffuseSource()   const;
    double imageRotation() const;
    double imageScaleX() const;
    double imageScaleY() const;
    ViewportBenchmarkStats benchmarkStats() const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

public slots:
    void setTerrainSource(const QUrl& url);
    void setDiffuseSource(const QUrl& url);
    void setImageRotation(double rotation);
    void setImageScaleX(double scale);
    void setImageScaleY(double scale);
    Q_INVOKABLE void setOverlayRects(const QVariantList& rects);
    Q_INVOKABLE void clearOverlayRects();
    Q_INVOKABLE void setOverlayUseScreenSpace(bool enabled);
    Q_INVOKABLE void setOverlayDebugAxes(bool enabled);
    Q_INVOKABLE void focusOverlayRect(int rectId);

signals:
    void terrainSourceChanged();
    void diffuseSourceChanged();
    void imageRotationChanged();
    void overlayRectClicked(int rectId);

public:
    // 渲染线程和 GUI 线程共享的场景
    TerrainScene m_scene;
    mutable QMutex m_lock;
    std::vector<OverlayRect> m_pendingOverlayRects;
    bool m_overlayDirty = false;
    bool m_transformDirty = false;
    bool m_pickPending = false;
    QPointF m_pickPos;
    bool m_leftDown = false;
    bool m_leftDragging = false;
    QPointF m_leftPressPos;
    bool m_focusPending = false;
    int m_focusRectId = -1;
    double m_imageScaleX = 1.0;
    double m_imageScaleY = 1.0;
    double m_imageRotation = 0.0;

private:
    QUrl m_terrainSource;
    QUrl m_diffuseSource;
    ViewportBenchmarkStats m_benchmarkStats;
};


class TerrainViewportRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit TerrainViewportRenderer(TerrainViewportItem* item);
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;

    void synchronize(QQuickFramebufferObject* qitem) override;
    void render() override;
    ~TerrainViewportRenderer() override;
    bool ensureSurface(const QSize& sz);
    void resetReadbackState();

private:
    QSize m_lastSize;

    float frameDeltaSeconds();
    bool hasReadbackInFlight() const;
    void queueSurfaceReadback(const QSize& size);
    void handlePostFrameInteractions();
    void processCompletedReadbacks();
    void scheduleReadbacksFromQueue();
    void waitForPendingReadbacks();
    void resetBenchmarkStats(const QString& terrainPath);
    void publishBenchmarkStats(const QSize& size, float deltaSeconds);

    TerrainViewportItem*  m_item        = nullptr;
    TerrainScene* m_scene       = nullptr;

    bool m_runtimeInited = false;
    bool m_sceneInited   = false;
    RenderDeviceBgfx::ViewSurface m_surface;

    QElapsedTimer m_timer;
    uint64_t      m_frameIndex   = 0;

    uint32_t m_lastFrameId = std::numeric_limits<uint32_t>::max();

    std::deque<BlitRecord>  m_readyForRead;
    std::deque<PendingRead> m_pendingReads;
    std::array<bool, RenderDeviceBgfx::kReadbackBufferCount> m_readbackInUse{};
    uint8_t m_nextReadbackIndex = 0;

    bool m_pickPending = false;
    QPointF m_pickPos;

    void* m_nativeWindowHandle = nullptr;
    QString m_lastTerrainPath;
    QString m_lastDiffusePath;
    QElapsedTimer m_benchmarkTimer;
    double m_readyFrameSumMs = 0.0;
    double m_readyFrameMaxMs = 0.0;
    bool m_readySeen = false;
    uint64_t m_syncCount = 0;
    uint64_t m_renderCount = 0;
    bool m_loggedMissingScene = false;
    bool m_loggedMissingFramebuffer = false;
    bool m_loggedInvalidFramebufferSize = false;
    bool m_loggedMissingContext = false;
    bool m_loggedEnsureSurfaceFailure = false;
    bool m_loggedFirstRender = false;
    bool m_loggedSurfaceReady = false;
};
