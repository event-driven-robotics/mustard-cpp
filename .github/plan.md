# Plan: C++ DVS Event Viewer (mustard-cpp)

## TL;DR
Port the Python Kivy GUI to C++ using Dear ImGui + GLFW + OpenGL 3.3 + GLAD. The architecture mirrors the Python design — abstract `DataLoader`/`DataStream` base classes expose a time-indexed API so multiple `ViewerPanel` instances can be added/removed and synchronized by a single `TimeController`. Background threading handles chunk-based prefetching. Annotations (starting with bounding boxes) overlay on viewers with interactive drawing support.

---

## Architecture Overview

**GUI stack**: Dear ImGui (docking branch) + GLFW + OpenGL 3.3 + GLAD
**Dependencies**: imgui, glfw, glad (via FetchContent); stb_image (vendored single-header); googletest (FetchContent)
**Build**: CMake 3.21+, FetchContent for all deps. No git submodules.
**Platforms**: Windows + Linux (cross-platform by construction via GLFW/ImGui)

### Core Concepts
- **TimeController**: singleton-like shared state for `current_time`, `start_time`, `end_time`, `is_playing`, `playback_speed`
- **DataStream<T>**: abstract template — wraps a `DataLoader<T>` + `ChunkCache<T>` + callback pipeline. Exposes `getDataAtTime(t)` interface. Viewer is agnostic of format.
- **ViewerPanel**: abstract panel — holds a shared DataStream + AnnotationStore, renders a texture, handles interaction. Multiple panels synchronized by shared TimeController.
- **ChunkCache<T>**: LRU memory-limited cache of time-range chunks. Background thread prefetches chunks around playhead.
- **Annotation**: abstract overlay; `BoundingBox` is first implementation.

---

## Project Structure

```
mustard-cpp/
├── CMakeLists.txt
├── cmake/
│   └── FetchDependencies.cmake
├── include/mustard/
│   ├── core/
│   │   └── TimeController.h
│   ├── data/
│   │   ├── DVSEvent.h              # {int64_t t; uint16_t x, y; bool polarity}
│   │   ├── DataChunk.h             # template<T> chunk with [t_start, t_end], vector<T>
│   │   ├── ChunkCache.h            # template LRU cache with memory budget
│   │   ├── DataLoader.h            # abstract template base + callback pipeline
│   │   ├── DataStream.h            # abstract: getDataAtTime(t) -> optional<DataChunk<T>>
│   │   ├── events/
│   │   │   └── IITDatalogLoader.h  # iitdatalog format (sample: data_samples/iitdatalog/)
│   │   └── image/
│   │       └── StbImageLoader.h
│   ├── annotation/
│   │   ├── Annotation.h            # abstract: render(), serialize()
│   │   ├── BoundingBox.h
│   │   └── AnnotationStore.h       # map<int64_t, vector<Annotation>>
│   ├── viewer/
│   │   ├── ViewerPanel.h           # abstract: draw(), setDataStream(), getAnnotationStore()
│   │   ├── EventViewerPanel.h      # accum events → RGBA texture → ImGui::Image
│   │   └── ImageViewerPanel.h      # stb_image → GL texture → ImGui::Image
│   └── app/
│       └── App.h                   # owns panels, TimeController, media player UI
├── src/
│   ├── core/TimeController.cpp
│   ├── data/
│   │   ├── ChunkCache.cpp
│   │   ├── events/IITDatalogLoader.cpp
│   │   └── image/StbImageLoader.cpp
│   ├── annotation/
│   │   ├── BoundingBox.cpp
│   │   └── AnnotationStore.cpp
│   ├── viewer/
│   │   ├── EventViewerPanel.cpp
│   │   └── ImageViewerPanel.cpp
│   └── app/
│       ├── App.cpp
│       └── main.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_chunk_cache.cpp        # LRU eviction, memory budget
│   ├── test_event_loader.cpp       # load/seek, chunk boundaries (PENDING format spec)
│   ├── test_callback_pipeline.cpp  # filter chain applied in order
│   ├── test_annotation_store.cpp   # add/remove/serialize bboxes
│   └── test_viewer_state.cpp       # viewer time-sync, no rendering
├── data_samples/                   # sample recordings (git-lfs tracked)
│   └── iitdatalog/                 # iitdatalog format sample (data.log + info.log)
└── third_party/
    └── stb/                        # stb_image.h (single-header, vendored)
```

