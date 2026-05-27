---
name: add-data-format
description: "Full workflow for adding a new data format to mustard-cpp: DataChunk specialization → concrete DataLoader<T> → DataStream<T> → factory registration → unit tests. Use when adding a new file format, sensor type, or data source. Includes the canonical DataLoader template."
---

# Add Data Format

## When to Use
- Adding a new file format (binary events, CSV, HDF5, etc.)
- Adding a new sensor type requiring a custom `DataLoader<T>` specialization
- Wiring a new loader into `ChunkCache` and the callback pipeline

## Assets
- [DataLoader.template.h](./assets/DataLoader.template.h) — canonical template to copy and adapt

## Procedure

### Step 1 — Define the Data Type (if new)
If `T` is a new struct (not `DVSEvent`, `uint8_t`, etc.):
1. Create `include/mustard/data/<YourType>.h`
2. Plain struct, no dependencies, C++17, no raw pointers
3. Example:
   ```cpp
   #pragma once
   #include <cstdint>
   struct YourEvent { int64_t t; float value; };
   ```

### Step 2 — Implement the Concrete DataLoader
1. Copy [DataLoader.template.h](./assets/DataLoader.template.h) to `include/mustard/data/<format>/<FormatLoader>.h`
2. Create `src/data/<format>/<FormatLoader>.cpp`
3. Implement:
   - `open()`: parse file header, build packet index (`std::vector<PacketIndex>`)
   - `close()`: close file handle, clear index
   - `readChunk(int64_t t0, int64_t t1)`: binary-search index for `t0`, seek, decode packets until `ts > t1`; apply callback pipeline; return `DataChunk<T>`
4. **Binary decode rules** (see [cpp-conventions](./../instructions/cpp-conventions.instructions.md)):
   - Never use compiler bit-fields
   - Use explicit `& mask` + shift
   - Unescape with index-based loop, pre-reserve to `src.size()`

### Step 3 — Implement the Concrete DataStream
1. Create `include/mustard/data/<format>/<FormatStream>.h`
2. Subclass `DataStream<T>`; constructor takes a `std::shared_ptr<FormatLoader>`
3. Override `getDataAtTime(int64_t t) -> std::optional<DataChunk<T>>`; check cache first, then call loader, insert to cache
4. Start background prefetch thread in `open()`

### Step 4 — Wire into ChunkCache
- Cache is keyed by `{t_start, t_end}` (use a pair or small struct as the key)
- On cache miss in `getDataAtTime`: call `loader_->readChunk(...)`, insert result, return it
- On cache hit: return cached chunk directly

### Step 5 — Register in Factory (if a loader factory exists)
- Add a `case` or `if` block mapping a format string/enum to `std::make_shared<FormatLoader>(...)`

### Step 6 — Write Unit Tests
1. Create `tests/test_<format>_loader.cpp`
2. Build a synthetic binary payload in memory (no file I/O)
3. Instantiate the loader with the in-memory data
4. Verify: correct event count, correct field decode (x, y, polarity, timestamp), chunk boundaries, callback pipeline invocation order
5. Add to `tests/CMakeLists.txt`:
   ```cmake
   add_executable(test_<format>_loader test_<format>_loader.cpp)
   target_link_libraries(test_<format>_loader PRIVATE mustard GTest::gtest_main)
   add_test(NAME test_<format>_loader COMMAND test_<format>_loader)
   ```

## Checklist
- [ ] New type header (if needed)
- [ ] Loader header + impl
- [ ] Stream header + impl
- [ ] Callback pipeline tested
- [ ] Unit tests: happy path, boundary, malformed input
- [ ] CMakeLists updated
