#pragma once
#include "mustard/data/events/IITDatalogStream.h"

#include <glad/gl.h>
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
class DVSViewerPanel {
public:
    /// Rolling accumulation window in microseconds (~33 ms ≈ 30 fps).
    static constexpr int64_t kAccumWindowUs = 33'333;

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
};

} // namespace mustard
