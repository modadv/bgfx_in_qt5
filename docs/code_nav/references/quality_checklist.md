# Code Nav Quality Checklist

## Structural

- `CODE_NAVIGATOR.md` includes feature entry links.
- Feature docs exist at `docs/code_nav/<feature>/CODE_NAV.md`.
- Module-local docs exist at module roots when applicable.

## Feature Doc Completeness

- Has 6 mandatory sections:
  - Feature Boundary
  - Entry Files
  - Control Binding Map
  - Data Flow
  - Edit Playbook
  - Minimal-Load Sequence
- Entry files include QML + C++/service + app wiring where applicable.
- Binding table rows map to concrete symbols.

## Correctness

- All listed paths exist in repository.
- Binding rows can be verified by source search.
- Data flow reflects actual call chain.
- Edit playbook points to ownership files.

## Scope and Clarity

- In-scope/out-of-scope are explicit.
- No unrelated deep dives.
- Path-first wording; avoid long code excerpts.

## Maintainability

- Feature slug naming is stable.
- Root doc remains concise.
- Local docs point back to feature docs.
- Minimal-load sequence is shortest sufficient path.
