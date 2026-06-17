#pragma once
#include "mustard/ui/ViewerPanel.h"

#include <glad/gl.h>
#include "imgui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mustard {

/// ImGui panel that decodes an MP4 (or any FFmpeg-supported) video file and
/// renders each frame as an OpenGL texture, synchronised to the global
/// TimeController timeline.
///
/// FFmpeg types are hidden behind a PIMPL struct (FFmpegCtx) so callers do
/// not need to include FFmpeg headers.
///
/// Time mapping
/// ------------
/// start_offset_us (inherited from ViewerPanel) is the global time (µs) at
/// which video frame 0 is displayed:
///   video_time_us = global_t - start_offset_us_
/// Call setStartOffset() after construction to align with other streams.
class RGBVideoPanel : public ViewerPanel {
public:
    /// @param filepath  Path to the video file.
    /// @param label     ImGui window title.
    explicit RGBVideoPanel(std::string filepath,
                           std::string label,
                           std::function<void(float, const std::string&)> progress_cb = {});
    ~RGBVideoPanel() override;

    RGBVideoPanel(const RGBVideoPanel&)            = delete;
    RGBVideoPanel& operator=(const RGBVideoPanel&) = delete;

    bool    isLoaded()    const noexcept { return loaded_; }
    int64_t durationUs()  const noexcept { return duration_us_; }

    void draw()                   override;
    void onTimeChanged(int64_t t) override;

    int64_t streamStartUs() const noexcept override { return 0; }
    int64_t streamEndUs()   const noexcept override { return duration_us_; }

private:
    bool openVideo(const std::string& filepath);
    void closeVideo();
    bool seekAndDecode(int64_t video_time_us);
    void uploadTexture();

    // FFmpeg state — kept opaque to avoid including FFmpeg headers here.
    struct FFmpegCtx;
    std::unique_ptr<FFmpegCtx> ff_;

    // OpenGL
    GLuint               tex_id_{0};
    int                  tex_w_{0};
    int                  tex_h_{0};
    std::vector<uint8_t> pixels_; ///< RGBA row-major pixel buffer

    int64_t duration_us_{0};
    int64_t last_time_us_{-1};
    bool    loaded_{false};
    std::function<void(float, const std::string&)> progress_cb_;
};

} // namespace mustard
