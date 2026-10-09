#pragma once
#include "mustard/annotation/AnnotationStore.h"
#include "imgui.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <optional>

namespace mustard {

/// Annotation type selector — more types will follow.
enum class AnnotationType {
    kBoundingBox,
    kEyeTracking,
    kPoint,
};

struct VideoExportSettings {
    int64_t start_us{0};
    int64_t end_us{0};
    int     fps{30};
    bool    include_annotations{true};
    bool    lossless{false};
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
    explicit ViewerPanel(std::string label);
    virtual ~ViewerPanel();

    // Non-copyable; moveable (explicitly defaulted because the virtual
    // destructor suppresses the implicit move constructor).
    ViewerPanel(const ViewerPanel&)            = delete;
    ViewerPanel& operator=(const ViewerPanel&) = delete;
    ViewerPanel(ViewerPanel&&);
    ViewerPanel& operator=(ViewerPanel&&);

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
    std::shared_ptr<AnnotationStore> annotationStore() const noexcept { return ann_store_; }
    void setAnnotationFileStem(std::string stem) { annotation_file_stem_ = std::move(stem); }
    void setTimelineSeekCallback(std::function<void(int64_t)> callback) {
        timeline_seek_callback_ = std::move(callback);
    }

protected:
    std::string label_;
    bool        open_{true};
    int64_t     start_offset_us_{0};

    // ------------------------------------------------------------------
    // Annotation state — shared across all panel types
    // ------------------------------------------------------------------

    std::shared_ptr<AnnotationStore> ann_store_;
    std::string export_status_;
    float       export_progress_{0.f};
    bool        exporting_{false};
    bool           annotating_{false};
    bool           interpolation_enabled_{false};
    AnnotationType annotation_type_{AnnotationType::kBoundingBox};

    // Drag state for annotation drawing
    ImVec2 drag_start_{0.f, 0.f};
    bool   dragging_{false};
    enum class AnnotationDragMode { kNone, kCreate, kMove, kResize, kOrient };
    AnnotationDragMode annotation_drag_mode_{AnnotationDragMode::kNone};

    struct AnnotationSelection {
        bool active{false};
        int64_t timestamp{0};
        std::string type;
        std::string label;
    } selected_;
    std::array<char, 128> annotation_label_{};
    struct AnnotationHistoryEntry {
        std::string state;
        std::optional<int64_t> affected_time_us;
    };
    std::vector<AnnotationHistoryEntry> undo_history_;
    std::vector<AnnotationHistoryEntry> redo_history_;
    std::function<void(int64_t)> timeline_seek_callback_;
    int64_t current_annotation_time_us_{-1};
    std::string drag_history_snapshot_;
    float draft_x_{0.f}, draft_y_{0.f}, draft_w_{0.f}, draft_h_{0.f};
    float drag_base_x_{0.f}, drag_base_y_{0.f}, drag_base_w_{0.f}, drag_base_h_{0.f};
    int bbox_resize_x_{1}, bbox_resize_y_{1};

    float   eye_draft_phi_{0.f};
    float   eye_draft_theta_{0.f};
    float   eye_draft_center_x_{0.f};
    float   eye_draft_center_y_{0.f};
    float   eye_draft_radius_{100.f};
    float   eye_drag_start_center_x_{0.f};
    float   eye_drag_start_center_y_{0.f};
    int annotation_image_width_{0};
    int annotation_image_height_{0};

    void setAnnotationImageSize(int width, int height) noexcept {
        annotation_image_width_ = width;
        annotation_image_height_ = height;
    }

    /// Annotation files always use time relative to the beginning of their
    /// stream, regardless of the stream's position on the global timeline.
    int64_t annotationTimeUs(int64_t global_time_us) const noexcept {
        return global_time_us - start_offset_us_;
    }

    /// Render the annotation toolbar row (Annotate / Stop / Save).
    /// Call once per frame from within an ImGui::Begin…End block.
    void drawAnnotationControls(int64_t annotation_time_us);

    /// Produce the panel's native RGBA rendering at a stream-local timestamp.
    /// The shared exporter encodes these frames into an MP4.
    virtual bool renderFrameForExport(int64_t stream_time_us,
                                      std::vector<uint8_t>& rgba,
                                      int& width, int& height);
    virtual void beginVideoExport();
    virtual void endVideoExport();

    VideoExportSettings export_settings_;
    bool export_settings_initialized_{false};

    /// Handle bbox click-drag interaction and commit to ann_store_.
    /// Call immediately after ImGui::Image when the image is valid.
    /// @param img_origin  Top-left of the image in screen space.
    /// @param scale       Pixels-per-sensor-unit scale factor.
    /// @param t           Current timestamp in microseconds.
    void drawAnnotationInteraction(ImVec2 img_origin, float scale, int64_t t);

    /// Render all annotations from ann_store_ at timestamp @p t as overlay.
    /// Call after ImGui::Image (and after interaction) when the image is valid.
    void drawAnnotationOverlay(ImVec2 img_origin, float scale, int64_t t) const;

    void pushAnnotationHistory(const std::string& before);
    void undoAnnotation();
    void redoAnnotation();
    void clearAnnotationSelection();

private:
    struct ExportJob;
    bool startVideoExport(const std::string& output_path, std::string& error);
    void advanceVideoExport();
    std::unique_ptr<ExportJob> export_job_;
    std::string annotation_file_stem_{"annotations"};
    std::string pending_save_directory_;
    bool save_interpolated_values_{false};
    int interpolation_save_fps_{30};
};

} // namespace mustard
