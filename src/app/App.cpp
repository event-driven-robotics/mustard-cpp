#include "mustard/app/App.h"
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/ui/DVSViewerPanel.h"
#include "mustard/ui/RGBVideoPanel.h"
#include "mustard/ui/ImageListPanel.h"
#include "mustard/data/events/IITDatalogStream.h"
#include "mustard/data/events/PropheseeRawStream.h"

#include "ImGuiFileDialog.h"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace mustard {

App::App()
    : time_ctrl_(std::make_shared<TimeController>())
{
    loadRecentPaths();
}

App::~App() {
    if (loading_thread_.joinable()) {
        loading_thread_.join();
    }
}

void App::tick(double dt) {
    time_ctrl_->tick(dt);
}

void App::draw() {
    drawMenuBar();
    drawFileDialog();

    finishLoadingIfDone();
    if (loading_active_) {
        drawLoadingOverlay();
        return;
    }

    if (!viewers_.empty()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const int   n    = static_cast<int>(viewers_.size());
        const int   cols = std::max(1, static_cast<int>(
                               std::ceil(std::sqrt(static_cast<double>(n)))));
        const int   rows = (n + cols - 1) / cols;
        const float playback_h = 48.f;
        const float pw = vp->WorkSize.x / static_cast<float>(cols);
        const float ph = (vp->WorkSize.y - playback_h) / static_cast<float>(rows);

        // On the frame immediately after openFolder, force the grid layout.
        // Afterwards, let ImGui/docking remember the user's arrangement.
        const ImGuiCond cond = layout_pending_ ? ImGuiCond_Always : ImGuiCond_Once;

        for (int i = 0; i < n; ++i) {
            const int col = i % cols;
            const int row = i / cols;
            ImGui::SetNextWindowPos(
                {vp->WorkPos.x + col * pw, vp->WorkPos.y + row * ph}, cond);
            ImGui::SetNextWindowSize({pw, ph}, cond);
            viewers_[i]->draw();
        }

        layout_pending_ = false;

        // Remove any panels the user closed this frame.
        viewers_.erase(
            std::remove_if(viewers_.begin(), viewers_.end(),
                           [](const auto& v) { return !v->isOpen(); }),
            viewers_.end());

        // Draw playback panel last so it renders on top of viewer windows.
        drawPlaybackPanel();

    } else {
        // Welcome overlay shown when no dataset is loaded
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            {vp->WorkPos.x + vp->WorkSize.x * 0.5f,
             vp->WorkPos.y + vp->WorkSize.y * 0.5f},
            ImGuiCond_Always, {0.5f, 0.5f});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##welcome", nullptr, kFlags);
        ImGui::TextDisabled("File  \xe2\x86\x92  Open Folder\xe2\x80\xa6 to load a dataset");
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Private — menu bar
// ---------------------------------------------------------------------------

void App::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File\xe2\x80\xa6", "Ctrl+O")) {
                show_open_file_dialog_ = true;
            }
            if (ImGui::MenuItem("Open Folder\xe2\x80\xa6", "Ctrl+Shift+O")) {
                show_open_folder_dialog_ = true;
            }
            if (ImGui::BeginMenu("Open Recent", !recent_paths_.empty())) {
                int idx = 0;
                for (const auto& p : recent_paths_) {
                    if (p.empty()) { ++idx; continue; }
                    // Append ##r<n> so ImGui always has a non-empty unique ID
                    // even if two paths share the same display text.
                    const std::string item_id = p + "##r" + std::to_string(idx++);
                    if (ImGui::MenuItem(item_id.c_str())) {
                        openFileOrFolder(p);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Recent")) {
                    recent_paths_.clear();
                    saveRecentPaths();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                wants_quit_ = true;
            }
            ImGui::EndMenu();
        }

        if (loading_active_) {
            std::lock_guard<std::mutex> lock(loading_mutex_);
            if (!loading_stage_.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("%s", loading_stage_.c_str());
            }
        } else if (!status_message_.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", status_message_.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    // Ctrl+O / Ctrl+Shift+O shortcuts
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, /*repeat=*/false)) {
        show_open_file_dialog_ = true;
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, /*repeat=*/false)) {
        show_open_folder_dialog_ = true;
    }
}

