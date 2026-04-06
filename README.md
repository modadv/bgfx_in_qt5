# 3dengine

`3dengine` is a cross-platform 3D rendering solution for Qt GUI applications. It keeps Qt as the host UI layer and introduces a modern rendering engine through an offscreen rendering path, so existing desktop applications can gain modern 3D graphics capabilities without abandoning their Qt-based architecture.

The current focus is narrow on purpose:

- Qt `>= 5.15` remains the host/UI compatibility baseline.
- The rendering path stays offscreen so it can be embedded into Qt Widgets and Qt Quick controls.
- The engine core stays independent from Qt-facing adapters wherever practical.
- Terrain rendering is the first shipped example, not the architectural limit.

## Why This Repository Exists

Many industrial desktop applications are already built around Qt, especially in engineering, simulation, GIS, visualization, and equipment-control software. In those environments, 3D rendering is often still handled by legacy OpenGL code, tightly coupled widget logic, or platform-specific integrations.

This repository is intended as a compact starting point for teams that want:

- a reusable rendering core
- a Qt 5.15+ integration path
- a cross-platform host story for Windows, Linux, and macOS
- a practical way to bring modern engine-style rendering into an existing Qt GUI application
- room to grow toward more advanced rendering features without rewriting the host application

## Problem Statement

This project addresses a specific integration problem:

- the application shell is already Qt
- the product still needs a modern 3D rendering stack
- the team wants that rendering stack to remain portable and embeddable

The repository is therefore designed around `Qt host application + engine core + offscreen presentation bridge`, rather than around a standalone game-engine runtime.

## Intended Use Cases

- Industrial desktop software that already uses Qt as its main GUI framework
- Existing Qt applications that need embedded 3D viewports
- Teams that want to add modern rendering features incrementally instead of replacing their full UI stack

## Status

This project is currently a template-quality foundation, not a full general-purpose engine. The architecture is intentionally small and opinionated:

- `src/engine/render`: backend/runtime, scene packets, features, terrain pipeline
- `src/engine/quick`: Qt adapter layer and viewport bridge
- `examples/qt_widget_offscreen`: interactive Qt demo
- `examples/terrain_headless_benchmark`: headless benchmark harness
- `tests`: focused regression tests for core architectural seams

Public APIs and file layout may still evolve while the project is being prepared for broader open-source use.

## Architecture

The engine is split into four layers:

1. `Host/UI`
   Qt owns windows, input, event delivery, and display embedding.
2. `Qt Adapter`
   `src/engine/quick` translates Qt events and surfaces into engine-facing data.
3. `Render Core`
   Scene packets, render features, render graph state, and terrain runtime stay in `src/engine/render`.
4. `Backend`
   bgfx provides the portable graphics abstraction and shader toolchain.

More detail is in [docs/architecture.md](docs/architecture.md).

## Supported Configuration

- Host UI: Qt `>= 5.15`
- Renderer backend: bgfx
- Platforms: Windows, Linux, macOS
- Build system: CMake `>= 3.26`
- Dependency manager: Conan 2
- Language: C++17

The default local and CI path uses Qt `5.15.11` from Conan. System Qt can still be used by setting the appropriate Conan configuration for `use_system_qt`.

## Quick Start

1. Install Conan 2 and a C++17-capable toolchain.
2. Initialize submodules:

```bash
git submodule update --init --recursive
```

3. Install dependencies and generate the Conan toolchain:

```bash
conan install . -of .build -s build_type=Debug --build=missing
```

4. Configure, build, and test:

On Windows with MSVC:

```bash
cmake --preset debug-msvc
cmake --build --preset debug-msvc
ctest --preset debug-msvc
```

On Linux or macOS:

```bash
cmake --preset debug-ninja
cmake --build --preset debug-ninja
ctest --preset debug-ninja
```

If you prefer explicit commands instead of presets, the equivalent configure step is:

```bash
cmake -S . -B .build -DCMAKE_TOOLCHAIN_FILE=.build/build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug
```

## Running the Examples

Interactive Qt example:

```bash
.build/examples/qt_widget_offscreen/Debug/bgfx_qt_widget_example.exe
```

Headless benchmark:

```bash
.build/examples/terrain_headless_benchmark/Debug/terrain_headless_benchmark.exe
```

On Windows Debug builds started outside Visual Studio, run them under the Conan runtime environment so Qt DLLs resolve correctly:

```powershell
cmd /c "call .build\build\generators\conanrun.bat && .build\examples\qt_widget_offscreen\Debug\bgfx_qt_widget_example.exe"
```

## Sample Assets

The repository tracks a small sample subset of terrain assets so the demo works in a fresh clone. Larger benchmark assets are intentionally not committed.

- Sample and benchmark asset notes: [assets/README.md](assets/README.md)
- Benchmark manifest used by the Qt demo: [assets/benchmarks/terrain/usgs_3dep/manifest.json](assets/benchmarks/terrain/usgs_3dep/manifest.json)
- Asset bootstrap helper: [scripts/fetch_usgs_3dep_assets.py](scripts/fetch_usgs_3dep_assets.py)

## Testing

The repository currently includes focused regression tests for:

- standard heightmap decoding
- render graph ordering/resource declarations
- scene packet filtering
- architecture boundary constraints
- orbit camera controller behavior

The public CI baseline is defined in [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Current Constraints

- The shipped rendering example is terrain-first.
- The Qt bridge still uses an offscreen readback path.
- The render graph is intentionally minimal and not yet a full transient-resource frame graph.

These are conscious tradeoffs to keep the host integration stable while the core is being hardened.

## Contributing and Security

- Contribution guide: [CONTRIBUTING.md](CONTRIBUTING.md)
- Code of conduct: [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)
- Security policy: [SECURITY.md](SECURITY.md)
- Changelog baseline: [CHANGELOG.md](CHANGELOG.md)

GitHub issue and pull request templates are included so external contributors can report problems and propose changes with the expected level of context.

## License

Unless otherwise noted, the original code in this repository is licensed under the MIT License. Third-party code in vendored or imported directories keeps its original license terms.

See [LICENSE](LICENSE).
