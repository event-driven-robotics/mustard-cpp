---
description: "Generate a complete GoogleTest file for a DataLoader class using synthetic binary data. Provide the loader class name and data type."
agent: agent
tools: [read, edit, search]
argument-hint: "DataLoader class name (e.g. IITDatalogLoader)"
---
Generate a complete GoogleTest unit test file for the `$ARGUMENTS` DataLoader class.

Requirements:
- Read the loader's header to understand its public API and the data type `T` it produces
- Read `.github/instructions/test-conventions.instructions.md` before writing any tests
- Fixture: `class $ARGUMENTSTest : public ::testing::Test`
- All test data constructed in-process — no file I/O, no disk reads
  - Build a synthetic binary payload as `std::vector<uint8_t>` or `std::string` matching the format's byte layout
  - Feed it to the loader via a memory stream or a temporary in-memory abstraction
- Cover:
  1. **Happy path**: correct event count, correct field decode (all fields: timestamp, x, y, polarity, etc.)
  2. **Chunk boundary**: `readChunk(t0, t1)` returns only events in `[t0, t1]`
  3. **Callback pipeline**: two callbacks applied in order; verify second sees output of first
  4. **Empty range**: `readChunk` with `t0 > t1` returns `std::nullopt`
  5. **Empty file / malformed input**: graceful failure, no crash
- Use `EXPECT_*` over `ASSERT_*` unless a failure makes subsequent checks meaningless
- Output the complete file content for `tests/test_$ARGUMENTS.cpp`
- Also output the CMakeLists.txt lines needed to register the test executable