void App::openFileOrFolder(const std::string &p)
{
    if (loading_active_) return;
    viewers_.clear();
    time_ctrl_ = std::make_shared<TimeController>();
    addRecentPath(p);
    loading_progress_ = 0.f;
    {
        std::lock_guard<std::mutex> lock(loading_mutex_);
        loading_stage_ = "Starting load";
    }
    load_progress_cb_ = [this](float progress, const std::string& stage) {
        loading_progress_ = progress;
        std::lock_guard<std::mutex> lock(loading_mutex_);
        loading_stage_ = stage;
    };
    loading_active_ = true;
    loading_done_ = false;
    loading_thread_ = std::thread([this, p]() {
        openFileOrFolderBlocking(p);
        loading_done_ = true;
        loading_active_ = false;
    });
}

void App::openFileOrFolderBlocking(const std::string &p)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(p, ec) && !ec)
        openFolder(p);
    else
        openSingleFile(p);
}

// ---------------------------------------------------------------------------
// Private — file/folder browser dialog
// ---------------------------------------------------------------------------

void App::drawFileDialog() {
    // Compute a sensible starting directory from the most recent path.
    auto initialPath = [&]() -> std::string {
        if (!recent_paths_.empty()) {
            namespace fs = std::filesystem;
            fs::path rp(recent_paths_.front());
            std::error_code ec;
            return (fs::is_directory(rp, ec) && !ec)
                       ? rp.string()
                       : rp.parent_path().string();
        }
        return ".";
    };

    // Open file dialog — shows all files, double-clicking a file selects it.
    if (show_open_file_dialog_) {
        ImGuiFileDialog::Instance()->OpenDialog(
            "OpenFileDlg", "Open File", ".*",
            initialPath(), "", 1, nullptr, ImGuiFileDialogFlags_Modal);
        show_open_file_dialog_ = false;
    }
    ImGui::SetNextWindowSize({700.f, 450.f}, ImGuiCond_Appearing);
    if (ImGuiFileDialog::Instance()->Display(
            "OpenFileDlg", ImGuiWindowFlags_NoCollapse)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            openFileOrFolder(ImGuiFileDialog::Instance()->GetFilePathName());
        }
        ImGuiFileDialog::Instance()->Close();
    }

    // Open folder dialog — filter nullptr shows only directories.
    if (show_open_folder_dialog_) {
        ImGuiFileDialog::Instance()->OpenDialog(
            "OpenFolderDlg", "Open Folder", nullptr,
            initialPath(), "", 1, nullptr, ImGuiFileDialogFlags_Modal);
        show_open_folder_dialog_ = false;
    }
    ImGui::SetNextWindowSize({700.f, 450.f}, ImGuiCond_Appearing);
    if (ImGuiFileDialog::Instance()->Display(
            "OpenFolderDlg", ImGuiWindowFlags_NoCollapse)) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string sel = ImGuiFileDialog::Instance()->GetFilePathName();
            if (sel.empty()) sel = ImGuiFileDialog::Instance()->GetCurrentPath();
            openFileOrFolder(sel.empty() ? "." : sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

// ---------------------------------------------------------------------------
// Private — playback bar
// ---------------------------------------------------------------------------

void App::drawPlaybackPanel() {
    constexpr float kPanelH = 48.f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kPanelH},
        ImGuiCond_Always);
    ImGui::SetNextWindowSize({vp->WorkSize.x, kPanelH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar     | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove         | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::Begin("##playback", nullptr, kFlags);

    const int64_t t_start = time_ctrl_->startTime();
    const int64_t t_end   = time_ctrl_->endTime();
    const int64_t t_range = t_end - t_start;

    // Play / Pause button
    const bool playing = time_ctrl_->isPlaying();
    if (ImGui::Button(playing ? " Pause " : "  Play  ")) {
        time_ctrl_->setPlaying(!playing);
    }

    ImGui::SameLine();

    // Seek slider (0.0 – 1.0 relative position)
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 160.f);
    float t_rel = (t_range > 0)
        ? static_cast<float>(time_ctrl_->currentTime() - t_start) /
          static_cast<float>(t_range)
        : 0.f;
    if (ImGui::SliderFloat("##seek", &t_rel, 0.f, 1.f, "")) {
        time_ctrl_->seekTo(
            t_start + static_cast<int64_t>(t_rel * static_cast<float>(t_range)));
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();

    // Time readout in seconds
    const double cur_s = static_cast<double>(time_ctrl_->currentTime() - t_start) / 1e6;
    const double dur_s = static_cast<double>(t_range) / 1e6;
    ImGui::Text("%.2f / %.2f s", cur_s, dur_s);

    ImGui::SameLine();

    // Quit button at the far right
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 26.f);
    if (ImGui::Button("\xc3\x97##quit", {26.f, 0.f})) {
        wants_quit_ = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quit");

    ImGui::End();
}

void App::finishLoadingIfDone() {
    if (!loading_done_) return;
    if (loading_thread_.joinable()) {
        loading_thread_.join();
    }
    loading_done_ = false;
    layout_pending_ = true;
}

void App::drawLoadingOverlay() {
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                             vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                            ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440.f, 120.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::Begin("##loading_overlay", nullptr, kFlags);

    ImGui::TextUnformatted("Loading dataset");
    {
        std::lock_guard<std::mutex> lock(loading_mutex_);
        if (!loading_stage_.empty()) {
            ImGui::TextDisabled("%s", loading_stage_.c_str());
        }
    }
    const float progress = loading_progress_.load();
    const bool determinate = progress > 0.0f;
    const ImVec2 bar_size(-FLT_MIN, 0.f);
    if (determinate) {
        ImGui::ProgressBar(progress, bar_size, nullptr);
    } else {
        ImGui::ProgressBar(0.5f, bar_size, "Working...");
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Private — folder scanning
// ---------------------------------------------------------------------------

bool App::isIITDatalogCandidate(const std::string& filepath) {
    namespace fs = std::filesystem;
    return fs::path(filepath).extension() == ".log";
}

bool App::isPropheseeRawCandidate(const std::string& filepath) {
    namespace fs = std::filesystem;
    const auto ext = fs::path(filepath).extension().string();
    return ext == ".raw" || ext == ".RAW";
}

bool App::isMp4Candidate(const std::string& filepath) {
    namespace fs = std::filesystem;
    const auto ext = fs::path(filepath).extension().string();
    // Accept common video container extensions FFmpeg can decode
    return ext == ".mp4" || ext == ".MP4" || ext == ".mkv" || ext == ".avi";
}

bool App::isImageListCandidate(const std::string& dir_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec) || ec) return false;
    constexpr int kMinImages = 50;
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
        const auto ext = entry.path().extension().string();
        if (ext == ".png" || ext == ".PNG" ||
            ext == ".jpg" || ext == ".JPG" ||
            ext == ".jpeg" || ext == ".JPEG") {
            ++count;
            if (count > kMinImages) return true;
        }
    }
    return false;
}

