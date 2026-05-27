---
description: "Use when writing or reviewing C++ headers or source files in mustard-cpp. Covers RAII, smart pointers, const-correctness, std::optional, and binary decode rules."
applyTo: "**/*.{h,cpp,hpp}"
---
# C++ Conventions — mustard-cpp

## Ownership & Memory
- **No raw `new`/`delete`**: use `std::unique_ptr` (exclusive) or `std::shared_ptr` (shared, e.g., DataStream passed to multiple panels)
- RAII everywhere: resources acquired in constructor, released in destructor
- Prefer stack allocation; heap only when lifetime extends beyond scope

## Const-Correctness
- Mark member functions `const` when they do not mutate state
- Pass by `const T&` for non-trivial types; by value for cheap types (`int`, `bool`, `int64_t`)
- `const` on local variables when not reassigned

## Nullable Returns
- Return `std::optional<T>` instead of raw pointers or sentinel values
- Never return a raw null pointer from public API

## Headers
- No `using namespace std` in headers — always qualify (`std::vector`, `std::optional`)
- Use `#pragma once` as the include guard
- Forward-declare when a full type is not needed (pointer/reference only)

## Templates (DataLoader<T>, DataStream<T>, ChunkCache<T>)
- Template definitions must be visible at instantiation: keep in `.h` or use explicit instantiation in `.cpp`
- Use `static_assert` to document type constraints where helpful

## Binary Decode (DVSEvents)
- **Never use compiler bit-fields** — they are not portable across compilers/platforms
- Always decode with explicit masks and shifts:
  ```cpp
  bool polarity = (raw & 0x1u);
  uint16_t x    = static_cast<uint16_t>((raw >> 1) & 0x7FFu);
  uint16_t y    = static_cast<uint16_t>((raw >> 12) & 0x3FFu);
  ```
- Unescape binary strings with an index-based loop; pre-reserve output to `src.size()` to avoid per-char heap allocation

## Error Handling
- Use `std::optional` / early returns for expected failure cases (file not found, parse error)
- Reserve exceptions for truly unrecoverable situations; document `noexcept` where applicable
- Do not silently swallow errors — log or propagate
