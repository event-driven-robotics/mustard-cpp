---
description: "Use when writing or reviewing GoogleTest unit tests for mustard-cpp. Covers fixture naming, no-OpenGL rule, synthetic data, and test organization."
applyTo: "tests/**/*.cpp"
---
# Test Conventions — mustard-cpp

## Framework
- **GoogleTest** (linked via FetchContent as `GTest::gtest_main`)
- All test files live under `tests/`; each module gets its own file (`test_<module>.cpp`)

## Fixture Naming
```cpp
class ChunkCacheTest : public ::testing::Test { ... };
class EventLoaderTest : public ::testing::Test { ... };
```
Pattern: `class <Module>Test : public ::testing::Test`

## No OpenGL in Tests
- **Never call any OpenGL, GLFW, or ImGui rendering functions** in tests
- Test logic only: data structures, algorithms, state transitions
- If a class has both logic and GL: extract the logic into a testable helper or test via the non-GL interface

## Synthetic Data Only
- **No file I/O**: construct `DataChunk<T>` / `DVSEvent` vectors directly in test code
- Build binary payloads in memory (e.g., `std::vector<uint8_t>`) to test parsers
- No dependency on sample files in `samples/` — those are for benchmarks only

## Test Structure
```cpp
TEST_F(ChunkCacheTest, EvictsLRUWhenBudgetExceeded) {
    // Arrange
    ChunkCache<DVSEvent> cache{/*max_bytes=*/1024};
    // Act
    cache.insert(...);
    // Assert
    EXPECT_EQ(cache.size(), expected);
}
```
- Use `EXPECT_*` over `ASSERT_*` unless the failure makes subsequent checks meaningless
- One logical concept per test case; descriptive names in `PascalCase`

## CMake Registration
```cmake
add_executable(test_chunk_cache test_chunk_cache.cpp)
target_link_libraries(test_chunk_cache PRIVATE mustard GTest::gtest_main)
add_test(NAME test_chunk_cache COMMAND test_chunk_cache)
```

## Running Tests
```bash
# With display (runs normally)
ctest --test-dir build

# Headless / CI
xvfb-run ctest --test-dir build
```
