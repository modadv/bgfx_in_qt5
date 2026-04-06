# Contributing

Thanks for contributing.

## Before You Start

- Keep changes focused. Separate refactors from behavior changes.
- Prefer small, reviewable pull requests.
- Keep Qt host code inside `src/engine/quick` unless there is a strong reason to move it.
- Avoid adding framework or dependency noise that is not required by the current feature.

## Development Setup

1. Clone the repository and initialize submodules:

```bash
git submodule update --init --recursive
```

2. Install dependencies:

```bash
conan install . -of .build -s build_type=Debug --build=missing
```

3. Configure and build:

```bash
cmake --preset debug
cmake --build --preset debug
```

4. Run tests:

```bash
ctest --preset debug
```

## Contribution Rules

- Preserve Qt `>= 5.15` compatibility.
- Keep the offscreen rendering architecture intact unless the change is explicitly about that boundary.
- Add or update targeted tests for new core behavior when practical.
- Remove dead code and template residue while touching the affected area.
- Do not commit generated build trees, logs, or large benchmark output files.

## Pull Requests

Each pull request should include:

- a short problem statement
- the affected subsystem
- build and test commands used
- screenshots or benchmark output when the change affects rendering behavior

## Commit Style

Use short, imperative commit subjects such as:

- `Tighten scene packet filtering`
- `Document public build workflow`
- `Trim benchmark preset bootstrap path`
