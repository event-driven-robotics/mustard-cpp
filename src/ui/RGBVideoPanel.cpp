#include "mustard/ui/RGBVideoPanel.h"

// FFmpeg headers are C-compatible; include them here to keep them out of the
// public header (hidden behind the PIMPL FFmpegCtx struct).
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace mustard {

// ---------------------------------------------------------------------------
// PIMPL struct — owns all FFmpeg resources
// ---------------------------------------------------------------------------

struct RGBVideoPanel::FFmpegCtx {
    AVFormatContext* fmt_ctx{nullptr};
    AVCodecContext*  codec_ctx{nullptr};
    SwsContext*      sws_ctx{nullptr};
    AVFrame*         frame{nullptr};
    AVFrame*         frame_rgba{nullptr};
    AVPacket*        packet{nullptr};
    int              video_stream_idx{-1};
    AVRational       time_base{0, 1};

    ~FFmpegCtx() {
        av_packet_free(&packet);
        av_frame_free(&frame_rgba);
        av_frame_free(&frame);
        sws_freeContext(sws_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RGBVideoPanel::RGBVideoPanel(std::string filepath,
                             std::string label)
    : ViewerPanel(std::move(label))
{
    ff_ = std::make_unique<FFmpegCtx>();
    loaded_ = openVideo(filepath);
}

RGBVideoPanel::~RGBVideoPanel() {
    if (tex_id_) {
        glDeleteTextures(1, &tex_id_);
        tex_id_ = 0;
    }
    // ff_ destructor handles FFmpeg teardown via unique_ptr
}

// ---------------------------------------------------------------------------
// ViewerPanel interface
// ---------------------------------------------------------------------------

void RGBVideoPanel::onTimeChanged(int64_t t) {
    if (!loaded_) return;
    if (t == last_time_us_) return;
    last_time_us_ = t;

    const int64_t video_time_us = t - start_offset_us_;
    if (video_time_us < 0 || video_time_us > duration_us_) return;

    if (seekAndDecode(video_time_us)) {
        uploadTexture();
    }
}

void RGBVideoPanel::draw() {
    if (!ImGui::Begin(label_.c_str(), &open_)) {
        ImGui::End();
        return;
    }

    if (!loaded_) {
        ImGui::TextDisabled("Failed to load video");
        ImGui::End();
        return;
    }

    drawAnnotationControls();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (tex_id_ != 0 && tex_w_ > 0 && tex_h_ > 0 &&
        avail.x > 0.f && avail.y > 0.f)
    {
        const float scale = std::min(avail.x / static_cast<float>(tex_w_),
                                     avail.y / static_cast<float>(tex_h_));
        const float dw    = tex_w_ * scale;
        const float dh    = tex_h_ * scale;
        const float off_x = (avail.x - dw) * 0.5f;
        const float off_y = (avail.y - dh) * 0.5f;
        if (off_x > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off_x);
        if (off_y > 0.f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + off_y);

        const ImVec2 img_origin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(tex_id_), ImVec2(dw, dh));

        drawAnnotationInteraction(img_origin, scale, last_time_us_);
        drawAnnotationOverlay(img_origin, scale, last_time_us_);
    } else {
        ImGui::TextDisabled("Waiting for first frame\xe2\x80\xa6");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Private — FFmpeg open / close
// ---------------------------------------------------------------------------

bool RGBVideoPanel::openVideo(const std::string& filepath) {
    // Open container
    if (avformat_open_input(&ff_->fmt_ctx, filepath.c_str(),
                            nullptr, nullptr) < 0)
        return false;

    if (avformat_find_stream_info(ff_->fmt_ctx, nullptr) < 0)
        return false;

    // Find the best video stream
    ff_->video_stream_idx = av_find_best_stream(
        ff_->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (ff_->video_stream_idx < 0) return false;

    AVStream* vs   = ff_->fmt_ctx->streams[ff_->video_stream_idx];
    ff_->time_base = vs->time_base;

    // Duration in microseconds
    if (vs->duration != AV_NOPTS_VALUE) {
        duration_us_ = av_rescale_q(vs->duration, ff_->time_base,
                                    AVRational{1, 1'000'000});
    } else if (ff_->fmt_ctx->duration != AV_NOPTS_VALUE) {
        // fmt_ctx->duration is in AV_TIME_BASE (microseconds)
        duration_us_ = ff_->fmt_ctx->duration;
    }
    if (duration_us_ <= 0) return false;

    // Find and open decoder
    const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) return false;

    ff_->codec_ctx = avcodec_alloc_context3(codec);
    if (!ff_->codec_ctx) return false;

    if (avcodec_parameters_to_context(ff_->codec_ctx, vs->codecpar) < 0)
        return false;
    if (avcodec_open2(ff_->codec_ctx, codec, nullptr) < 0)
        return false;

    tex_w_ = ff_->codec_ctx->width;
    tex_h_ = ff_->codec_ctx->height;
    if (tex_w_ <= 0 || tex_h_ <= 0) return false;

    // Allocate decode + RGBA frames
    ff_->frame      = av_frame_alloc();
    ff_->frame_rgba = av_frame_alloc();
    ff_->packet     = av_packet_alloc();
    if (!ff_->frame || !ff_->frame_rgba || !ff_->packet) return false;

    // Allocate RGBA pixel buffer and wire it into frame_rgba
    const int buf_size = av_image_get_buffer_size(
        AV_PIX_FMT_RGBA, tex_w_, tex_h_, 1);
    if (buf_size <= 0) return false;
    pixels_.resize(static_cast<std::size_t>(buf_size));
    av_image_fill_arrays(ff_->frame_rgba->data, ff_->frame_rgba->linesize,
                         pixels_.data(), AV_PIX_FMT_RGBA, tex_w_, tex_h_, 1);

    // SWS context: decode pixel format → RGBA
    ff_->sws_ctx = sws_getContext(
        tex_w_, tex_h_, ff_->codec_ctx->pix_fmt,
        tex_w_, tex_h_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!ff_->sws_ctx) return false;

    return true;
}

void RGBVideoPanel::closeVideo() {
    // Managed by FFmpegCtx destructor; reset state here
    tex_w_ = tex_h_ = 0;
    duration_us_ = 0;
    last_time_us_ = -1;
    loaded_ = false;
}

// ---------------------------------------------------------------------------
// Private — seek + decode one frame
// ---------------------------------------------------------------------------

bool RGBVideoPanel::seekAndDecode(int64_t video_time_us) {
    // Convert microseconds to stream PTS units
    const int64_t target_pts = av_rescale_q(
        video_time_us, AVRational{1, 1'000'000}, ff_->time_base);

    if (av_seek_frame(ff_->fmt_ctx, ff_->video_stream_idx,
                      target_pts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;

    avcodec_flush_buffers(ff_->codec_ctx);

    bool converted = false;

    while (av_read_frame(ff_->fmt_ctx, ff_->packet) >= 0) {
        if (ff_->packet->stream_index != ff_->video_stream_idx) {
            av_packet_unref(ff_->packet);
            continue;
        }

        if (avcodec_send_packet(ff_->codec_ctx, ff_->packet) < 0) {
            av_packet_unref(ff_->packet);
            continue;
        }
        av_packet_unref(ff_->packet);

        bool past_target = false;
        while (true) {
            const int ret = avcodec_receive_frame(ff_->codec_ctx, ff_->frame);
            if (ret == AVERROR(EAGAIN) || ret < 0) break;

            // Convert every frame we receive so that pixels_ always holds the
            // last frame before (or at) target_pts.
            sws_scale(ff_->sws_ctx,
                      const_cast<const uint8_t* const*>(ff_->frame->data),
                      ff_->frame->linesize,
                      0, tex_h_,
                      ff_->frame_rgba->data, ff_->frame_rgba->linesize);
            converted = true;

            const int64_t pts = ff_->frame->best_effort_timestamp;
            if (pts >= target_pts) {
                past_target = true;
                break;
            }
        }
        if (past_target) break;
    }

    return converted;
}

// ---------------------------------------------------------------------------
// Private — GL texture upload
// ---------------------------------------------------------------------------

void RGBVideoPanel::uploadTexture() {
    if (pixels_.empty()) return;
    if (tex_id_ == 0) {
        glGenTextures(1, &tex_id_);
        glBindTexture(GL_TEXTURE_2D, tex_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     tex_w_, tex_h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindTexture(GL_TEXTURE_2D, tex_id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w_, tex_h_,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace mustard
