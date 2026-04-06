# Code Navigator

This repository uses a 3-layer navigation model:

1. Root index: `CODE_NAVIGATOR.md`
2. Feature docs: `docs/code_nav/<feature>/CODE_NAV.md`
3. Module-local docs: `<module>/CODE_NAV.md`

## Feature Docs

- `terrain_qt_offscreen`
  - `docs/code_nav/terrain_qt_offscreen/CODE_NAV.md`
  - Scope: QML demo, `TerrainViewportItem`, `TerrainScene`, render feature orchestration.
- `engine_render_core`
  - `docs/code_nav/engine_render_core/CODE_NAV.md`
  - Scope: bgfx runtime lifecycle, render pipeline/graph, terrain render core internals.

## Module-Local Docs

- `src/engine/CODE_NAV.md`
- `src/engine/quick/CODE_NAV.md`
- `src/engine/render/CODE_NAV.md`
- `examples/qt_widget_offscreen/CODE_NAV.md`

## Suggested Read Paths

- UI/interaction first:
  - `docs/code_nav/terrain_qt_offscreen/CODE_NAV.md`
  - `examples/qt_widget_offscreen/CODE_NAV.md`
  - `src/engine/quick/CODE_NAV.md`
- Render/runtime first:
  - `docs/code_nav/engine_render_core/CODE_NAV.md`
  - `src/engine/render/CODE_NAV.md`
  - `src/engine/CODE_NAV.md`
