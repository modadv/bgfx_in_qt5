# Engine Local Navigator

## Scope

Local map for `src/engine/*`.

## Files

- `src/engine/CMakeLists.txt`
  - Declares `engine_core`, shader compile pipeline, renderer target matrix.
- `src/engine/common/bgfx_utils.h`
  - Utility API declarations for shader/program/resource loading.
- `src/engine/common/bgfx_utils.cpp`
  - Shader/program load helpers and bgfx utility routines.
- `src/engine/quick/*`
  - Qt Quick bridge layer from QML to render pipeline.
- `src/engine/render/*`
  - Render runtime core, feature graph, terrain renderer.
- `src/engine/shaders/*`
  - Vertex/fragment/compute shader sources consumed by shaderc.

## Primary Entry

Start from `src/engine/CMakeLists.txt`, then follow:

1. `render/pipeline/*` for orchestration.
2. `render/features/terrain/*` for terrain feature integration.
3. `quick/terrain_viewport_item.*` and `quick/terrain_scene.*` for Qt bridge.

## Feature-Level Doc

- `docs/code_nav/engine_render_core/CODE_NAV.md`
- `docs/code_nav/terrain_qt_offscreen/CODE_NAV.md`
