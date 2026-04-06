# CODE_NAV: terrain_qt_offscreen

## 1. Feature Boundary

- In scope:
  - Qt demo app startup and QML entry.
  - `TerrainViewportItem` properties/events and renderer synchronization.
  - `TerrainScene` camera/input forwarding and frame orchestration.
  - Overlay pick/focus request flow.
- Out of scope:
  - Low-level bgfx handle lifecycle internals (covered by `engine_render_core`).
  - Shader algorithm details and terrain compute/draw kernel internals.

## 2. Entry Files

- QML entry:
  - `examples/qt_widget_offscreen/qml/Main.qml`
- C++ ViewModel/service entry:
  - `src/engine/quick/terrain_viewport_item.h` (`class TerrainViewportItem`)
  - `src/engine/quick/terrain_viewport_item.cpp` (`setTerrainSource`, `setDiffuseSource`, `setOverlayRects`)
- App wiring:
  - `examples/qt_widget_offscreen/main.cpp` (`qmlRegisterType<TerrainViewportItem>`, `QQmlApplicationEngine::load`)
  - `examples/qt_widget_offscreen/demo_controller.h` (`DemoController` declarations)
  - `examples/qt_widget_offscreen/demo_controller.cpp` (dialog invokables)

## 3. Control Binding Map

| UI/Control | QML Location | Binding/Event | C++ Endpoint |
|---|---|---|---|
| `Button "Load Terrain Height"` | `examples/qt_widget_offscreen/qml/Main.qml` | `onClicked: demoController.openTerrainDialog()` | `DemoController::openTerrainDialog()` |
| `Button "Load Diffuse"` | `examples/qt_widget_offscreen/qml/Main.qml` | `onClicked: demoController.openDiffuseDialog()` | `DemoController::openDiffuseDialog()` |
| `Button "Clear"` | `examples/qt_widget_offscreen/qml/Main.qml` | `onClicked: demoController.clearSources()` | `DemoController::clearSources()` |
| `TerrainViewport.terrainSource` | `examples/qt_widget_offscreen/qml/Main.qml` | `terrainSource: demoController.terrainSource` | `TerrainViewportItem::setTerrainSource(const QUrl&)` |
| `TerrainViewport.diffuseSource` | `examples/qt_widget_offscreen/qml/Main.qml` | `diffuseSource: demoController.diffuseSource` | `TerrainViewportItem::setDiffuseSource(const QUrl&)` |
| Mouse left drag rotate | `TerrainViewportItem` event override | `mousePressEvent/mouseMoveEvent/mouseReleaseEvent` | `TerrainScene::handleMousePress/Move/Release` |
| Mouse wheel zoom | `TerrainViewportItem` event override | `wheelEvent` | `TerrainScene::handleWheel` |
| Overlay rect submission | QML/JS invokes `setOverlayRects(...)` | `Q_INVOKABLE setOverlayRects(const QVariantList&)` | `TerrainViewportItem::setOverlayRects` -> `TerrainScene::setOverlayRects` |
| Overlay click pick | Left click release without drag | `m_pickPending = true`, `requestOverlayMaxReadback()` | `TerrainScene::requestOverlayMaxReadback` + `pickOverlayRect` |
| Overlay focus | QML/JS invokes `focusOverlayRect(id)` | `m_focusPending = true` | `TerrainScene::focusOverlayRect(int)` |

## 4. Data Flow

1. User action:
  - Click load/clear buttons in `Main.qml`; interact with viewport by mouse.
2. QML handler:
  - Buttons call `DemoController` invokables.
  - `TerrainViewport` property bindings update when `DemoController::sourcesChanged` is emitted.
3. VM/service call:
  - `TerrainViewportItem::setTerrainSource/setDiffuseSource` store URL under lock and call `update()`.
  - On render thread, `TerrainViewportRenderer::synchronize` diffs path and calls `TerrainScene::loadTerrainData/loadDiffuse`.
4. Data/render path:
  - `TerrainScene::loadTerrainData/loadDiffuse` delegates to `TerrainRenderFeature` API.
  - Each frame `TerrainScene::update` builds `RenderFrameContext`, pushes `RenderProxyType::Terrain`, calls `RenderPipeline::renderFrame`.
  - `TerrainRenderFeature::registerPasses` adds `terrain.main` pass, invoking terrain renderer update.
5. UI update path:
  - `TerrainViewportRenderer::render` drives bgfx frame, schedules readback, blits pixels into FBO texture.
  - Overlay pick result emits `TerrainViewportItem::overlayRectClicked(int)`.

## 5. Edit Playbook

- Change visual/UI behavior:
  - `examples/qt_widget_offscreen/qml/Main.qml`
  - `examples/qt_widget_offscreen/demo_controller.h`
  - `examples/qt_widget_offscreen/demo_controller.cpp`
- Change QML-to-C++ viewport API:
  - `src/engine/quick/terrain_viewport_item.h` (Q_PROPERTY/Q_INVOKABLE)
  - `src/engine/quick/terrain_viewport_item.cpp` (setter/event/sync logic)
- Change interaction/camera behavior:
  - `src/engine/quick/terrain_scene.h` (`OrbitCamera` API)
  - `src/engine/quick/terrain_scene.cpp` (`handleMouse*`, `handleWheel`, `applyAutoFitIfNeeded`)
- Change render scheduling (without deep shader internals):
  - `src/engine/render/pipeline/render_pipeline.cpp`
  - `src/engine/render/features/terrain/terrain_render_feature.cpp`

## 6. Minimal-Load Sequence

1. Open this feature doc.
2. Open `examples/qt_widget_offscreen/qml/Main.qml`.
3. Open `examples/qt_widget_offscreen/demo_controller.h` and `examples/qt_widget_offscreen/demo_controller.cpp` for source property producers.
4. Open `src/engine/quick/terrain_viewport_item.h`, then search in `.cpp` for `setTerrainSource`, `synchronize`, `render`.
5. Open `src/engine/quick/terrain_scene.cpp` only for touched symbols (`loadTerrainData`, `update`, `handleMouse*`, `focusOverlayRect`).
6. Open `src/engine/render/features/terrain/terrain_render_feature.cpp` only if frame pass registration behavior is in scope.
