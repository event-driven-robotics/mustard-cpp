#pragma once
#include "mustard/annotation/AnnotationStore.h"
#include "imgui.h"
#include <cstdint>
#include <memory>
#include <string>

namespace mustard {

/// Annotation type selector — more types will follow.
enum class AnnotationType {
    kBoundingBox,
};

/// Abstract base class for all viewer panels.
///
/// Concrete panels inherit from this class and implement draw() and
/// onTimeChanged().  The TimeController notifies every registered panel
/// via onTimeChanged(); each panel manages its own rendering state.
///
/// Annotation controls (Annotate / Stop / Save), bounding-box drag interaction,
/// and overlay rendering are all handled here so no panel subclass needs to
/// duplicate that logic.
class ViewerPanel {
public:
    explicit ViewerPanel(std::string label) : label_(std::move(label)) {}
    virtual ~ViewerPanel() = default;

    // Non-copyable; moveable (explicitly defaulted because the virtual
    // destructor suppresses the implicit move constructor).
    ViewerPanel(const ViewerPanel&)            = delete;
    ViewerPanel& operator=(const ViewerPanel&) = delete;
    ViewerPanel(ViewerPanel&&)                 = default;
    ViewerPanel& operator=(ViewerPanel&&)      = default;

    /// Emit all ImGui commands for this panel (one standalone window).
    virtual void draw() = 0;

    /// Called by the TimeController observer on every time change.
    virtual void onTimeChanged(int64_t t) = 0;

    /// First timestamp of the underlying data stream in its native time base (µs).
    virtual int64_t streamStartUs() const noexcept = 0;

    /// Last timestamp of the underlying data stream in its native time base (µs).
    virtual int64_t streamEndUs() const noexcept = 0;

    const std::string& label()  const noexcept { return label_; }
    /// Returns false after the user clicks the window's close (✕) button.
    bool               isOpen() const noexcept { return open_; }

    /// Set the global timeline offset (µs) at which this panel's data begins.
    /// onTimeChanged receives global time t; the panel converts to stream time as:
    ///   stream_t = t - start_offset_us + streamStartUs()
    void setStartOffset(int64_t offset_us) noexcept { start_offset_us_ = offset_us; }

    /// Attach an AnnotationStore.  Annotations are rendered as overlays and
    /// can be saved via the toolbar Save button.
    void setAnnotationStore(std::shared_ptr<AnnotationStore> store);

protected:
    std::string label_;
    bool        open_{true};
    int64_t     start_offset_us_{0};

    // ------------------------------------------------------------------
    // Annotation state — shared across all panel types
    // ------------------------------------------------------------------

    std::shared_ptr<AnnotationStore> ann_store_;
    bool           annotating_{false};
    AnnotationType annotation_type_{AnnotationType::kBoundingBox};

    // Drag state for bbox drawing
    ImVec2 drag_start_{0.f, 0.f};
    bool   dragging_{false};

    /// Render the annotation toolbar row (Annotate / Stop / Save).
    /// Call once per frame from within an ImGui::Begin…End block.
    void drawAnnotationControls();

    /// Handle bbox click-drag interaction and commit to ann_store_.
    /// Call immediately after ImGui::Image when the image is valid.
    /// @param img_origin  Top-left of the image in screen space.
    /// @param scale       Pixels-per-sensor-unit scale factor.
    /// @param t           Current timestamp in microseconds.
    void drawAnnotationInteraction(ImVec2 img_origin, float scale, int64_t t);

    /// Render all annotations from ann_store_ at timestamp @p t as overlay.
    /// Call after ImGui::Image (and after interaction) when the image is valid.
    void drawAnnotationOverlay(ImVec2 img_origin, float scale, int64_t t) const;
};

} // namespace mustard