---

## Phases

### Phase 0 — Container Setup *(prerequisite for all phases)*
1. `.devcontainer/Dockerfile`: Ubuntu 24.04 base; install build-essential, cmake, ninja-build, git, pkg-config, libgl1-mesa-dev (Mesa SW renderer), libglu1-mesa-dev, X11 GLFW deps (libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libxi-dev, libxext-dev), Wayland GLFW deps (libwayland-dev, libxkbcommon-dev, wayland-protocols), xvfb (headless testing). Set `ENV LIBGL_ALWAYS_SOFTWARE=1` (no GPU required by default). Set `WORKDIR /workspace`.
2. `.devcontainer/devcontainer.json`: point to Dockerfile; mount `/tmp/.X11-unix` + `DISPLAY` env for X11 GUI forwarding; extensions: ms-vscode.cpptools, ms-vscode.cmake-tools, ms-vscode.cpptools-extension-pack; cmake settings (buildDir=build, configureOnOpen=true); `postCreateCommand`: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
3. Display strategy: default Mesa software renderer (`LIBGL_ALWAYS_SOFTWARE=1`). X11 socket mount works on Linux and Windows+WSLg automatically. Windows+VcXsrv: user sets `DISPLAY=host.docker.internal:0.0` in local env. GPU passthrough: optional `--device /dev/dri` in runArgs (user uncomments if needed).
4. Headless test support: `xvfb-run ctest --test-dir build` for CI/no-display environments.

### Phase 0.5 — Agent / Skill / Prompt Customizations *(parallel with P0; benefits all later phases)*

All files live under `.github/` — git-tracked, team-shared.

**Always-on project instructions**: `.github/copilot-instructions.md` — living project memory: C++17, FetchContent, bitmask decode, DataLoader/DataStream patterns, gtest conventions, devcontainer rules. Updated as conventions solidify.

**File-specific instructions** (`.github/instructions/`):
- `cpp-conventions.instructions.md` — `applyTo: **/*.{h,cpp,hpp}` — RAII, smart pointers, const-correctness, std::optional
- `cmake-conventions.instructions.md` — `applyTo: {**/CMakeLists.txt,**/*.cmake}` — FetchContent patterns, target-based linking
- `test-conventions.instructions.md` — `applyTo: tests/**/*.cpp` — gtest fixture naming, no OpenGL calls, synthetic data only

**Custom Agents** (`.github/agents/`):
1. `data-loader.agent.md` — Implements new `DataLoader<T>` + `DataStream<T>` specializations. Tools: read, edit, search. Will NOT touch viewer/annotation code.
2. `viewer.agent.md` — Implements new `ViewerPanel` subclasses (GL texture, ImGui::Image, event accumulation). Tools: read, edit, search. Will NOT touch data loading code.
3. `annotation.agent.md` — Implements new `Annotation` types + interactive drawing. Knows ImDrawList, mouse drag, AnnotationStore. Tools: read, edit, search.
4. `test-writer.agent.md` — Writes gtest tests using synthetic data/fixtures. Tools: read, edit, search. No execute.

**Skills** (`.github/skills/`):
1. `add-data-format/SKILL.md` — Full workflow: DataChunk specialization → DataLoader → DataStream → factory registration → tests. Includes `assets/DataLoader.template.h`.
2. `add-annotation-type/SKILL.md` — Workflow: extend Annotation → renderOverlay + serialize → drawing mode → registration → tests.
3. `add-viewer-type/SKILL.md` — Workflow: extend ViewerPanel → onTimeChanged + texture + render + overlay → App menu → tests.

**Prompts** (`.github/prompts/`):
1. `generate-loader-tests.prompt.md` — Generate gtest file for a DataLoader class with synthetic binary data.
2. `review-binary-decode.prompt.md` — Audit decode for: no bit-fields, correct masks, little-endian safety.
3. `add-filter-callback.prompt.md` — Scaffold a new processing callback for the DataLoader callback pipeline.

