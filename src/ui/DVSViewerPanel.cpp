#include "mustard/ui/DVSViewerPanel.h"

#include "imgui.h"

#include <algorithm>

namespace mustard {

DVSViewerPanel::DVSViewerPanel(std::shared_ptr<IITDatalogStream> stream,
                               std::string                        label)
    : stream_(std::move(stream)), label_(std::move(label))
{}

DVSViewerPanel::~DVSViewerPanel() {
    if (texture_id_) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
}

void DVSViewerPanel::onTimeChanged(int64_t t) {
    if (!stream_ || !stream_->isOpen()) return;
    if (t == last_time_) return;
    last_time_ = t;

    const int w = stream_->sensorWidth();
    const int h = stream_->sensorHeight();
    if (w <= 0 || h <= 0) return;

    ensureTexture(w, h);
    clearPixels();

    // Accumulation window: clamp start to stream's beginning
    const int64_t accum_t0 = std::max(stream_->startTime(), t - kAccumWindowUs);
    const int64_t chunk_dur = IITDatalogStream::kChunkDurationUs;

    // First aligned chunk boundary at or before accum_t0
    const int64_t ct_start = (accum_t0 / chunk_dur) * chunk_dur;

    for (int64_t ct = ct_start; ct <= t; ct += chunk_dur) {
        auto chunk = stream_->getDataAtTime(ct);
        if (!chunk) continue;
        for (const auto& ev : chunk->data) {
            if (ev.t < accum_t0 || ev.t > t) continue;
            if (static_cast<int>(ev.x) < w && static_cast<int>(ev.y) < h) {
                paintEvent(static_cast<int>(ev.x), static_cast<int>(ev.y), ev.polarity);
            }
        }
    }

    uploadTexture();
}

void DVSViewerPanel::draw() {
    if (!ImGui::Begin(label_.c_str())) {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();

    if (texture_id_ != 0 && tex_w_ > 0 && tex_h_ > 0 &&
        avail.x > 0.f && avail.y > 0.f)
    {
        // Scale uniformly to fit available area, preserving aspect ratio
        const float scale = std::min(avail.x / static_cast<float>(tex_w_),
                                     avail.y / static_cast<float>(tex_h_));
        const float dw = tex_w_ * scale;
        const float dh = tex_h_ * scale;

        // Centre the image within the available space
        const float off_x = (avail.x - dw) * 0.5f;
        const float off_y = (avail.y - dh) * 0.5f;
        if (off_x > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off_x);
        if (off_y > 0.f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off_y);

        ImGui::Image(static_cast<ImTextureID>(texture_id_), ImVec2(dw, dh));
    } else if (stream_ && stream_->isOpen()) {
        ImGui::TextDisabled("Waiting for data…");
    } else {
        ImGui::TextDisabled("No stream");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void DVSViewerPanel::ensureTexture(int w, int h) {
    if (tex_w_ == w && tex_h_ == h && texture_id_ != 0) return;

    if (texture_id_) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }

    tex_w_ = w;
    tex_h_ = h;
    pixels_.assign(static_cast<std::size_t>(w * h * 4), 0u);

    glGenTextures(1, &texture_id_);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DVSViewerPanel::uploadTexture() {
    if (!texture_id_ || pixels_.empty()) return;
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w_, tex_h_,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DVSViewerPanel::clearPixels() {
    // Dark background (R=20, G=20, B=20, A=255)
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i + 0] = 20;
        pixels_[i + 1] = 20;
        pixels_[i + 2] = 20;
        pixels_[i + 3] = 255;
    }
}

void DVSViewerPanel::paintEvent(int x, int y, bool polarity) {
    const std::size_t idx = static_cast<std::size_t>((y * tex_w_ + x) * 4);
    if (polarity) {
        // ON event → green
        pixels_[idx + 0] = 0;
        pixels_[idx + 1] = 230;
        pixels_[idx + 2] = 80;
        pixels_[idx + 3] = 255;
    } else {
        // OFF event → red
        pixels_[idx + 0] = 255;
        pixels_[idx + 1] = 60;
        pixels_[idx + 2] = 60;
        pixels_[idx + 3] = 255;
    }
}

} // namespace mustard
