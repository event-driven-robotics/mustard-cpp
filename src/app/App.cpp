#include "mustard/app/App.h"
#include "mustard/data/events/IITDatalogStream.h"

#include "ImGuiFileDialog.h"
#include "imgui.h"

#include <algorithm>
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

void App::tick(double dt) {
    time_ctrl_->tick(dt);
}

void App::draw() {
    drawMenuBar();
    drawFileDialog();

    if (!viewers_.empty()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const int   n    = static_cast<int>(viewers_.size());
        const int   cols = std::max(1, static_cast<int>(
                               std::ceil(std::sqrt(static_cast<double>(n)))));
        const int   rows = (n + cols - 1) / cols;
        const float playback_h = 80.f;
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
                        namespace fs = std::filesystem;
                        std::error_code ec;
                        if (fs::is_directory(p, ec) && !ec)
                            openFolder(p);
                        else
                            openSingleFile(p);
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

        if (!status_message_.empty()) {
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
            openSingleFile(ImGuiFileDialog::Instance()->GetFilePathName());
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
            openFolder(sel.empty() ? "." : sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

// ---------------------------------------------------------------------------
// Private — playback bar
// ---------------------------------------------------------------------------

void App::drawPlaybackPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 80.f},
        ImGuiCond_Always);
    ImGui::SetNextWindowSize({vp->WorkSize.x, 80.f}, ImGuiCond_Always);
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

    // Close / quit button at the far right of row 1
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 26.f);
    if (ImGui::Button("\xc3\x97##quit", {26.f, 0.f})) {   // UTF-8 × (U+00D7)
        wants_quit_ = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quit");

    // Row 2: accumulation window slider
    ImGui::SetNextItemWidth(120.f);
    ImGui::TextUnformatted("Accum window:");
    ImGui::SameLine();
    float accum_ms = static_cast<float>(accum_window_us_) / 1000.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.f);
    if (ImGui::SliderFloat("##accum", &accum_ms, 0.5f, 500.f, "%.1f ms",
                           ImGuiSliderFlags_Logarithmic)) {
        accum_window_us_ = static_cast<int64_t>(accum_ms * 1000.f);
        for (auto& v : viewers_) {
            v->setAccumWindow(accum_window_us_);
            v->onTimeChanged(time_ctrl_->currentTime());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Private — folder scanning
// ---------------------------------------------------------------------------

void App::openFolder(const std::string& path) {
    namespace fs = std::filesystem;

    viewers_.clear();
    time_ctrl_ = std::make_shared<TimeController>();
    status_message_.clear();

    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) {
        status_message_ = "Not a valid directory: " + path;
        return;
    }

    int64_t global_start = std::numeric_limits<int64_t>::max();
    int64_t global_end   = std::numeric_limits<int64_t>::min();
    int     found        = 0;

    for (const auto& entry :
         fs::recursive_directory_iterator(path,
             fs::directory_options::skip_permission_denied, ec))
    {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec)) continue;

        const std::string fpath = entry.path().string();
        if (!isIITDatalogCandidate(fpath)) continue;

        auto stream = std::make_shared<IITDatalogStream>();
        if (!stream->open(fpath)) continue;
        if (stream->sensorWidth()  <= 0 ||
            stream->sensorHeight() <= 0 ||
            stream->startTime()    >= stream->endTime()) continue;

        ++found;
        global_start = std::min(global_start, stream->startTime());
        global_end   = std::max(global_end,   stream->endTime());

        // Build a display label from the path relative to the root folder
        fs::path rel = fs::relative(entry.path(), fs::path(path), ec);
        const std::string display = ec ? entry.path().filename().string()
                                       : rel.string();
        // Append a unique ImGui ID suffix to disambiguate windows with the
        // same display text (e.g., two "data.log" files in different subdirs)
        const std::string label = display + "##v" + std::to_string(found);

        auto panel = std::make_unique<DVSViewerPanel>(stream, label);

        // Register observer — raw pointer is valid as long as viewers_ is alive,
        // which outlives the time_ctrl_ that holds these lambdas.
        DVSViewerPanel* raw = panel.get();
        time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });

        viewers_.push_back(std::move(panel));
    }

    if (found == 0) {
        status_message_ = "No iitdatalog streams found in: " + path;
        return;
    }

    time_ctrl_->setRange(global_start, global_end);
    time_ctrl_->seekTo(global_start);

    addRecentPath(path);
    status_message_ = std::to_string(found) + " stream(s) opened";
    layout_pending_ = true;
}

bool App::isIITDatalogCandidate(const std::string& filepath) {
    namespace fs = std::filesystem;
    const fs::path p(filepath);
    return p.extension() == ".log";
}

// ---------------------------------------------------------------------------
// Private — open a single data file directly
// ---------------------------------------------------------------------------

void App::openSingleFile(const std::string& filepath) {
    viewers_.clear();
    time_ctrl_ = std::make_shared<TimeController>();
    status_message_.clear();

    auto stream = std::make_shared<IITDatalogStream>();
    if (!stream->open(filepath)) {
        status_message_ = "Failed to open: " + filepath;
        return;
    }
    if (stream->sensorWidth()  <= 0 ||
        stream->sensorHeight() <= 0 ||
        stream->startTime()    >= stream->endTime()) {
        status_message_ = "Invalid stream: " + filepath;
        return;
    }

    namespace fs = std::filesystem;
    const std::string label = fs::path(filepath).filename().string() + "##v1";
    const int64_t t_start   = stream->startTime();
    const int64_t t_end     = stream->endTime();

    auto panel = std::make_unique<DVSViewerPanel>(stream, label);
    DVSViewerPanel* raw = panel.get();
    time_ctrl_->addObserver([raw](int64_t t) { raw->onTimeChanged(t); });
    viewers_.push_back(std::move(panel));

    time_ctrl_->setRange(t_start, t_end);
    time_ctrl_->seekTo(t_start);

    addRecentPath(filepath);
    status_message_ = "Opened: " + fs::path(filepath).filename().string();
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

void App::addRecentPath(const std::string& path) {
    recent_paths_.erase(
        std::remove(recent_paths_.begin(), recent_paths_.end(), path),
        recent_paths_.end());
    recent_paths_.push_front(path);
    if (static_cast<int>(recent_paths_.size()) > kMaxRecentPaths)
        recent_paths_.resize(static_cast<std::size_t>(kMaxRecentPaths));
    saveRecentPaths();
}

} // namespace mustard