### Phase 1 — Scaffold & Build System *(depends on P0)*
2. Create `cmake/FetchDependencies.cmake`: fetch imgui, glfw, glad, googletest
3. Create `src/app/main.cpp`: minimal GLFW window + OpenGL context + ImGui init/render loop
4. Create `tests/CMakeLists.txt`: link gtest_main, add test executables
5. Verify: `cmake --build` succeeds on Windows and Linux; empty ImGui window opens

### Phase 2 — Core Data Abstractions (*parallel with Phase 1 can be designed, depends on P1 for build*)
1. `DVSEvent.h`: plain struct, no dependencies
2. `DataChunk.h`: template `DataChunk<T>` with `t_start`, `t_end`, `std::vector<T> data`
3. `DataLoader.h`: abstract template base with `open()`, `close()`, `readChunk(t0, t1)`, `addCallback(fn)` (applies `std::vector<std::function<void(DataChunk<T>&)>>` pipeline)
4. `DataStream.h`: abstract template with `getDataAtTime(int64_t t) -> std::optional<DataChunk<T>>`; internally starts background loader thread on open
5. `ChunkCache.h`: template LRU cache keyed by `{t_start, t_end}`; configurable max_bytes; evicts LRU when full
6. `TimeController.h`: `current_time`, `start/end_time`, `is_playing`, `speed`; tick(delta_seconds) advances time; observer list (callbacks)

### Phase 3 — Concrete Loaders *(depends on Phase 2)*

#### iitdatalog Format Spec (now known)
- **Per-line structure**: `packet_num  sec.usec  AE  duration_usec  "«escaped_binary»"`
- **Unescape step**: strip surrounding quotes; resolve `\\`→`\`, `\n`→`0x0A`, `\r`→`0x0D`, `\0`→`0x00`, `\x`→`x` (any other char after backslash). Optimize by pre-reserving output to `src.size()` and using index-based loop (avoid per-char heap alloc of the original code).
- **Event struct (32-bit, ENABLE_TS=0)** — portable bitmask decode of a `uint32_t`:
  - bit 0: `p` (polarity)
  - bits 1–11: `x` (11 bits)
  - bits 12–21: `y` (10 bits)
  - bits 22–25: `channel`, `type`, `skin`, `corner` (metadata, kept in DVSEvent)
  - bits 26–31: fill (ignored)
  - **Do NOT use compiler bit-fields for decode** — use explicit `& mask` shifts for portability
- **Per-event timestamp**: all events in a packet share the packet header timestamp. Convert `sec.usec` string → `int64_t` microseconds.

#### IITDatalogLoader implementation
1. **open()**: scan file line-by-line; for each line parse only the text header fields (read until first `"`); record `PacketIndex{file_offset_of_binary, timestamp_us, line_byte_length}` in `std::vector`. Build O(N_packets) index in memory, O(1) per packet. Store `t_start = index[0].ts`, `t_end = index.back().ts`.
2. **Resolution detection**: after indexing, sample ~50 random packet indices, decode their events, track max x/y. `sensor_width = max_x + 1`, `sensor_height = max_y + 1`. Default fallback: 640×480. Store as `uint16_t sensor_w, sensor_h` on the loader.
3. **readChunk(t0, t1)**: binary-search `PacketIndex` for t0 → `seekg()` to offset → read and process packets until `ts > t1`; return `DataChunk<DVSEvent>`. All events in a packet get the same `t` from the header (no sub-packet ordering).
4. `StbImageLoader` — PNG/JPG at path → `DataChunk<uint8_t>`
5. Unit tests: `test_event_loader.cpp` (load synthetic file bytes, verify event fields), `test_callback_pipeline.cpp`

