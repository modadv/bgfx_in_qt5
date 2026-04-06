#include "render/pipeline/render_graph.h"
#include "render/scene/orbit_camera_controller.h"
#include "render/scene/render_scene_packet.h"
#include "render/scene/view_input.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>

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
    static_assert(std::is_standard_layout<ViewPoint>::value, "ViewPoint should stay a plain transport type");
    static_assert(std::is_standard_layout<ViewPointerEvent>::value, "ViewPointerEvent should stay a plain transport type");
    static_assert(std::is_standard_layout<RenderVector3>::value, "RenderVector3 should stay a plain transport type");

    ViewPointerEvent pointerEvent;
    pointerEvent.position = { 10.0f, 20.0f };
    pointerEvent.button = ViewMouseButton::Left;
    pointerEvent.buttons = toViewMouseButtons(ViewMouseButton::Left);
    if (!expect(pointerEvent.position.x == 10.0f && pointerEvent.position.y == 20.0f,
        "view input should use plain float coordinates"))
    {
        return EXIT_FAILURE;
    }

    RenderScenePacket scenePacket;
    scenePacket.push(RenderProxy::makeTerrain());
    if (!expect(scenePacket.containsVisibleFeature(render_feature_bits::Terrain, render_pass_bits::Main),
        "scene packet should remain usable without Qt host types"))
    {
        return EXIT_FAILURE;
    }

    RenderGraph::ResourceDesc resource;
    resource.name = "scene.color";
    resource.lifetime = RenderGraph::ResourceLifetime::Transient;
    if (!expect(resource.name == "scene.color", "render graph resource descriptors should remain header-only usable"))
    {
        return EXIT_FAILURE;
    }

    if (!expect(std::is_move_constructible<OrbitCameraController>::value,
        "orbit camera controller header should remain usable without Qt host types"))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
