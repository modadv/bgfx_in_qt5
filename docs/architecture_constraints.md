# Architecture Constraints

This project keeps `Qt >= 5.15` as the host baseline, but the rendering core is not allowed to drift into a Qt-first design.

## Current rules

- `examples/` and `src/engine/quick/` are the Qt host and adapter layer.
- `src/engine/render/` is the rendering core. Public headers in this layer should avoid exposing Qt UI/event types.
- View/input transport types must stay plain data so the core can be driven by non-Qt hosts later if needed.
- Scene extraction owns deciding which proxies exist for a frame. Features consume filtered scene packets instead of assuming terrain-first defaults.
- The render graph must declare resources explicitly before passes read or write them.

## Extension policy

- Add new features only when a concrete rendering requirement exists.
- Prefer extending `RenderProxy`, `RenderScenePacket`, and `RenderGraph` over adding one-off terrain-only branches.
- Any new host-specific behavior belongs in the Qt adapter layer, not in core render headers.
- Every architectural change must come with at least one focused test and must pass the Qt 5.15 build/test baseline.
