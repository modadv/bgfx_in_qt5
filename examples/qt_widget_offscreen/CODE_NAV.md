# Qt Offscreen Example Local Navigator

## Scope

Local map for `examples/qt_widget_offscreen/*`.

## Files

- `examples/qt_widget_offscreen/CMakeLists.txt`
  - Defines `bgfx_qt_widget_example`, links `engine_core`, copies shaders post-build.
- `examples/qt_widget_offscreen/main.cpp`
  - Registers `TerrainViewportItem` to QML and boots `QQmlApplicationEngine`.
- `examples/qt_widget_offscreen/demo_controller.h`
  - QObject API and `Q_PROPERTY` for terrain/diffuse sources.
- `examples/qt_widget_offscreen/demo_controller.cpp`
  - UI file-selection controller (`openTerrainDialog`, `openDiffuseDialog`, `clearSources`).
- `examples/qt_widget_offscreen/qml/Main.qml`
  - UI controls and `TerrainViewport` grid instances.
- `examples/qt_widget_offscreen/qml.qrc`
  - Resource packaging for QML entry.

## Primary Entry

Start from `main.cpp`, then follow:

1. `qml/Main.qml` bindings to `demoController` and `TerrainViewport`.
2. `demo_controller.cpp` invokables emitting `sourcesChanged`.
3. `src/engine/quick/terrain_viewport_item.*` for bound property handling.

## Feature-Level Doc

- `docs/code_nav/terrain_qt_offscreen/CODE_NAV.md`
