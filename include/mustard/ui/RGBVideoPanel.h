#pragma once
#include "mustard/ui/ViewerPanel.h"

#include <glad/gl.h>
#include "imgui.h"

#include <cstdint>
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
/// The panel stores a start_offset_us that maps global time to video time:
///   video_time_us = global_t - start_offset_us
/// For a standalone MP4 load the offset is 0 (global range starts at 0).
/// When mixed with event streams the App passes the event stream's
/// global_start so the video aligns with the events.
class RGBVideoPanel : public ViewerPanel {
public:
    /// @param filepath        Path to the video file.
    /// @param label           ImGui window title.
    /// @param start_offset_us Global time (µs) at which the video starts.
    explicit RGBVideoPanel(std::string filepath,
                           std::string label,
                           int64_t     start_offset_us = 0);
    ~RGBVideoPanel() override;

    RGBVideoPanel(const RGBVideoPanel&)            = delete;
    RGBVideoPanel& operator=(const RGBVideoPanel&) = delete;

    bool    isLoaded()    const noexcept { return loaded_; }
    int64_t durationUs()  const noexcept { return duration_us_; }

    void draw()                   override;
    void onTimeChanged(int64_t t) override;

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

    int64_t start_offset_us_{0};
    int64_t duration_us_{0};
    int64_t last_time_us_{-1};
    bool    loaded_{false};
};

} // namespace mustard
