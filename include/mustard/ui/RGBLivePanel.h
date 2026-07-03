#pragma once
#include "mustard/ui/ViewerPanel.h"

#include <glad/gl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mustard {

/// ImGui panel that displays frames from an OpenCV RGB camera.
class RGBLivePanel : public ViewerPanel {
public:
    explicit RGBLivePanel(int camera_index, std::string label);
    ~RGBLivePanel() override;

    RGBLivePanel(const RGBLivePanel&) = delete;
    RGBLivePanel& operator=(const RGBLivePanel&) = delete;

    bool isLoaded() const noexcept { return loaded_; }

    void draw() override;
    void onTimeChanged(int64_t t) override;

    int64_t streamStartUs() const noexcept override { return 0; }
    int64_t streamEndUs() const noexcept override { return last_time_us_; }

private:
    void captureLoop(int camera_index);
    bool copyLatestFrame();
    void uploadTexture();

    GLuint tex_id_{0};
    int tex_w_{0};
    int tex_h_{0};
    int pending_w_{0};
    int pending_h_{0};
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> pending_pixels_;

    std::thread capture_thread_;
    mutable std::mutex frame_mutex_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> loaded_{false};
    std::atomic<bool> capture_failed_{false};
    bool has_pending_frame_{false};
    int64_t last_time_us_{0};
};

} // namespace mustard
