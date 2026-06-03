#include "mustard/app/App.h"
#include "mustard/data/events/IITDatalogStream.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

namespace mustard {

App::App()
    : time_ctrl_(std::make_shared<TimeController>())
{}

void App::tick(double dt) {
    time_ctrl_->tick(dt);
}

void App::draw() {
    drawMenuBar();
    drawOpenFolderModal();

    if (!viewers_.empty()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const int   n    = static_cast<int>(viewers_.size());
        const int   cols = std::max(1, static_cast<int>(
                               std::ceil(std::sqrt(static_cast<double>(n)))));
        const int   rows = (n + cols - 1) / cols;
        const float playback_h = 55.f;
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
            if (ImGui::MenuItem("Open Folder\xe2\x80\xa6", "Ctrl+O")) {
                show_open_dialog_ = true;
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

    // Ctrl+O shortcut
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, /*repeat=*/false)) {
        show_open_dialog_ = true;
    }
}

// ---------------------------------------------------------------------------
// Private — open-folder dialog
// ---------------------------------------------------------------------------

void App::drawOpenFolderModal() {
    if (show_open_dialog_) {
        ImGui::OpenPopup("Open Folder");
        show_open_dialog_ = false;
    }

    ImGui::SetNextWindowSize({620, 105}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Open Folder", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Folder path:");
        ImGui::SetNextItemWidth(-1.f);
        const bool enter = ImGui::InputText(
            "##path", folder_path_buf_, sizeof(folder_path_buf_),
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Spacing();
        const bool open_clicked = ImGui::Button("Open", {120, 0});
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", {120, 0});

        if ((open_clicked || enter) && folder_path_buf_[0] != '\0') {
            openFolder(folder_path_buf_);
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Private — playback bar
// ---------------------------------------------------------------------------

void App::drawPlaybackPanel() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 55.f},
        ImGuiCond_Always);
    ImGui::SetNextWindowSize({vp->WorkSize.x, 55.f}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    // Force the playback bar to the front of the draw stack every frame so
    // viewer windows can never render on top of it.
    ImGui::SetNextWindowFocus();

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
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 130.f);
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

    status_message_ = std::to_string(found) + " stream(s) opened";
    layout_pending_ = true;
}

bool App::isIITDatalogCandidate(const std::string& filepath) {
    namespace fs = std::filesystem;
    const fs::path p(filepath);
    return p.extension() == ".log";
}

} // namespace mustard
