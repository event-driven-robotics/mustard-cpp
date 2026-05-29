#include "mustard/ui/DVSViewerPanel.h"
#include "mustard/annotation/BoundingBox.h"

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

    // img_origin / img_scale are set inside the texture-valid branch so that
    // both the interaction logic and annotation overlay rendering use the same
    // values without duplicating the scale computation.
    ImVec2 img_origin{0.f, 0.f};
    float  img_scale = 1.f;
    bool   img_valid = false;

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

        // Record screen-space origin BEFORE the Image call so we can map
        // mouse/annotation coordinates correctly afterwards.
        img_origin = ImGui::GetCursorScreenPos();
        img_scale  = scale;
        img_valid  = true;

        ImGui::Image(static_cast<ImTextureID>(texture_id_), ImVec2(dw, dh));

        // ------------------------------------------------------------------
        // Bounding-box draw mode interaction
        // ------------------------------------------------------------------
        if (mode_ == InteractionMode::kDrawBBox && ann_store_) {
            // Begin drag when the user clicks inside the image
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                drag_start_ = ImGui::GetMousePos();
                dragging_   = true;
            }

            // While dragging, paint a live preview rectangle
            if (dragging_ && ImGui::IsMouseDown(0)) {
                const ImVec2 cur = ImGui::GetMousePos();
                ImGui::GetWindowDrawList()->AddRect(
                    drag_start_, cur, IM_COL32(255, 200, 0, 180));
            }

            // On release, commit the finished bounding box to the store
            if (dragging_ && ImGui::IsMouseReleased(0)) {
                const ImVec2 cur = ImGui::GetMousePos();
                if (img_scale > 0.f) {
                    const float x0 = std::min(drag_start_.x, cur.x);
                    const float y0 = std::min(drag_start_.y, cur.y);
                    const float x1 = std::max(drag_start_.x, cur.x);
                    const float y1 = std::max(drag_start_.y, cur.y);
                    // Convert from screen space to sensor pixel space
                    const float bx = (x0 - img_origin.x) / img_scale;
                    const float by = (y0 - img_origin.y) / img_scale;
                    const float bw = (x1 - x0)           / img_scale;
                    const float bh = (y1 - y0)           / img_scale;
                    ann_store_->add(
                        std::make_unique<BoundingBox>(last_time_, bx, by, bw, bh));
                }
                dragging_ = false;
            }
        }
    } else if (stream_ && stream_->isOpen()) {
        ImGui::TextDisabled("Waiting for data…");
    } else {
        ImGui::TextDisabled("No stream");
    }

    // ------------------------------------------------------------------
    // Render existing annotations for the current timestamp as overlay
    // ------------------------------------------------------------------
    if (img_valid && ann_store_) {
        const auto* anns = ann_store_->queryAt(last_time_);
        if (anns) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (const auto& ann : *anns) {
                ann->renderOverlay(dl, img_origin, img_scale);
            }
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Annotation API
// ---------------------------------------------------------------------------

void DVSViewerPanel::setAnnotationStore(std::shared_ptr<AnnotationStore> store) {
    ann_store_ = std::move(store);
    dragging_  = false; // reset any in-progress drag when the store changes
}

void DVSViewerPanel::setInteractionMode(InteractionMode mode) noexcept {
    mode_     = mode;
    dragging_ = false; // cancel any in-progress drag on mode switch
}

DVSViewerPanel::InteractionMode DVSViewerPanel::interactionMode() const noexcept {
    return mode_;
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
