# Engine Quick Local Navigator

## Scope

Local map for `src/engine/quick/*`.

## Files

- `src/engine/quick/terrain_viewport_item.h`
  - QML-facing API (`Q_PROPERTY`, `Q_INVOKABLE`) and renderer declaration.
- `src/engine/quick/terrain_viewport_item.cpp`
  - QML-facing `QQuickFramebufferObject` item, properties, input events, sync/render bridge.
- `src/engine/quick/terrain_scene.h`
  - Scene API, camera struct, render feature composition.
- `src/engine/quick/terrain_scene.cpp`
  - Scene-level orchestration: camera, render pipeline calls, terrain feature forwarding.
- `src/engine/quick/terrain_asset_service.h`
  - Asset service API and Qt signal contract.
- `src/engine/quick/terrain_asset_service.cpp`
  - URL-to-local-cache download service for terrain/diffuse assets.
- `src/engine/quick/terrain_hash_utils.h`
  - Hash helper used by asset service cache keys.

## Primary Entry

Start from `terrain_viewport_item.h`:

1. Check `Q_PROPERTY` and `Q_INVOKABLE` API.
2. Follow `terrain_viewport_item.cpp` symbols: `synchronize`, `render`, input handlers.
3. Follow into `terrain_scene.cpp` symbols: `resize`, `update`, `loadTerrainData`, `focusOverlayRect`.

## Feature-Level Doc

- `docs/code_nav/terrain_qt_offscreen/CODE_NAV.md`
- `docs/code_nav/engine_render_core/CODE_NAV.md`
