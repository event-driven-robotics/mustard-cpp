# mustard-cpp Project Guidelines

## Project Purpose
C++ DVS Event Viewer — Dear ImGui + GLFW + OpenGL 3.3 + GLAD. Ports a Python Kivy GUI.
Architecture: `DataLoader<T>` → `DataStream<T>` → `ChunkCache<T>` → `ViewerPanel` synchronized by `TimeController`.

See [.github/plan.md](./plan.md) for full architecture, phase plan, and decisions.

## Language & Standard
- **C++17** throughout: `std::optional`, `std::filesystem`, structured bindings, `if constexpr`
- No C++20 features; no compiler extensions

## Build System
- **CMake 3.21+** with **FetchContent** for all third-party deps (imgui, glfw, glad, googletest)
- **No git submodules** — FetchContent only
- Single-header `stb_image.h` vendored under `third_party/stb/`
- Build dir: `build/`; configure: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`
- All FetchContent declarations live in `cmake/FetchDependencies.cmake`
- Link libraries via **target-based linking** (`target_link_libraries` with `PRIVATE`/`PUBLIC`)

## Core Architecture Patterns

### DataLoader<T>
Abstract template base. Subclasses implement `open()`, `close()`, `readChunk(t0, t1)`.
Callback pipeline: `std::vector<std::function<void(DataChunk<T>&)>>` applied in order inside `readChunk`.
See `.github/skills/add-data-format/assets/DataLoader.template.h` for the canonical template.

### DataStream<T>
Abstract template; `getDataAtTime(int64_t t) -> std::optional<DataChunk<T>>`.
Owns a `DataLoader<T>` + `ChunkCache<T>`; background thread prefetches chunks around the playhead.

### ChunkCache<T>
LRU cache keyed by `{t_start, t_end}` with configurable `max_bytes` budget. Evicts LRU on overflow.

### TimeController
Singleton-like shared state: `current_time`, `start_time`, `end_time`, `is_playing`, `playback_speed`.
`tick(delta_seconds)` advances time. Observer list of callbacks.

### ViewerPanel
Abstract panel. Pure virtual: `draw(ImDrawList*, ImVec2 origin, ImVec2 size)`, `onTimeChanged(int64_t t)`.
Owns a shared DataStream + AnnotationStore. Multiple panels share one TimeController.

## Binary Decode Rules (DVS Events)
- **Never use compiler bit-fields** — always explicit `& mask` + shift for portability
- DVSEvent 32-bit layout (ENABLE_TS=0): bit 0 = polarity, bits 1–11 = x (11 bits), bits 12–21 = y (10 bits), bits 22–25 = metadata
- Timestamps: `sec.usec` text header → `int64_t` microseconds
- Unescape: `\\`→`\`, `\n`→`0x0A`, `\r`→`0x0D`, `\0`→`0x00`, `\x`→`x`. Pre-reserve output to `src.size()`.

## Code Style
- **RAII everywhere**: no raw `new`/`delete`; use `std::unique_ptr` / `std::shared_ptr`
- **const-correctness**: const on member functions and parameters where applicable
- `std::optional<T>` for nullable returns; never return raw null pointers
- Smart pointer ownership: `unique_ptr` for exclusive ownership, `shared_ptr` for shared (e.g., DataStream passed to multiple panels)
- No `using namespace std` in headers

## Testing Conventions
- **GoogleTest** (FetchContent); test executables in `tests/`
- No OpenGL calls in tests — test logic, not rendering
- Synthetic data only: construct `DataChunk` / `DVSEvent` vectors in-process; no file I/O in unit tests
- Fixture naming: `class <Module>Test : public ::testing::Test`
- See `.github/instructions/test-conventions.instructions.md` for full rules

## DevContainer
- Ubuntu 24.04; Mesa SW renderer (`LIBGL_ALWAYS_SOFTWARE=1`); no GPU required
- Headless tests: `xvfb-run ctest --test-dir build`
- X11 GUI forwarding via `/tmp/.X11-unix` socket mount
