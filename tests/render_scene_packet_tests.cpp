#include "render/scene/render_scene_packet.h"

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
    RenderProxy proxy;
    if (!expect(proxy.type == RenderProxyType::Unknown, "default proxy type should be neutral"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(proxy.featureMask == 0, "default proxy should not assume a feature"))
    {
        return EXIT_FAILURE;
    }

    RenderScenePacket packet;
    packet.reserve(3);

    RenderProxy terrainProxy = RenderProxy::makeTerrain(7);
    if (!expect(terrainProxy.type == RenderProxyType::Terrain, "terrain factory should tag terrain type"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(terrainProxy.featureMask == render_feature_bits::Terrain, "terrain factory should tag terrain feature"))
    {
        return EXIT_FAILURE;
    }
    packet.push(terrainProxy);

    RenderProxy hiddenMesh;
    hiddenMesh.type = RenderProxyType::StaticMesh;
    hiddenMesh.featureMask = render_feature_bits::StaticMesh;
    hiddenMesh.passMask = render_pass_bits::Main;
    hiddenMesh.visible = false;
    packet.push(hiddenMesh);

    RenderProxy shadowLight;
    shadowLight.type = RenderProxyType::Light;
    shadowLight.featureMask = render_feature_bits::Light;
    shadowLight.passMask = render_pass_bits::Shadow;
    packet.push(shadowLight);

    if (!expect(packet.containsVisibleFeature(render_feature_bits::Terrain, render_pass_bits::Main),
        "terrain feature should be visible in main pass"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(packet.containsVisibleType(RenderProxyType::Terrain, render_pass_bits::Main),
        "terrain type should be visible in main pass"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(!packet.containsVisibleFeature(render_feature_bits::StaticMesh, render_pass_bits::Main),
        "hidden proxies should not match visible feature queries"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(packet.containsVisibleFeature(render_feature_bits::Light, render_pass_bits::Shadow),
        "shadow-only proxies should match their declared pass"))
    {
        return EXIT_FAILURE;
    }
    if (!expect(!packet.containsVisibleFeature(render_feature_bits::Light, render_pass_bits::Main),
        "pass filtering should exclude non-matching proxies"))
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
