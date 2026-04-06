# CODE_NAV: engine_render_core

## 1. Feature Boundary

- In scope:
  - CMake shader compile pipeline and engine core target composition.
  - bgfx runtime singleton and surface lifecycle.
  - Render feature interface, graph, pipeline orchestration.
  - Terrain render feature integration and key terrain render API surfaces.
- Out of scope:
  - Qt QML UI bindings and demo controller flow (covered by `terrain_qt_offscreen`).
  - External third-party source internals under `external/`.

## 2. Entry Files

- Build/system entry:
  - `src/engine/CMakeLists.txt`
  - `examples/qt_widget_offscreen/CMakeLists.txt` (consumes `engine_core` shaders/runtime)
- C++ runtime entry:
  - `src/engine/render/core/render_device_bgfx.h` (`RenderDeviceBgfx` API)
  - `src/engine/render/core/render_device_bgfx.cpp` (lifecycle implementation)
  - `src/engine/render/pipeline/render_feature.h` (`IRenderFeature`, `RenderFrameContext`)
  - `src/engine/render/pipeline/render_pipeline.h` (`RenderPipeline` API)
  - `src/engine/render/pipeline/render_pipeline.cpp` (frame orchestration)
  - `src/engine/render/features/terrain/terrain_render_feature.h` (`TerrainRenderFeature` API)
  - `src/engine/render/features/terrain/terrain_render_feature.cpp` (pass registration)
  - `src/engine/render/terrain/terrain_render_pipeline.h` (`TerrainRenderPipeline` API)
  - `src/engine/render/terrain/terrain_render_pipeline.cpp` (terrain runtime implementation)

## 3. Control Binding Map

| Trigger | Source Location | API/Event | Endpoint |
|---|---|---|---|
| Engine target build | `src/engine/CMakeLists.txt` | `add_library(engine_core ...)` | Compiles render core + terrain units |
| Shader build | `src/engine/CMakeLists.txt` | `compile_shader(...)` and `add_custom_target(engine_shaders)` | Produces runtime shader bins under build tree |
| Feature registration | `src/engine/quick/terrain_scene.cpp` | `m_renderPipeline.registerFeature(&m_terrainFeature)` | `RenderPipeline::registerFeature` |
| Per-frame orchestration | `src/engine/quick/terrain_scene.cpp` | `m_renderPipeline.renderFrame(frameCtx)` | `RenderPipeline::renderFrame` |
| Feature pass enqueue | `terrain_render_feature.cpp` | `graph.addPass("terrain.main", ...)` | `RenderGraph::addPass` + `RenderGraph::execute` |
| Actual terrain update | `terrain_render_feature.cpp` | pass callback lambda | `TerrainRenderPipeline::update(...)` |
| bgfx runtime acquire | `terrain_viewport_item.cpp` | `RenderDeviceBgfx::instance().acquire(...)` | `RenderDeviceBgfx::acquire`/`doInit` |
| Surface resize/recreate | `terrain_viewport_item.cpp` | `createSurface` / `resizeSurface` | `RenderDeviceBgfx` surface API |
| Frame submit/fence | `terrain_viewport_item.cpp` | `RenderDeviceBgfx::endFrame()` | wraps `bgfx::frame()` cadence |

## 4. Data Flow

1. Startup/build path:
  - CMake builds `engine_core`; `engine_shaders` compiles `.sc` shader sources into runtime bins.
2. Runtime init path:
  - Qt renderer sets `bgfx::PlatformData`, then calls `RenderDeviceBgfx::acquire`.
  - Surface is created (`renderViewId`, `blitViewId`, framebuffer/textures).
3. Frame orchestration path:
  - `TerrainScene::update` forms `RenderFrameContext` with camera matrices.
  - `RenderPipeline::renderFrame` calls each feature `setupFrame` + `registerPasses`, then executes graph.
4. Terrain feature path:
  - `TerrainRenderFeature` filters proxies, conditionally registers `terrain.main`.
  - Pass callback calls `TerrainRenderPipeline::update`, which internally runs streaming/texture/compute/draw stages.
5. Output path:
  - bgfx submits into offscreen framebuffer.
  - Renderer performs blit/readback to Qt FBO texture for presentation and optional overlay picking.

## 5. Edit Playbook

- Change shader compile targets/platform profiles:
  - `src/engine/CMakeLists.txt` (`RENDERERS`, `compile_shader`, shader lists)
- Change engine feature architecture:
  - `src/engine/render/pipeline/render_feature.h`
  - `src/engine/render/pipeline/render_pipeline.h`
  - `src/engine/render/pipeline/render_pipeline.cpp`
  - `src/engine/render/pipeline/render_graph.h`
  - `src/engine/render/pipeline/render_graph.cpp`
- Change bgfx singleton/surface lifecycle:
  - `src/engine/render/core/render_device_bgfx.h`
  - `src/engine/render/core/render_device_bgfx.cpp`
- Change terrain feature API surface exposed to scene layer:
  - `src/engine/render/features/terrain/terrain_render_feature.h`
  - `src/engine/render/features/terrain/terrain_render_feature.cpp`
- Change terrain render kernel behavior/resources:
  - `src/engine/render/terrain/terrain_render_pipeline.h`
  - `src/engine/render/terrain/terrain_render_pipeline.cpp`
  - `src/engine/render/terrain/terrain_uniform_set.h`
  - `src/engine/render/terrain/terrain_uniform_set.cpp`
  - `src/engine/render/terrain/terrain_patch_tables.h`
  - `src/engine/render/terrain/terrain_patch_tables.cpp`
  - `src/engine/shaders/*`

## 6. Minimal-Load Sequence

1. Open this feature doc.
2. Open `src/engine/CMakeLists.txt` for build/runtime composition.
3. Open `src/engine/render/pipeline/render_feature.h` and `render_pipeline.cpp`.
4. Open `src/engine/render/features/terrain/terrain_render_feature.cpp`.
5. Open `src/engine/render/core/render_device_bgfx.h` and `src/engine/render/core/render_device_bgfx.cpp` if issue touches device/surface/frame lifecycle.
6. Open only impacted slices of `src/engine/render/terrain/terrain_render_pipeline.h` and `src/engine/render/terrain/terrain_render_pipeline.cpp` for terrain-core behavior.
