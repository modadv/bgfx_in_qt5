#include "render/scene/orbit_camera_controller.h"

#include "common/logger.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>

#include <utility>

class OrbitCameraController::Impl
{
public:
    void beginRotate(const ViewPoint& pos)
    {
        m_panning = false;
        m_zooming = false;
        m_lastPos = pos;
        m_rotating = true;
    }

    void rotateTo(const ViewPoint& pos)
    {
        if (!m_rotating)
        {
            return;
        }

        const float deltaX = pos.x - m_lastPos.x;
        const float deltaY = pos.y - m_lastPos.y;
        m_lastPos = pos;

        constexpr float rotateSpeed = 0.2f;
        m_yaw -= deltaX * rotateSpeed;
        m_pitch += deltaY * rotateSpeed;
        m_pitch = qBound(-89.0f, m_pitch, 89.0f);

        m_viewDirty = true;
        notifyConfigChanged();
    }

    void endRotate()
    {
        m_rotating = false;
    }

    void beginPan(const ViewPoint& pos)
    {
        m_rotating = false;
        m_zooming = false;
        m_lastPanPos = pos;
        m_panning = true;
    }

    void panTo(const ViewPoint& pos)
    {
        if (!m_panning)
        {
            return;
        }

        const float deltaX = pos.x - m_lastPanPos.x;
        const float deltaY = pos.y - m_lastPanPos.y;
        m_lastPanPos = pos;

        const float w = float(m_viewportWidth);
        const float h = float(m_viewportHeight);
        if (w <= 0.0f || h <= 0.0f)
        {
            return;
        }

        const float aspect = h > 0.0f ? (w / h) : 1.0f;
        const float fovRad = qDegreesToRadians(m_fovY);
        const float viewH = 2.0f * std::tan(fovRad * 0.5f) * m_distance;
        const float viewW = viewH * aspect;

        const float dx = deltaX / w * viewW;
        const float dy = deltaY / h * viewH;

        const float yawRad = qDegreesToRadians(m_yaw);
        const float pitchRad = qDegreesToRadians(m_pitch);
        QVector3D forward(
            std::sin(yawRad) * std::cos(pitchRad),
            std::sin(pitchRad),
            std::cos(yawRad) * std::cos(pitchRad)
        );
        forward.normalize();

        const QVector3D worldUp(0.0f, 1.0f, 0.0f);
        QVector3D right = QVector3D::crossProduct(forward, worldUp).normalized();
        QVector3D up = QVector3D::crossProduct(right, forward).normalized();

        m_target += (-dx) * right + (-dy) * up;
        m_viewDirty = true;
        notifyConfigChanged();
    }

    void endPan()
    {
        m_panning = false;
    }

    void beginZoomDrag(const ViewPoint& pos)
    {
        m_rotating = false;
        m_panning = false;
        m_lastZoomPos = pos;
        m_zooming = true;
    }

    void zoomDragTo(const ViewPoint& pos)
    {
        if (!m_zooming)
        {
            return;
        }

        const float deltaY = pos.y - m_lastZoomPos.y;
        m_lastZoomPos = pos;

        constexpr float zoomSpeed = 0.01f;
        m_distance = qBound(0.2f, m_distance + deltaY * zoomSpeed, 250.0f);
        m_viewDirty = true;
        notifyConfigChanged();
    }

    void endZoomDrag()
    {
        m_zooming = false;
    }

    void zoom(float delta)
    {
        constexpr float zoomSpeed = 0.0015f;
        m_distance = qBound(0.2f, m_distance - delta * zoomSpeed, 250.0f);
        m_viewDirty = true;
        notifyConfigChanged();
    }

    void updateView()
    {
        const float yawRad = qDegreesToRadians(m_yaw);
        const float pitchRad = qDegreesToRadians(m_pitch);

        QVector3D forward(
            std::sin(yawRad) * std::cos(pitchRad),
            std::sin(pitchRad),
            std::cos(yawRad) * std::cos(pitchRad)
        );
        forward.normalize();

        const QVector3D worldUp(0.0f, 1.0f, 0.0f);
        QVector3D right = QVector3D::crossProduct(forward, worldUp).normalized();
        QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        const QVector3D eye = m_target - forward * m_distance;

        m_view.setToIdentity();
        m_view.lookAt(eye, m_target, up);
        m_viewDirty = false;
    }

