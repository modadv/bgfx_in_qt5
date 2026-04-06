# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and the project aims to follow Semantic Versioning once public releases begin.

## [Unreleased]

### Added

- Public repository metadata and governance files for open-source release readiness.
- A public architecture overview document and contributor workflow documentation.
- Cross-platform CI scaffolding for configure, build, and test on Windows, Linux, and macOS.
- A tracked minimal terrain sample set plus a script for fetching the larger optional USGS benchmark pack.

### Changed

- The Conan recipe was reduced to project-scoped dependencies required by the current engine and examples.
- Public build entrypoints now use curated CMake presets instead of relying on generated user presets.
- The benchmark preset loader now skips missing local benchmark files quietly and reports a compact summary.

