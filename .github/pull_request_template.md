## Summary

- What problem does this change solve?
- Which subsystem is affected?

## Validation

- [ ] `conan install . -of .build -s build_type=Debug --build=missing`
- [ ] `cmake --preset debug-msvc` or `cmake --preset debug-ninja`
- [ ] `cmake --build --preset debug-msvc` or `cmake --build --preset debug-ninja`
- [ ] `ctest --preset debug-msvc` or `ctest --preset debug-ninja`

## Notes

- Screenshots, logs, or benchmark output if rendering behavior changed
- Compatibility or architecture constraints that reviewers should keep in mind

