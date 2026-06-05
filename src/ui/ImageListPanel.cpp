#include "mustard/ui/ImageListPanel.h"

// stb_image.h is vendored under third_party/stb/.
// STB_IMAGE_IMPLEMENTATION is intentionally NOT defined here: the
// implementation is already compiled into the ImGuiFileDialog library that is
// linked into mustard_app, so we only need the declarations.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace mustard {

namespace {

bool isImageExtension(const std::filesystem::path& p) {
    const auto ext = p.extension().string();
    return ext == ".png" || ext == ".PNG" ||
           ext == ".jpg" || ext == ".JPG" ||
           ext == ".jpeg" || ext == ".JPEG";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ImageListPanel::ImageListPanel(std::string dir_path, std::string label)
    : ViewerPanel(std::move(label))
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
        if (isImageExtension(entry.path()))
            images_.push_back(entry.path().string());
    }
    std::sort(images_.begin(), images_.end());
    loaded_ = !images_.empty();
}

ImageListPanel::~ImageListPanel() {
    if (tex_id_) {
        glDeleteTextures(1, &tex_id_);
        tex_id_ = 0;
    }
}

// ---------------------------------------------------------------------------
// ViewerPanel interface
// ---------------------------------------------------------------------------

void ImageListPanel::onTimeChanged(int64_t t) {
    if (!loaded_) return;
    const int64_t local_t = t - start_offset_us_;
    if (local_t < 0) return;
    const int idx = static_cast<int>(local_t / kFrameDurationUs);
    const int clamped = std::min(idx, static_cast<int>(images_.size()) - 1);
    if (clamped == last_index_) return;
    if (loadImageAt(clamped)) {
        last_index_ = clamped;
        uploadTexture();
    }
}

void ImageListPanel::draw() {
    if (!ImGui::Begin(label_.c_str(), &open_)) {
        ImGui::End();
        return;
    }

    if (!loaded_) {
        ImGui::TextDisabled("No images found");
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

        const int64_t frame_t = last_index_ >= 0
            ? static_cast<int64_t>(last_index_) * kFrameDurationUs
            : int64_t{0};
        const ImVec2 img_origin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(tex_id_), ImVec2(dw, dh));

        drawAnnotationInteraction(img_origin, scale, frame_t);
        drawAnnotationOverlay(img_origin, scale, frame_t);
    } else {
        ImGui::TextDisabled("Waiting for first frame\xe2\x80\xa6");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Private — on-demand image load
// ---------------------------------------------------------------------------

bool ImageListPanel::loadImageAt(int index) {
    if (index < 0 || index >= static_cast<int>(images_.size())) return false;
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(images_[index].c_str(), &w, &h, &channels, 4);
    if (!data) return false;

    if (w != tex_w_ || h != tex_h_) {
        // Image dimensions changed — recreate the GL texture next upload.
        if (tex_id_) {
            glDeleteTextures(1, &tex_id_);
            tex_id_ = 0;
        }
        tex_w_ = w;
        tex_h_ = h;
    }
    pixels_.assign(data, data + static_cast<std::size_t>(w * h * 4));
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// Private — GL texture upload
// ---------------------------------------------------------------------------

void ImageListPanel::uploadTexture() {
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
