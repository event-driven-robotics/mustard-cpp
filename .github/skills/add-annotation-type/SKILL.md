---
name: add-annotation-type
description: "Full workflow for adding a new Annotation type to mustard-cpp: subclass Annotation → implement renderOverlay + serialize/deserialize → add interactive drawing mode to ViewerPanel → register in AnnotationStore → unit tests. Use when adding bounding boxes, polylines, keypoints, or any new overlay type."
---

# Add Annotation Type

## When to Use
- Adding a new annotation shape (bounding box, polyline, keypoint, polygon, etc.)
- Extending the interactive drawing system with a new draw mode
- Adding serialization for a new annotation type

## Procedure

### Step 1 — Subclass Annotation
1. Create `include/mustard/annotation/<YourAnnotation>.h`
2. Create `src/annotation/<YourAnnotation>.cpp`
3. Subclass `Annotation` (see `include/mustard/annotation/Annotation.h`)
4. Required fields: `int64_t t` (timestamp), shape-specific data (e.g. `float x, y, w, h`), `std::string label`
5. Implement pure virtuals:
   - `renderOverlay(ImDrawList* dl, ImVec2 origin, float scale)` — draw onto ImDrawList using ImGui draw API; scale coordinates by `scale` from `origin`
   - `std::string serialize() const` — emit a simple text/key-value representation
   - `static std::unique_ptr<Annotation> deserialize(const std::string&)` — parse it back

### Step 2 — Register in AnnotationStore
1. Open `src/annotation/AnnotationStore.cpp`
2. Add a `deserialize` branch that recognizes your type tag and calls `YourAnnotation::deserialize(...)`

### Step 3 — Add Interactive Drawing Mode (optional)
1. Open `include/mustard/viewer/ViewerPanel.h`; add your mode to the `InteractionMode` enum
2. In the relevant `ViewerPanel::draw()` (e.g., `EventViewerPanel`):
   - When mode is active and `ImGui::IsItemHovered()`:
     - `IsMouseClicked(0)` → record start point
     - `IsMouseDown(0)` → draw a preview rect/shape on `ImDrawList`
     - `IsMouseReleased(0)` → commit a new `YourAnnotation` to `AnnotationStore::add(t, ...)`

### Step 4 — Write Unit Tests
1. Create or extend `tests/test_annotation_store.cpp`
2. Construct annotation objects directly (no GL, no file I/O)
3. Verify: add, query by time range, remove, serialize → deserialize round-trip
4. Add to `tests/CMakeLists.txt` if creating a new test file

## Checklist
- [ ] Header + impl for new annotation type
- [ ] `renderOverlay` draws correctly scaled to `origin` + `scale`
- [ ] `serialize` / `deserialize` round-trip verified in tests
- [ ] `AnnotationStore` deserialize branch added
- [ ] `InteractionMode` entry + drawing logic (if interactive)
- [ ] Unit tests passing
