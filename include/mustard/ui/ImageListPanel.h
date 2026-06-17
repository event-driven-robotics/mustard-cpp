#pragma once
#include "mustard/ui/ViewerPanel.h"

#include <glad/gl.h>
#include "imgui.h"
#include "stb_image.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mustard {

/// ImGui panel that displays a sorted sequence of PNG/JPG images from a
/// directory, synchronised to the global TimeController timeline.
///
/// Images are discovered at construction time (sorted alphabetically) and
/// loaded on demand with stb_image — one per frame tick, never pre-loaded.
///
/// Time mapping
/// ------------
/// Frame index i is shown at stream time  i * kFrameDurationUs.
/// start_offset_us_ (inherited from ViewerPanel) shifts the sequence within
/// the global timeline:
///   local_t = global_t - start_offset_us_
///   index   = local_t / kFrameDurationUs
class ImageListPanel : public ViewerPanel {
public:
    /// Display duration of each frame in microseconds (~30 fps).
    static constexpr int64_t kFrameDurationUs = 33'333;

    /// @param dir_path  Directory containing PNG/JPG images.
    /// @param label     ImGui window title.
    explicit ImageListPanel(std::string dir_path, std::string label,
                            std::function<void(float, const std::string&)> progress_cb = {});
    ~ImageListPanel() override;

    ImageListPanel(const ImageListPanel&)            = delete;
    ImageListPanel& operator=(const ImageListPanel&) = delete;

    bool isLoaded()   const noexcept { return loaded_; }
    int  imageCount() const noexcept { return static_cast<int>(images_.size()); }

    void draw()                   override;
    void onTimeChanged(int64_t t) override;

    int64_t streamStartUs() const noexcept override { return 0; }
    int64_t streamEndUs()   const noexcept override {
        return static_cast<int64_t>(images_.size()) * kFrameDurationUs;
    }

private:
    /// Load the image at @p index into pixels_. Returns true on success.
    bool loadImageAt(int index);
    void uploadTexture();

    std::vector<std::string> images_; ///< Sorted absolute paths to image files

    GLuint               tex_id_{0};
    int                  tex_w_{0};
    int                  tex_h_{0};
    std::vector<uint8_t> pixels_; ///< RGBA row-major pixel buffer

    int  last_index_{-1};
    bool loaded_{false};
    std::function<void(float, const std::string&)> progress_cb_;
};

} // namespace mustard