    void updateProj()
    {
        const float aspect = m_viewportHeight > 0
            ? float(m_viewportWidth) / float(m_viewportHeight)
            : 1.0f;

        m_proj.setToIdentity();
        m_proj.perspective(m_fovY, aspect, 0.1f, 1000.0f);
        m_projDirty = false;
    }

    void notifyConfigChanged()
    {
        if (m_onConfigChanged)
        {
            m_onConfigChanged();
        }
    }

    ViewPoint m_lastPos;
    ViewPoint m_lastPanPos;
    ViewPoint m_lastZoomPos;
    uint32_t m_viewportWidth = 1;
    uint32_t m_viewportHeight = 1;
    QVector3D m_target = QVector3D(0.0f, 0.0f, 0.0f);
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_distance = 3.0f;
    float m_fovY = 60.0f;
    bool m_rotating = false;
    bool m_panning = false;
    bool m_zooming = false;
    bool m_viewDirty = true;
    bool m_projDirty = true;
    QMatrix4x4 m_view;
    QMatrix4x4 m_proj;
    std::function<void()> m_onConfigChanged;
};

OrbitCameraController::OrbitCameraController()
    : m_impl(std::make_unique<Impl>())
{
}

OrbitCameraController::~OrbitCameraController() = default;

OrbitCameraController::OrbitCameraController(OrbitCameraController&&) noexcept = default;

OrbitCameraController& OrbitCameraController::operator=(OrbitCameraController&&) noexcept = default;

float OrbitCameraController::distance() const
{
    return m_impl->m_distance;
}

const float* OrbitCameraController::viewData() const
{
    return m_impl->m_view.constData();
}

const float* OrbitCameraController::projData() const
{
    return m_impl->m_proj.constData();
}

void OrbitCameraController::setOnConfigChanged(std::function<void()> callback)
{
    m_impl->m_onConfigChanged = std::move(callback);
}

nlohmann::json OrbitCameraController::exportConfig() const
{
    nlohmann::json config;

    config["yaw"] = m_impl->m_yaw;
    config["pitch"] = m_impl->m_pitch;
    config["distance"] = m_impl->m_distance;
    config["target"] = {
        {"x", m_impl->m_target.x()},
        {"y", m_impl->m_target.y()},
        {"z", m_impl->m_target.z()}
    };
    config["fovY"] = m_impl->m_fovY;

    return config;
}

void OrbitCameraController::loadConfig(const nlohmann::json& config)
{
    if (config.is_null() || !config.is_object())
    {
        LOG_W("[OrbitCamera] Invalid config, using defaults");
        return;
    }

    try
    {
        auto oldCallback = std::move(m_impl->m_onConfigChanged);
        m_impl->m_onConfigChanged = nullptr;

        m_impl->m_yaw = config.value("yaw", -90.0f);
        m_impl->m_pitch = config.value("pitch", 20.0f);
        m_impl->m_distance = config.value("distance", 3.0f);
        m_impl->m_fovY = config.value("fovY", 60.0f);

        if (config.contains("target") && config["target"].is_object())
        {
            const auto& target = config["target"];
            m_impl->m_target.setX(target.value("x", 0.0f));
            m_impl->m_target.setY(target.value("y", 0.0f));
            m_impl->m_target.setZ(target.value("z", 0.0f));
        }

        m_impl->m_pitch = qBound(-89.0f, m_impl->m_pitch, 89.0f);
        m_impl->m_distance = qBound(0.2f, m_impl->m_distance, 250.0f);

        m_impl->m_viewDirty = true;
        m_impl->m_projDirty = true;
        m_impl->m_onConfigChanged = std::move(oldCallback);
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_E("[OrbitCamera] Failed to parse config: {}", e.what());
    }
}

void OrbitCameraController::resize(uint32_t w, uint32_t h)
{
    m_impl->m_viewportWidth = qMax<uint32_t>(1, w);
    m_impl->m_viewportHeight = qMax<uint32_t>(1, h);
    m_impl->m_projDirty = true;
}

void OrbitCameraController::handlePointerPress(const ViewPointerEvent& event)
{
    if (event.button == ViewMouseButton::Left)
    {
        m_impl->beginRotate(event.position);
        return;
    }
    if (event.button == ViewMouseButton::Middle)
    {
        m_impl->beginPan(event.position);
        return;
    }
    if (event.button == ViewMouseButton::Right)
    {
        m_impl->beginZoomDrag(event.position);
        return;
    }

    if (event.button == ViewMouseButton::None)
    {
        if (hasViewMouseButton(event.buttons, ViewMouseButton::Left))
        {
            m_impl->beginRotate(event.position);
            return;
        }
        if (hasViewMouseButton(event.buttons, ViewMouseButton::Middle))
        {
            m_impl->beginPan(event.position);
            return;
        }
        if (hasViewMouseButton(event.buttons, ViewMouseButton::Right))
        {
            m_impl->beginZoomDrag(event.position);
        }
    }
}