### Phase 4 — TimeController + Viewer Infrastructure (*depends on Phase 2*)
1. `ViewerPanel.h`: pure virtual `draw(ImDrawList*, ImVec2 origin, ImVec2 size)`, `setStream(...)`, `onTimeChanged(int64_t t)`
2. OpenGL texture helper (in ViewerPanel base): create/update/delete GL texture, upload `uint8_t*` pixels
3. `EventViewerPanel.cpp`: accumulates DVS events in window `[t - accumulation_window, t]` into RGBA frame buffer; polarity→color mapping configurable; uploads as GL_RGBA texture; renders with `ImGui::Image`
4. `ImageViewerPanel.cpp`: fetches image chunk at `t`, uploads as GL_RGB texture
5. Tests: `test_viewer_state.cpp` (no GL — test time sync logic, stream attachment)

### Phase 5 — Annotation System (*parallel with Phase 4*)
1. `Annotation.h`: pure virtual `renderOverlay(ImDrawList*, ImVec2 origin, float scale)`, `serialize()`, `deserialize()`
2. `BoundingBox.h/cpp`: stores `{int64_t t, float x, y, w, h, string label}`
3. `AnnotationStore.cpp`: `std::map<int64_t, std::vector<std::unique_ptr<Annotation>>>` with add/remove/query by time range; JSON-like serialization (simple, no external dep initially)
4. Interactive drawing: each ViewerPanel has `InteractionMode` enum (OBSERVE / DRAW_BBOX). In DRAW_BBOX: track `ImGui::IsItemHovered()` + `IsMouseDown(0)` to capture drag rect; on release, commit to AnnotationStore
5. Tests: `test_annotation_store.cpp`

### Phase 6 — Main App (*depends on Phases 1–5*)
1. `App.cpp`: owns `TimeController`, vector of `unique_ptr<ViewerPanel>`, global annotation store reference
2. Media player toolbar: ImGui buttons (Play/Pause, Stop), time slider (`ImGui::SliderScalar` with int64_t), speed selector
3. ImGui Docking layout: viewers docked in main dockspace, toolbar at bottom
4. Panel management: "Add Viewer" menu → choose type + data file → spawns new panel
5. Playback loop: `TimeController::tick()` called each frame based on `ImGui::GetIO().DeltaTime`; all panels receive `onTimeChanged()` callback

### Phase 7 — Tests & Benchmarks (*depends on all phases*)
1. Complete gtest suite covering all modules
2. Benchmark test: load sample data, measure frame time for event accumulation at 1M events/sec
3. Memory pressure test: verify ChunkCache respects budget, no unbounded growth

---

## Key Files to Create (in order)

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root build |
| `cmake/FetchDependencies.cmake` | All FetchContent declarations |
| `src/app/main.cpp` | GLFW + ImGui entrypoint |
| `include/mustard/core/TimeController.h` | Global playback state |
| `include/mustard/data/DVSEvent.h` | Core event struct |
| `include/mustard/data/DataChunk.h` | Template chunk |
| `include/mustard/data/DataLoader.h` | Abstract loader + callbacks |
| `include/mustard/data/DataStream.h` | Abstract stream |
| `include/mustard/data/ChunkCache.h` | LRU cache |
| `include/mustard/viewer/ViewerPanel.h` | Abstract viewer |
| `include/mustard/annotation/Annotation.h` | Abstract annotation |

---

## Decisions

- **No git submodules**: FetchContent only → cleaner repo, simpler CI
- **OpenGL 3.3 core profile**: broadest hardware support, avoids legacy GL
- **C++17**: `std::optional`, `std::filesystem`, structured bindings
- **ImGui docking branch** (`IMGUI_HAS_DOCK`): enables multi-panel drag-and-drop layout
- **stb_image vendored** (single-header): no CMake complexity, zero deps
- **No third-party JSON lib initially**: hand-rolled annotation serialization to keep deps minimal; can swap to nlohmann later
- **IITDatalogLoader**: format spec known; sample data at `data_samples/iitdatalog/` (tracked via git-lfs)

## Pending / Blockers
- **IITDatalogLoader**: format spec known; implement in Phase 3 using sample at `data_samples/iitdatalog/`
- **Sensor resolution**: needed to pre-allocate event frame buffers (EventViewerPanel). Typical: 346x260 (DAVIS346), 640x480, 1280x720
- **Default accumulation window**: time window for event-to-frame accumulation (suggest: 20ms default, user-configurable per panel)