bool App::tryAddImageList(const std::string& dir_path, const std::string& label,
                          int64_t& t_min, int64_t& t_max) {
    auto panel = std::make_unique<ImageListPanel>(dir_path, label, load_progress_cb_);
    if (!panel->isLoaded()) return false;

    panel->setAnnotationStore(std::make_shared<AnnotationStore>());
    t_min = std::min(t_min, panel->streamStartUs());
    t_max = std::max(t_max, panel->streamEndUs());

    ImageListPanel* raw = panel.get();
    time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });
    viewers_.push_back(std::move(panel));
    return true;
}

// ---------------------------------------------------------------------------
// Private — format-specific panel builders
// ---------------------------------------------------------------------------

bool App::tryAddIITDatalog(const std::string& filepath, const std::string& label,
                           int64_t& t_min, int64_t& t_max) {
    auto stream = std::make_shared<IITDatalogStream>();
    if (!stream->open(filepath, load_progress_cb_)) return false;
    if (stream->sensorWidth()  <= 0 ||
        stream->sensorHeight() <= 0 ||
        stream->startTime()    >= stream->endTime()) return false;

    t_min = std::min(t_min, stream->startTime());
    t_max = std::max(t_max, stream->endTime());

    auto panel = std::make_unique<DVSViewerPanel>(stream, label);
    panel->setAnnotationStore(std::make_shared<AnnotationStore>());
    // Raw pointer is valid as long as viewers_ is alive, which outlives time_ctrl_.
    DVSViewerPanel* raw = panel.get();
    raw->setStartOffset(stream->startTime());
    time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });
    viewers_.push_back(std::move(panel));
    return true;
}

bool App::tryAddPropheseeRaw(const std::string& filepath, const std::string& label,
                             int64_t& t_min, int64_t& t_max) {
    auto stream = std::make_shared<PropheseeRawStream>();
    if (!stream->open(filepath, load_progress_cb_)) return false;
    if (stream->sensorWidth()  <= 0 ||
        stream->sensorHeight() <= 0 ||
        stream->startTime()    >= stream->endTime()) return false;

    t_min = std::min(t_min, stream->startTime());
    t_max = std::max(t_max, stream->endTime());

    auto panel = std::make_unique<DVSViewerPanel>(stream, label);
    panel->setAnnotationStore(std::make_shared<AnnotationStore>());
    DVSViewerPanel* raw = panel.get();
    raw->setStartOffset(stream->startTime());
    time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });
    viewers_.push_back(std::move(panel));
    return true;
}

