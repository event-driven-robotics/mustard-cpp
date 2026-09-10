#include "mustard/ui/RGBLivePanel.h"

#include "mustard/ui/RGBFrameConversion.h"

#include "imgui.h"
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

namespace mustard {

RGBLivePanel::RGBLivePanel(int camera_index, std::string label)
    : ViewerPanel(std::move(label))
{
    capture_thread_ = std::thread([this, camera_index] { captureLoop(camera_index); });
}

RGBLivePanel::~RGBLivePanel() {
    stop_requested_ = true;
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (tex_id_) {
        glDeleteTextures(1, &tex_id_);
        tex_id_ = 0;
    }
}

void RGBLivePanel::onTimeChanged(int64_t t) {
    last_time_us_ = t;
}

void RGBLivePanel::draw() {
    constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin(label_.c_str(), &open_, kWindowFlags)) {
        ImGui::End();
        return;
    }

    drawAnnotationControls();

    if (copyLatestFrame()) {
        uploadTexture();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (tex_id_ != 0 && tex_w_ > 0 && tex_h_ > 0 &&
        avail.x > 0.f && avail.y > 0.f)
    {
        const float scale = std::min(avail.x / static_cast<float>(tex_w_),
                                     avail.y / static_cast<float>(tex_h_));
        const float dw = tex_w_ * scale;
        const float dh = tex_h_ * scale;
        const float off_x = (avail.x - dw) * 0.5f;
        const float off_y = (avail.y - dh) * 0.5f;
        if (off_x > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off_x);
        if (off_y > 0.f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off_y);

        const ImVec2 img_origin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(tex_id_), ImVec2(dw, dh));
        drawAnnotationInteraction(img_origin, scale, last_time_us_);
        drawAnnotationOverlay(img_origin, scale, last_time_us_);
    } else if (capture_failed_) {
        ImGui::TextDisabled("Failed to open RGB camera");
    } else {
        ImGui::TextDisabled("Waiting for first frame...");
    }

    ImGui::End();
}

bool RGBLivePanel::renderFrameForExport(int64_t,
                                         std::vector<uint8_t>& rgba,
                                         int& width, int& height) {
    copyLatestFrame();
    if (pixels_.empty() || tex_w_ <= 0 || tex_h_ <= 0) return false;
    rgba = pixels_;
    width = tex_w_;
    height = tex_h_;
    return true;
}

void RGBLivePanel::captureLoop(int camera_index) {
    cv::VideoCapture cap(camera_index);
    if (!cap.isOpened()) {
        capture_failed_ = true;
        return;
    }

    loaded_ = true;
    cv::Mat frame;
    while (!stop_requested_) {
        if (!cap.read(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba;
        if (!convertFrameToRgba(frame, w, h, rgba)) {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            pending_w_ = w;
            pending_h_ = h;
            pending_pixels_ = std::move(rgba);
            has_pending_frame_ = true;
        }
    }
}

bool RGBLivePanel::copyLatestFrame() {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (!has_pending_frame_) return false;
    tex_w_ = pending_w_;
    tex_h_ = pending_h_;
    pixels_ = pending_pixels_;
    has_pending_frame_ = false;
    return true;
}

void RGBLivePanel::uploadTexture() {
    if (pixels_.empty() || tex_w_ <= 0 || tex_h_ <= 0) return;
    if (tex_id_ == 0) {
        glGenTextures(1, &tex_id_);
        glBindTexture(GL_TEXTURE_2D, tex_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w_, tex_h_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindTexture(GL_TEXTURE_2D, tex_id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w_, tex_h_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace mustard