void OrbitCameraController::handlePointerMove(const ViewPointerEvent& event)
{
    if (hasViewMouseButton(event.buttons, ViewMouseButton::Left))
    {
        m_impl->rotateTo(event.position);
        return;
    }
    if (hasViewMouseButton(event.buttons, ViewMouseButton::Middle))
    {
        m_impl->panTo(event.position);
        return;
    }
    if (hasViewMouseButton(event.buttons, ViewMouseButton::Right))
    {
        m_impl->zoomDragTo(event.position);
    }
}

void OrbitCameraController::handlePointerRelease(const ViewPointerEvent& event)
{
    if (event.button == ViewMouseButton::Left)
    {
        m_impl->endRotate();
        return;
    }
    if (event.button == ViewMouseButton::Middle)
    {
        m_impl->endPan();
        return;
    }
    if (event.button == ViewMouseButton::Right)
    {
        m_impl->endZoomDrag();
    }
}

void OrbitCameraController::handleScroll(const ViewScrollEvent& event)
{
    m_impl->zoom(event.delta);
}

void OrbitCameraController::setDistance(float value, bool notify)
{
    const float clamped = qBound(0.2f, value, 250.0f);
    if (qFuzzyCompare(m_impl->m_distance, clamped))
    {
        return;
    }

    m_impl->m_distance = clamped;
    m_impl->m_viewDirty = true;
    if (notify)
    {
        m_impl->notifyConfigChanged();
    }
}

void OrbitCameraController::setTarget(const RenderVector3& value, bool notify)
{
    const QVector3D target(value.x, value.y, value.z);
    if ((m_impl->m_target - target).lengthSquared() < 1e-8f)
    {
        return;
    }

    m_impl->m_target = target;
    m_impl->m_viewDirty = true;
    if (notify)
    {
        m_impl->notifyConfigChanged();
    }
}

float OrbitCameraController::computeFitDistance(float halfX, float halfY, float halfZ, float fillRatio) const
{
    const float safeFill = qBound(0.5f, fillRatio, 0.98f);
    const float aspect = m_impl->m_viewportHeight > 0
        ? float(m_impl->m_viewportWidth) / float(m_impl->m_viewportHeight)
        : 1.0f;
    const float fovYRad = qDegreesToRadians(m_impl->m_fovY);
    const float tanHalfY = std::tan(fovYRad * 0.5f);
    const float tanHalfX = tanHalfY * qMax(0.01f, aspect);

    const float yawRad = qDegreesToRadians(m_impl->m_yaw);
    const float pitchRad = qDegreesToRadians(m_impl->m_pitch);
    QVector3D forward(
        std::sin(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::cos(yawRad) * std::cos(pitchRad)
    );
    forward.normalize();

    const QVector3D worldUp(0.0f, 1.0f, 0.0f);
    QVector3D right = QVector3D::crossProduct(forward, worldUp).normalized();
    QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    const float halfWidthCam =
        std::fabs(right.x()) * halfX +
        std::fabs(right.y()) * halfY +
        std::fabs(right.z()) * halfZ;
    const float halfHeightCam =
        std::fabs(up.x()) * halfX +
        std::fabs(up.y()) * halfY +
        std::fabs(up.z()) * halfZ;
    const float halfDepthCam =
        std::fabs(forward.x()) * halfX +
        std::fabs(forward.y()) * halfY +
        std::fabs(forward.z()) * halfZ;

    const float reqX = (halfWidthCam / qMax(0.001f, tanHalfX * safeFill)) + halfDepthCam;
    const float reqY = (halfHeightCam / qMax(0.001f, tanHalfY * safeFill)) + halfDepthCam;
    const float reqCover = qMin(reqX, reqY);
    const float coverDistance = reqCover * 1.05f;

    const float nearPlane = 0.1f;
    const float nearSafety = halfDepthCam + nearPlane + 0.12f;
    return qMax(0.2f, qMax(coverDistance, nearSafety));
}

void OrbitCameraController::updateMatrices()
{
    if (m_impl->m_viewDirty)
    {
        m_impl->updateView();
    }
    if (m_impl->m_projDirty)
    {
        m_impl->updateProj();
    }
}
