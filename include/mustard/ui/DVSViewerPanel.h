#pragma once
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/data/events/IITDatalogStream.h"

#include <glad/gl.h>
#include "imgui.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mustard {

/// ImGui panel that renders accumulated DVS events as a colour-coded texture.
///
/// ON events  (polarity=true)  → green pixels.
/// OFF events (polarity=false) → red pixels.
/// Background                  → dark grey.
///
/// Events are accumulated in a rolling window of kAccumWindowUs ending at
/// the current playhead time.
///
/// When an AnnotationStore is attached and mode is kDrawBBox the user can
/// interactively drag a bounding box onto the sensor image.
class DVSViewerPanel {
public:
    /// Rolling accumulation window in microseconds (~33 ms ≈ 30 fps).
    static constexpr int64_t kAccumWindowUs = 33'333;

    /// Interaction mode controlling what happens when the user clicks/drags
    /// inside the sensor image area.
    enum class InteractionMode {
        kObserve,   ///< Default: no drawing, annotations are displayed read-only.
        kDrawBBox,  ///< Click-drag to create a BoundingBox annotation.
    };

    explicit DVSViewerPanel(std::shared_ptr<IITDatalogStream> stream,
                            std::string                        label);
    ~DVSViewerPanel();

    DVSViewerPanel(const DVSViewerPanel&)            = delete;
    DVSViewerPanel& operator=(const DVSViewerPanel&) = delete;
    DVSViewerPanel(DVSViewerPanel&&)                 = default;
    DVSViewerPanel& operator=(DVSViewerPanel&&)      = default;

    /// Called by the TimeController observer; repaints the event texture.
    void onTimeChanged(int64_t t);

    /// Draw this panel as a standalone ImGui window.
    void draw();

    const std::string& label() const noexcept { return label_; }

    // ------------------------------------------------------------------
    // Annotation API
    // ------------------------------------------------------------------

    /// Attach an AnnotationStore.  Annotations at the current timestamp are
    /// always rendered as overlays regardless of the interaction mode.
    void setAnnotationStore(std::shared_ptr<AnnotationStore> store);

    /// Switch interaction mode.
    void            setInteractionMode(InteractionMode mode) noexcept;
    InteractionMode interactionMode()                  const noexcept;

private:
    void ensureTexture(int w, int h);
    void uploadTexture();
    void clearPixels();
    void paintEvent(int x, int y, bool polarity);

    std::shared_ptr<IITDatalogStream> stream_;
    std::string                        label_;

    GLuint               texture_id_{0};
    int                  tex_w_{0};
    int                  tex_h_{0};
    std::vector<uint8_t> pixels_; ///< RGBA row-major pixel buffer

    int64_t last_time_{-1};

    // Annotation state
    std::shared_ptr<AnnotationStore> ann_store_;
    InteractionMode                  mode_{InteractionMode::kObserve};
    ImVec2                           drag_start_{0.f, 0.f};
    bool                             dragging_{false};
};

} // namespace mustard
