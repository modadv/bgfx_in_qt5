#pragma once

#include "render/scene/view_input.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

struct RenderVector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

class OrbitCameraController
{
public:
    OrbitCameraController();
    ~OrbitCameraController();
    OrbitCameraController(OrbitCameraController&&) noexcept;
    OrbitCameraController& operator=(OrbitCameraController&&) noexcept;

    OrbitCameraController(const OrbitCameraController&) = delete;
    OrbitCameraController& operator=(const OrbitCameraController&) = delete;

    void resize(uint32_t w, uint32_t h);
    void handlePointerPress(const ViewPointerEvent& event);
    void handlePointerMove(const ViewPointerEvent& event);
    void handlePointerRelease(const ViewPointerEvent& event);
    void handleScroll(const ViewScrollEvent& event);
    void updateMatrices();
    float distance() const;
    void setDistance(float value, bool notify = true);
    void setTarget(const RenderVector3& value, bool notify = true);
    float computeFitDistance(float halfX, float halfY, float halfZ, float fillRatio) const;

    const float* viewData() const;
    const float* projData() const;

    nlohmann::json exportConfig() const;
    void loadConfig(const nlohmann::json& config);
    void setOnConfigChanged(std::function<void()> callback);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
