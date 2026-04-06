#include "render/scene/orbit_camera_controller.h"

#include <cstdlib>
#include <iostream>

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << std::endl;
        return false;
    }
    return true;
}
}

int main()
{
    OrbitCameraController camera;
    camera.resize(800, 600);

    const nlohmann::json before = camera.exportConfig();
    camera.handlePointerPress({ { 100.0f, 100.0f }, ViewMouseButton::Left, toViewMouseButtons(ViewMouseButton::Left) });
    camera.handlePointerMove({ { 140.0f, 120.0f }, ViewMouseButton::None, toViewMouseButtons(ViewMouseButton::Left) });
    camera.handlePointerRelease({ { 140.0f, 120.0f }, ViewMouseButton::Left, 0 });
    const nlohmann::json afterRotate = camera.exportConfig();
    if (!expect(afterRotate.value("yaw", 0.0f) != before.value("yaw", 0.0f), "left-drag should update yaw"))
    {
        return EXIT_FAILURE;
    }

    camera.handlePointerPress({ { 100.0f, 100.0f }, ViewMouseButton::Middle, toViewMouseButtons(ViewMouseButton::Middle) });
    camera.handlePointerMove({ { 115.0f, 130.0f }, ViewMouseButton::None, toViewMouseButtons(ViewMouseButton::Middle) });
    camera.handlePointerRelease({ { 115.0f, 130.0f }, ViewMouseButton::Middle, 0 });
    const nlohmann::json afterPan = camera.exportConfig();
    const auto beforeTarget = afterRotate.at("target");
    const auto afterTarget = afterPan.at("target");
    if (!expect(beforeTarget.value("x", 0.0f) != afterTarget.value("x", 0.0f)
        || beforeTarget.value("y", 0.0f) != afterTarget.value("y", 0.0f)
        || beforeTarget.value("z", 0.0f) != afterTarget.value("z", 0.0f),
        "middle-drag should update target"))
    {
        return EXIT_FAILURE;
    }

    const float beforeDistance = afterPan.value("distance", 0.0f);
    camera.handleScroll({ 120.0f });
    const nlohmann::json afterScroll = camera.exportConfig();
    if (!expect(afterScroll.value("distance", 0.0f) != beforeDistance, "scroll should update distance"))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
