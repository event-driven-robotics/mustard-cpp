#pragma once
#include "mustard/ui/ViewerPanel.h"
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/data/events/DVSEventStream.h"

#include <glad/gl.h>
#include "imgui.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mustard {

/// ImGui panel that renders accumulated DVS events as a colour-coded texture.
///
/// Event colours and background are selected with EventTheme.
///
/// Events are accumulated in a rolling window of kAccumWindowUs ending at
/// the current playhead time.
class DVSViewerPanel : public ViewerPanel {
public:
    /// Rolling accumulation window in microseconds (~33 ms ≈ 30 fps).
    static constexpr int64_t kAccumWindowUs = 33'333;

    /// Controls how accumulated events are visualised in the panel.
    enum class RepresentationMode {
        kHistogram,    ///< Colour-coded event accumulation (default).
        kTimeSurface,  ///< Per-pixel recency heatmap using the selected palette.
        kTernaryImage, ///< Last polarity at each pixel, using the selected palette.
    };

    /// Colour palette used to render DVS events.
    enum class EventTheme {
        kJaer, ///< Dark background, green ON events, red OFF events.
        kEdpr, ///< White background, green ON events, purple OFF events.
    };

    explicit DVSViewerPanel(std::shared_ptr<DVSEventStream> stream,
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

    int64_t streamStartUs() const noexcept override{return stream_->startTime();};
    int64_t streamEndUs()   const noexcept override{return stream_->endTime();};

    // label() and isOpen() are inherited from ViewerPanel.

    // ------------------------------------------------------------------
    // Annotation API
    // ------------------------------------------------------------------

    /// Set the rolling accumulation window length (microseconds).
    void    setAccumWindow(int64_t us) noexcept { accum_window_us_ = us; last_time_ = -1; }
    int64_t accumWindow()        const noexcept { return accum_window_us_; }

    /// Set/get the event stream representation mode.
    void               setRepresentationMode(RepresentationMode mode) noexcept;
    RepresentationMode representationMode() const noexcept;

    /// Set/get the Histogram event count that reaches full brightness (1–65535).
    /// The lower of this threshold and the frame peak maps to 255.
    void     setHistogramSaturationCount(uint16_t count) noexcept;
    uint16_t histogramSaturationCount() const noexcept { return histogram_saturation_count_; }

    /// Set/get the colour palette used by all event representations.
    void       setEventTheme(EventTheme theme) noexcept;
    EventTheme eventTheme() const noexcept;

private:
    void beginVideoExport() override;
    void endVideoExport() override;
    bool renderFrameForExport(int64_t stream_time_us,
                              std::vector<uint8_t>& rgba,
                              int& width, int& height) override;
    void ensureTexture(int w, int h);
    void uploadTexture();
    void clearPixels();
    void paintEvent(int x, int y, bool polarity);
    void renderHistogram(int64_t ct_start, int64_t accum_t0, int64_t t_now);
    void renderTimeSurface(int64_t ct_start, int64_t accum_t0, int64_t t_now);
    void renderTernaryImage(int64_t ct_start, int64_t accum_t0, int64_t t_now);

    std::shared_ptr<DVSEventStream> stream_;

    GLuint               texture_id_{0};
    int                  tex_w_{0};
    int                  tex_h_{0};
    std::vector<uint8_t> pixels_; ///< RGBA row-major pixel buffer

    int64_t last_time_{-1};
    int64_t accum_window_us_{kAccumWindowUs};
    uint16_t histogram_saturation_count_{255};
    EventTheme event_theme_{EventTheme::kJaer};

    // Annotation interaction state
    RepresentationMode               rep_mode_{RepresentationMode::kHistogram};
    std::vector<float>               aux_surface_;   ///< Per-pixel float workspace.
    std::vector<int8_t>              aux_polarity_;  ///< Per-pixel last polarity: -1=none, 0=OFF, 1=ON.

    // Frozen at export start so live controls cannot alter an in-flight file.
    bool                 export_settings_frozen_{false};
    int64_t              export_accum_window_us_{kAccumWindowUs};
    uint16_t             export_saturation_count_{255};
    EventTheme           export_event_theme_{EventTheme::kJaer};
    RepresentationMode   export_rep_mode_{RepresentationMode::kHistogram};
};

} // namespace mustard
