# Architecture Overview

This repository is structured as a Qt-hosted rendering engine template, not as a Qt-only rendering widget.

## Design Goals

- Keep Qt `>= 5.15` as the long-term host baseline.
- Keep rendering logic independent from Qt-specific types where practical.
- Preserve an offscreen rendering architecture so the engine can be embedded into existing industrial desktop applications.
- Keep the architecture small enough to audit and extend without template noise.

## Layering

### 1. Host/UI Layer

The host application owns:

- windows
- event loop
- user input
- widget or Qt Quick composition

This repository currently ships Qt-based hosts in `examples/`.

### 2. Qt Adapter Layer

`src/engine/quick` is the boundary between Qt and the rendering core. It is responsible for:

- translating Qt input into plain engine input data
- managing the viewport bridge
- coordinating offscreen rendering with Qt presentation

Qt-specific code should stay here instead of leaking into the render core.

### 3. Render Core

`src/engine/render` contains the reusable core:

- device/runtime management
- scene packet extraction
- render features
- render graph ordering
- terrain runtime

The current shipped example focuses on terrain rendering, but the structure is intended to remain usable for broader scene rendering over time.

### 4. Backend Layer

bgfx is the current backend abstraction. It provides:

- cross-platform renderer selection
- resource and shader interfaces
- shader compilation tools

The repository treats bgfx as backend infrastructure, not as the application architecture itself.

## Repository Map

- `src/engine/render`: backend/runtime, scene, pipeline, terrain logic
- `src/engine/quick`: Qt-facing adapter layer
- `src/engine/common`: shared helpers and imported utility code
- `src/engine/shaders`: shader sources compiled during the build
- `examples/qt_widget_offscreen`: interactive Qt example
- `examples/terrain_headless_benchmark`: automated benchmark harness
- `tests`: focused regression coverage for critical seams

## Current Boundaries

The project is intentionally conservative in a few areas:

- The render graph is still a lightweight frame orchestration layer.
- The shipped feature set is terrain-first.
- The Qt presentation path favors stable offscreen integration over maximum throughput.

These constraints are acceptable for the current goal: helping Qt-based industrial software adopt a modernized 3D rendering core without forcing a host rewrite.
