# Engine Render Local Navigator

## Scope

Local map for `src/engine/render/*`.

## Files

- `src/engine/render/core/render_device_bgfx.h`
  - bgfx runtime singleton API and view-surface contract.
- `src/engine/render/core/render_device_bgfx.cpp`
  - bgfx init/shutdown, reference counting, view surface creation/resizing/destruction.
- `src/engine/render/pipeline/render_feature.h`
  - Feature contract and frame context.
- `src/engine/render/pipeline/render_graph.h`
  - Render graph API for pass registration.
- `src/engine/render/pipeline/render_graph.cpp`
  - Pass list and ordered execution.
- `src/engine/render/pipeline/render_pipeline.h`
  - Pipeline API for feature orchestration.
- `src/engine/render/pipeline/render_pipeline.cpp`
  - Feature registration, per-frame setup, graph execution.
- `src/engine/render/features/terrain/terrain_render_feature.h`
  - Terrain feature adapter API.
- `src/engine/render/features/terrain/terrain_render_feature.cpp`
  - Terrain feature adapter between pipeline and terrain renderer.
- `src/engine/render/terrain/terrain_render_pipeline.h`
  - Terrain renderer API, resource handles, stage methods.
- `src/engine/render/terrain/terrain_render_pipeline.cpp`
  - Terrain resource management, compute/draw stages, overlay logic.
- `src/engine/render/terrain/terrain_uniform_set.h`
  - Uniform-set API.
- `src/engine/render/terrain/terrain_uniform_set.cpp`
  - Terrain uniform setup/submission.
- `src/engine/render/terrain/terrain_patch_tables.h`
  - Patch table API.
- `src/engine/render/terrain/terrain_patch_tables.cpp`
  - Patch lookup tables for terrain topology/subdivision.
- `src/engine/render/scene/render_proxy.h`
  - Render proxy type and queue contract.

## Primary Entry

Start from `render/pipeline/render_pipeline.cpp`, then follow:

1. `render/features/terrain/terrain_render_feature.cpp`
2. `render/terrain/terrain_render_pipeline.h`
3. `render/terrain/terrain_render_pipeline.cpp`
4. `render/core/render_device_bgfx.cpp` when resource/view/frame issues are involved.

## Feature-Level Doc

- `docs/code_nav/engine_render_core/CODE_NAV.md`