bool App::tryAddVideo(const std::string& filepath, const std::string& label,
                      int64_t& t_min, int64_t& t_max) {
    auto panel = std::make_unique<RGBVideoPanel>(filepath, label, load_progress_cb_);
    if (!panel->isLoaded()) return false;

    panel->setAnnotationStore(std::make_shared<AnnotationStore>());
    t_min = std::min(t_min, panel->streamStartUs());
    t_max = std::max(t_max, panel->streamEndUs());

    RGBVideoPanel* raw = panel.get();
    time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });
    viewers_.push_back(std::move(panel));
    return true;
}

// ---------------------------------------------------------------------------
// Private — open a single data file directly
// ---------------------------------------------------------------------------

void App::openSingleFile(const std::string& filepath) {
    namespace fs = std::filesystem;

    status_message_.clear();

    const std::string label = fs::path(filepath).filename().string() + "##v1";
    int64_t t_start = std::numeric_limits<int64_t>::max();
    int64_t t_end   = std::numeric_limits<int64_t>::min();
    bool ok = false;

    if (isMp4Candidate(filepath))
        ok = tryAddVideo(filepath, label, t_start, t_end);
    else if (isPropheseeRawCandidate(filepath))
        ok = tryAddPropheseeRaw(filepath, label, t_start, t_end);
    else if (isIITDatalogCandidate(filepath))
        ok = tryAddIITDatalog(filepath, label, t_start, t_end);
    else if (isImageListCandidate(filepath))
        ok = tryAddImageList(filepath, label, t_start, t_end);

    if (!ok) {
        status_message_ = "Failed to open or unsupported format: " + filepath;
        return;
    }

    time_ctrl_->setRange(t_start, t_end);
    time_ctrl_->seekTo(t_start);
    status_message_ = "Opened: " + fs::path(filepath).filename().string();
    layout_pending_ = true;
}

// ---------------------------------------------------------------------------
// Private — folder scanning
// ---------------------------------------------------------------------------

void App::openFolder(const std::string& path) {
    namespace fs = std::filesystem;

    status_message_.clear();

    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
        status_message_ = "Not a valid directory: " + path;
        return;
    }

    int64_t t_min = std::numeric_limits<int64_t>::max();
    int64_t t_max = std::numeric_limits<int64_t>::min();

    openSingleFile(path);

    // Single directory traversal — bucket candidates by format.
    for (const auto& entry :
         fs::recursive_directory_iterator(path,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (ec) { ec.clear(); continue; }
        const std::string fp = entry.path().string();
        openSingleFile(fp); // Try to open as a file first (handles mixed content folders)
        
    }

    if (viewers_.empty()) {
        status_message_ = "No supported streams found in: " + path;
        return;
    }

    // Synchronisation: shift every panel so its data starts at global_start.
    // For DVS panels stream_t = global_t - offset + stream->startTime();
    // setting offset = global_start makes stream_t = stream->startTime() when
    // global_t = global_start, aligning all streams at the earliest timestamp.
    // For video panels (0-based) video_t = global_t - offset; offset = global_start
    // makes frame 0 appear at global_start.
    for (auto& v : viewers_) {
        v->setStartOffset(time_ctrl_->startTime());
    }

    status_message_ = std::to_string(viewers_.size()) + " stream(s) opened";
    layout_pending_ = true;
}

// ---------------------------------------------------------------------------
// Private — recent paths helpers
// ---------------------------------------------------------------------------

std::string App::recentPathsFile() {
    const char* home = std::getenv("HOME");
    if (!home) return "";
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(home) / ".config" / "mustard";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return ec ? "" : (dir / "recent.txt").string();
}

void App::loadRecentPaths() {
    const std::string f = recentPathsFile();
    if (f.empty()) return;
    std::ifstream ifs(f);
    std::string line;
    while (std::getline(ifs, line) &&
           static_cast<int>(recent_paths_.size()) < kMaxRecentPaths) {
        if (!line.empty()) recent_paths_.push_back(line);
    }
}

void App::saveRecentPaths() {
    const std::string f = recentPathsFile();
    if (f.empty()) return;
    std::ofstream ofs(f);
    for (const auto& p : recent_paths_) ofs << p << '\n';
}

void App::addRecentPath(const std::string path) {
    recent_paths_.erase(
        std::remove(recent_paths_.begin(), recent_paths_.end(), path),
        recent_paths_.end());
    recent_paths_.push_front(path);
    if (static_cast<int>(recent_paths_.size()) > kMaxRecentPaths)
        recent_paths_.resize(static_cast<std::size_t>(kMaxRecentPaths));
    saveRecentPaths();
}

} // namespace mustard
