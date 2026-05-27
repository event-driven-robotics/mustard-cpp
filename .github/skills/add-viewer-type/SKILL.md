---
name: add-viewer-type
description: "Full workflow for adding a new ViewerPanel subclass to mustard-cpp: subclass ViewerPanel → implement onTimeChanged + GL texture upload + ImGui::Image render + annotation overlay → register in App menu → unit tests (no GL). Use when adding a new sensor view, image view, or custom visualization panel."
---

# Add Viewer Type

## When to Use
- Adding a new panel type (event view, image view, depth map, IMU plot, etc.)
- Adding a new texture format or accumulation strategy
- Wiring a new panel type into the App's "Add Viewer" menu

## Procedure

### Step 1 — Subclass ViewerPanel
1. Create `include/mustard/viewer/<YourPanel>.h`
2. Create `src/viewer/<YourPanel>.cpp`
3. Subclass `ViewerPanel` (see `include/mustard/viewer/ViewerPanel.h`)

### Step 2 — Implement onTimeChanged
```cpp
void YourPanel::onTimeChanged(int64_t t) {
    auto chunk = stream_->getDataAtTime(t);
    if (!chunk) return;
    // Update internal pixel buffer from chunk data
    dirty_ = true;
}
```
- Keep this fast — called every frame during playback
- Store fetched data in a member buffer; set a `dirty_` flag

### Step 3 — Implement GL Texture Upload (in draw())
```cpp
if (dirty_) {
    if (tex_id_ == 0) glGenTextures(1, &tex_id_);
    glBindTexture(GL_TEXTURE_2D, tex_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    dirty_ = false;
}
```
- Use `GL_RGBA` for event frames (polarity → color); `GL_RGB` for image frames
- Generate texture once; update with `glTexImage2D` or `glTexSubImage2D`

### Step 4 — Render with ImGui::Image
```cpp
ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex_id_)),
             size, {0,0}, {1,1});
```

### Step 5 — Render Annotation Overlay
After `ImGui::Image`, iterate `annotationStore_->query(t - window, t + window)` and call `annotation->renderOverlay(dl, origin, scale)`.

### Step 6 — Register in App Menu
1. Open `src/app/App.cpp`
2. In the "Add Viewer" menu handler, add a `MenuItem` for your new panel type
3. On selection: construct a `std::make_unique<YourPanel>(stream, timeController_)` and push to `panels_`

### Step 7 — Write State Tests (no GL)
1. Create or extend `tests/test_viewer_state.cpp`
2. Test `onTimeChanged` logic: mock DataStream returning synthetic chunks; verify internal state updates
3. Test panel attachment/detachment from TimeController
4. No OpenGL calls — ever

## Checklist
- [ ] Header + impl
- [ ] `onTimeChanged` updates buffer + dirty flag
- [ ] Texture created/updated in `draw()` (GL path, not tested)
- [ ] `ImGui::Image` call with correct texture ID
- [ ] Annotation overlay rendered
- [ ] App menu entry added
- [ ] State-only unit tests passing
