#pragma once
#include "mustard/ui/ViewerPanel.h"
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
class DVSViewerPanel : public ViewerPanel {
public:
    /// Rolling accumulation window in microseconds (~33 ms ≈ 30 fps).
    static constexpr int64_t kAccumWindowUs = 33'333;

    /// Controls how accumulated events are visualised in the panel.
    enum class RepresentationMode {
        kHistogram,    ///< Colour-coded event accumulation (default).
        kTimeSurface,  ///< Per-pixel recency heatmap (green=ON, red=OFF).
        kTernaryImage, ///< Three states: ON=white / OFF=black / none=grey.
    };

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
    void onTimeChanged(int64_t t) override;

    /// Draw this panel as a standalone ImGui window.
    void draw() override;

    // label() and isOpen() are inherited from ViewerPanel.

    // ------------------------------------------------------------------
    // Annotation API
    // ------------------------------------------------------------------

    /// Attach an AnnotationStore.  Annotations at the current timestamp are
    /// always rendered as overlays regardless of the interaction mode.
    void setAnnotationStore(std::shared_ptr<AnnotationStore> store);

    /// Switch interaction mode.
    void            setInteractionMode(InteractionMode mode) noexcept;
    InteractionMode interactionMode()                  const noexcept;

    /// Set the rolling accumulation window length (microseconds).
    void    setAccumWindow(int64_t us) noexcept { accum_window_us_ = us; last_time_ = -1; }
    int64_t accumWindow()        const noexcept { return accum_window_us_; }

    /// Set/get the event stream representation mode.
    void               setRepresentationMode(RepresentationMode mode) noexcept;
    RepresentationMode representationMode() const noexcept;

private:
    void ensureTexture(int w, int h);
    void uploadTexture();
    void clearPixels();
    void paintEvent(int x, int y, bool polarity);
    void renderHistogram(int64_t ct_start, int64_t accum_t0, int64_t t_now);
    void renderTimeSurface(int64_t ct_start, int64_t accum_t0, int64_t t_now);
    void renderTernaryImage(int64_t ct_start, int64_t accum_t0, int64_t t_now);

    std::shared_ptr<IITDatalogStream> stream_;

    GLuint               texture_id_{0};
    int                  tex_w_{0};
    int                  tex_h_{0};
    std::vector<uint8_t> pixels_; ///< RGBA row-major pixel buffer

    int64_t last_time_{-1};
    int64_t accum_window_us_{kAccumWindowUs};

    RepresentationMode               rep_mode_{RepresentationMode::kHistogram};
    std::vector<float>               aux_surface_;   ///< Per-pixel float workspace.
    std::vector<int8_t>              aux_polarity_;  ///< Per-pixel last polarity: -1=none, 0=OFF, 1=ON.

    // Annotation state
    std::shared_ptr<AnnotationStore> ann_store_;
    InteractionMode                  mode_{InteractionMode::kObserve};
    ImVec2                           drag_start_{0.f, 0.f};
    bool                             dragging_{false};
};

} // namespace mustard
