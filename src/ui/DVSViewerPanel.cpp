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

    // Accumulation window: clamp start to stream's beginning
    const int64_t accum_t0  = std::max(stream_->startTime(), t - accum_window_us_);
    const int64_t chunk_dur = IITDatalogStream::kChunkDurationUs;
    const int64_t ct_start  = (accum_t0 / chunk_dur) * chunk_dur;

    switch (rep_mode_) {
        case RepresentationMode::kHistogram:
            renderHistogram(ct_start, accum_t0, t);
            break;
        case RepresentationMode::kTimeSurface:
            renderTimeSurface(ct_start, accum_t0, t);
            break;
        case RepresentationMode::kTernaryImage:
            renderTernaryImage(ct_start, accum_t0, t);
            break;
    }

    uploadTexture();
}

void DVSViewerPanel::draw() {
    if (!ImGui::Begin(label_.c_str(), &open_)) {
        ImGui::End();
        return;
    }

    // Representation mode selector
    static const char* const kRepItems[] = {
        "Histogram", "Time Surface", "Ternary Image"
    };
    int rep_idx = static_cast<int>(rep_mode_);
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::Combo("##rep_mode", &rep_idx, kRepItems, 3)) {
        rep_mode_ = static_cast<RepresentationMode>(rep_idx);
        if (last_time_ >= 0) {
            const int64_t cur = last_time_;
            last_time_ = -1;
            onTimeChanged(cur);
        }
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
    const std::size_t npix = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    pixels_.assign(npix * 4, 0u);
    aux_surface_.assign(npix, -1.f);
    aux_polarity_.assign(npix, int8_t{-1});

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

void DVSViewerPanel::setRepresentationMode(RepresentationMode mode) noexcept {
    rep_mode_  = mode;
    last_time_ = -1; // force repaint on next onTimeChanged
}

DVSViewerPanel::RepresentationMode DVSViewerPanel::representationMode() const noexcept {
    return rep_mode_;
}

// ---------------------------------------------------------------------------
// Render helpers — each implements one RepresentationMode
// ---------------------------------------------------------------------------

void DVSViewerPanel::renderHistogram(int64_t ct_start, int64_t accum_t0, int64_t t_now) {
    clearPixels();

    // aux_surface_  reused as ON-count  (index = y*tex_w_+x)
    // aux_polarity_ reused as OFF-count (stored as int8_t; values clamped to 127)
    std::fill(aux_surface_.begin(),  aux_surface_.end(),  0.f);
    std::fill(aux_polarity_.begin(), aux_polarity_.end(), int8_t{0});

    const int64_t chunk_dur = IITDatalogStream::kChunkDurationUs;
    for (int64_t ct = ct_start; ct <= t_now; ct += chunk_dur) {
        auto chunk = stream_->getDataAtTime(ct);
        if (!chunk) continue;
        for (const auto& ev : chunk->data) {
            if (ev.t < accum_t0 || ev.t > t_now) continue;
            if (static_cast<int>(ev.x) >= tex_w_ || static_cast<int>(ev.y) >= tex_h_) continue;
            const std::size_t idx = static_cast<std::size_t>(ev.y * tex_w_ + ev.x);
            if (ev.polarity) {
                aux_surface_[idx]  = std::min(aux_surface_[idx] + 1.f, 65535.f);
            } else {
                // use aux_polarity_ for OFF count; widen via unsigned before capping
                const int off_cnt = static_cast<int>(static_cast<uint8_t>(aux_polarity_[idx]));
                aux_polarity_[idx] = static_cast<int8_t>(std::min(off_cnt + 1, 255));
            }
        }
    }

    // Find max total count for normalisation
    float max_total = 1.f;
    for (int y = 0; y < tex_h_; ++y) {
        for (int x = 0; x < tex_w_; ++x) {
            const std::size_t idx   = static_cast<std::size_t>(y * tex_w_ + x);
            const float       total = aux_surface_[idx]
                + static_cast<float>(static_cast<uint8_t>(aux_polarity_[idx]));
            if (total > max_total) max_total = total;
        }
    }

    const float inv_max = 1.f / max_total;

    for (int y = 0; y < tex_h_; ++y) {
        for (int x = 0; x < tex_w_; ++x) {
            const std::size_t idx      = static_cast<std::size_t>(y * tex_w_ + x);
            const float       on_cnt   = aux_surface_[idx];
            const float       off_cnt  = static_cast<float>(static_cast<uint8_t>(aux_polarity_[idx]));
            const float       total    = on_cnt + off_cnt;
            if (total < 0.5f) continue; // background stays

            const auto        br       = static_cast<uint8_t>(std::min(total * inv_max, 1.f) * 255.f);
            const std::size_t pix      = idx * 4;
            if (on_cnt >= off_cnt) {
                // ON dominant (or tie) → green
                pixels_[pix + 0] = 0;   pixels_[pix + 1] = br;  pixels_[pix + 2] = 0;
            } else {
                // OFF dominant → red
                pixels_[pix + 0] = br;  pixels_[pix + 1] = 0;   pixels_[pix + 2] = 0;
            }
            pixels_[pix + 3] = 255;
        }
    }
}

void DVSViewerPanel::renderTimeSurface(int64_t ct_start, int64_t accum_t0, int64_t t_now) {
    clearPixels();
    std::fill(aux_surface_.begin(),  aux_surface_.end(),  -1.f);
    std::fill(aux_polarity_.begin(), aux_polarity_.end(), int8_t{-1});

    const int64_t span     = t_now - accum_t0;
    const float   inv_span = (span > 0) ? 1.f / static_cast<float>(span) : 1.f;
    const int64_t chunk_dur = IITDatalogStream::kChunkDurationUs;

    for (int64_t ct = ct_start; ct <= t_now; ct += chunk_dur) {
        auto chunk = stream_->getDataAtTime(ct);
        if (!chunk) continue;
        for (const auto& ev : chunk->data) {
            if (ev.t < accum_t0 || ev.t > t_now) continue;
            if (static_cast<int>(ev.x) >= tex_w_ || static_cast<int>(ev.y) >= tex_h_) continue;
            const std::size_t idx    = static_cast<std::size_t>(ev.y * tex_w_ + ev.x);
            const float       norm_t = static_cast<float>(ev.t - accum_t0) * inv_span;
            if (norm_t > aux_surface_[idx]) {
                aux_surface_[idx]  = norm_t;
                aux_polarity_[idx] = ev.polarity ? int8_t{1} : int8_t{0};
            }
        }
    }

    for (int y = 0; y < tex_h_; ++y) {
        for (int x = 0; x < tex_w_; ++x) {
            const std::size_t aux = static_cast<std::size_t>(y * tex_w_ + x);
            if (aux_surface_[aux] < 0.f) continue; // background stays
            const auto        br  = static_cast<uint8_t>(std::min(255.f, aux_surface_[aux] * 255.f));
            const std::size_t pix = aux * 4;
            if (aux_polarity_[aux] == int8_t{1}) {
                pixels_[pix + 0] = 0;   pixels_[pix + 1] = br;  pixels_[pix + 2] = 0;
            } else {
                pixels_[pix + 0] = br;  pixels_[pix + 1] = 0;   pixels_[pix + 2] = 0;
            }
            pixels_[pix + 3] = 255;
        }
    }
}

void DVSViewerPanel::renderTernaryImage(int64_t ct_start, int64_t accum_t0, int64_t t_now) {
    // Background: 50% grey
    for (std::size_t i = 0; i < pixels_.size(); i += 4) {
        pixels_[i + 0] = 128; pixels_[i + 1] = 128;
        pixels_[i + 2] = 128; pixels_[i + 3] = 255;
    }
    std::fill(aux_polarity_.begin(), aux_polarity_.end(), int8_t{-1});

    const int64_t chunk_dur = IITDatalogStream::kChunkDurationUs;
    for (int64_t ct = ct_start; ct <= t_now; ct += chunk_dur) {
        auto chunk = stream_->getDataAtTime(ct);
        if (!chunk) continue;
        for (const auto& ev : chunk->data) {
            if (ev.t < accum_t0 || ev.t > t_now) continue;
            if (static_cast<int>(ev.x) >= tex_w_ || static_cast<int>(ev.y) >= tex_h_) continue;
            const std::size_t idx = static_cast<std::size_t>(ev.y * tex_w_ + ev.x);
            aux_polarity_[idx] = ev.polarity ? int8_t{1} : int8_t{0};
        }
    }

    for (int y = 0; y < tex_h_; ++y) {
        for (int x = 0; x < tex_w_; ++x) {
            const std::size_t aux = static_cast<std::size_t>(y * tex_w_ + x);
            if (aux_polarity_[aux] < int8_t{0}) continue; // grey stays
            const std::size_t pix = aux * 4;
            if (aux_polarity_[aux] == int8_t{1}) {
                // ON → white
                pixels_[pix + 0] = 255; pixels_[pix + 1] = 255; pixels_[pix + 2] = 255;
            } else {
                // OFF → black
                pixels_[pix + 0] = 0;   pixels_[pix + 1] = 0;   pixels_[pix + 2] = 0;
            }
            pixels_[pix + 3] = 255;
        }
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
