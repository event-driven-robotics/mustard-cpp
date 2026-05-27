---
description: "Use when writing or reviewing CMakeLists.txt or .cmake files in mustard-cpp. Covers FetchContent patterns, target-based linking, and build conventions."
applyTo: "{**/CMakeLists.txt,**/*.cmake}"
---
# CMake Conventions — mustard-cpp

## Version & Policies
- Minimum version: `cmake_minimum_required(VERSION 3.21)`
- Set `CMAKE_CXX_STANDARD 17` and `CMAKE_CXX_STANDARD_REQUIRED ON` at the top level

## Dependencies — FetchContent Only
- **No git submodules**: all third-party deps use `FetchContent`
- All `FetchContent_Declare` calls live in `cmake/FetchDependencies.cmake`; include it from the root `CMakeLists.txt`
- Always pin a specific `GIT_TAG` (commit hash preferred) for reproducibility
- Exception: `third_party/stb/stb_image.h` is vendored (single-header, no CMake logic needed)

## Target-Based Linking
- Always use `target_link_libraries` with `PRIVATE` or `PUBLIC` — never bare `link_libraries`
- Prefer `PRIVATE` unless the dependency is part of the public API (exposed through headers)
- Use `target_include_directories` with `PUBLIC` for headers in `include/` so dependents inherit paths

## Build Layout
- Build directory: `build/`
- Configure command: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- Test executables defined in `tests/CMakeLists.txt`, linked via `add_subdirectory(tests)`

## Naming
- Main library target: `mustard` (or per-module targets as needed)
- Test executables: `test_<module>` (e.g., `test_chunk_cache`, `test_event_loader`)
- Use `add_executable` + `target_link_libraries(test_foo PRIVATE mustard GTest::gtest_main)`

## Common Patterns
```cmake
# FetchContent pattern
include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        <commit-sha>
)
FetchContent_MakeAvailable(imgui)

# Target linking pattern
target_link_libraries(mustard_app PRIVATE mustard glfw imgui glad)
```
