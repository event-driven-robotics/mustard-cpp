#include "mustard/app/App.h"
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/ui/DVSViewerPanel.h"
#include "mustard/ui/RGBVideoPanel.h"
#include "mustard/ui/ImageListPanel.h"
#include "mustard/data/events/TabularEventStream.h"

#include "ImGuiFileDialog.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <functional>
#include <sstream>
#include <string>

namespace mustard {

App::App()
    : time_ctrl_(std::make_shared<TimeController>()),
      import_(std::make_unique<EventImportSession>())
{
    // The dispatcher only visits currently owned panels; closing one cannot
    // leave a dangling callback in the playback controller.
    time_ctrl_->addObserver([this](int64_t t) {
        for (auto& viewer : viewers_) viewer->onTimeChanged(t);
    });
    loadRecentPaths();
}

App::~App() {
    viewers_.clear();
    import_.reset(); // cancel and join before main destroys the GL context
}

void App::tick(double dt) {
    time_ctrl_->tick(dt);
}

void App::draw() {
    drawMenuBar();
    drawFileDialog();
    if (import_) import_->poll();
    drawImportDialog();
    if (import_ && import_->phase() == EventImportSession::Phase::Ready) {
        commitImport();
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

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("jaer", nullptr,
                                event_theme_ == DVSViewerPanel::EventTheme::kJaer)) {
                setEventTheme(DVSViewerPanel::EventTheme::kJaer);
            }
            if (ImGui::MenuItem("edpr", nullptr,
                                event_theme_ == DVSViewerPanel::EventTheme::kEdpr)) {
                setEventTheme(DVSViewerPanel::EventTheme::kEdpr);
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

void App::setEventTheme(DVSViewerPanel::EventTheme theme) {
    event_theme_ = theme;
    for (const auto& viewer : viewers_) {
        if (auto* dvs_viewer = dynamic_cast<DVSViewerPanel*>(viewer.get())) {
            dvs_viewer->setEventTheme(theme);
        }
    }
}

void App::openFileOrFolder(const std::string& path) {
    import_->begin(path);
    selected_datasets_.clear();
    selection_file_.clear();
    status_message_ = "Preparing import";
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

void App::commitImport() {
    if (!import_) return;
    auto staged = import_->takeStaged();
    if (staged.empty()) return;

    viewers_.clear();

    ImportTimeRange time_range;
    for (auto& item : staged) {
        std::shared_ptr<DVSEventStream> stream = item.stream;
        if (!stream && item.loader) {
            auto tab_stream = std::make_shared<TabularEventStream>();
            tab_stream->adopt(std::move(item.loader), item.source.path);
            stream = std::move(tab_stream);
            item.stream = stream;
        }
        if (stream) {
            time_range.include(stream->startTime(), stream->endTime());
        }
    }

    if (!time_range.empty) {
        time_ctrl_->setRange(time_range.start, time_range.end);
        time_ctrl_->seekTo(time_range.start);
    }

    for (auto& item : staged) {
        std::string label = item.source.label;
        if (!item.dataset.empty()) {
            label += " :: " + item.dataset;
        }

        if (item.stream) {
            auto panel = std::make_unique<DVSViewerPanel>(item.stream, label);
            panel->setEventTheme(event_theme_);
            viewers_.push_back(std::move(panel));
        } else if (item.source.kind == ImportSourceKind::Video) {
            auto panel = std::make_unique<RGBVideoPanel>(item.source.path, label);
            if (panel->isLoaded()) {
                viewers_.push_back(std::move(panel));
            }
        } else if (item.source.kind == ImportSourceKind::Images) {
            auto panel = std::make_unique<ImageListPanel>(item.source.path, label);
            if (panel->isLoaded()) {
                viewers_.push_back(std::move(panel));
            }
        }
    }

    addRecentPath(import_->rootPath());
    layout_pending_ = true;
    status_message_ = "Imported " + std::to_string(viewers_.size()) + " stream(s)";
}

void App::drawImportDialog() {
    if (!import_ || !import_->active()) return;

    if (import_->busy()) {
        ImGui::OpenPopup("Import Progress");
        if (ImGui::BeginPopupModal("Import Progress", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s...", import_->progressLabel().c_str());
            ImGui::ProgressBar(import_->progress(), ImVec2(300.0f, 0.0f));
            if (ImGui::Button("Cancel")) {
                import_->cancel();
            }
            ImGui::EndPopup();
        }
        return;
    }

    if (import_->phase() == EventImportSession::Phase::Browsing) {
        ImGui::OpenPopup("Select Datasets (HDF5)");
        if (ImGui::BeginPopupModal("Select Datasets (HDF5)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("File: %s", import_->rootPath().c_str());
            ImGui::Separator();

            const auto& entries = import_->entries();
            if (ImGui::BeginChild("Hdf5Tree", ImVec2(500.0f, 300.0f), true)) {
                for (const auto& entry : entries) {
                    bool is_selected = selected_datasets_.count(entry.path) > 0;
                    if (!entry.selectable) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        ImGui::Text("%s (%s) - %s", entry.path.c_str(), entry.datatype.c_str(), entry.reason.c_str());
                        ImGui::PopStyleColor();
                    } else {
                        if (ImGui::Checkbox(entry.path.c_str(), &is_selected)) {
                            if (is_selected) selected_datasets_.insert(entry.path);
                            else selected_datasets_.erase(entry.path);
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", entry.datatype.c_str());
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Separator();
            if (ImGui::Button("Cancel")) import_->cancel();
            ImGui::SameLine();
            if (ImGui::Button("Skip File")) import_->skipFile();
            ImGui::SameLine();
            if (ImGui::Button("Next", ImVec2(80.0f, 0.0f))) {
                if (!selected_datasets_.empty()) {
                    import_->selectDatasets(std::vector<std::string>(selected_datasets_.begin(), selected_datasets_.end()));
                }
            }
            ImGui::EndPopup();
        }
        return;
    }

    if (import_->phase() == EventImportSession::Phase::Configuring) {
        auto* config = import_->configuration();
        if (!config) return;

        ImGui::OpenPopup("Configure Event Mapping");
        if (ImGui::BeginPopupModal("Configure Event Mapping", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto* file = import_->currentFile();
            ImGui::Text("File: %s", file ? file->label.c_str() : "");
            if (!config->options.dataset.empty()) {
                ImGui::Text("Dataset: %s", config->options.dataset.c_str());
            }
            ImGui::Separator();

            if (config->preview_ready) {
                const auto& preview = config->preview;
                const auto& cols = preview.columns;

                const char* fields[] = {"x column", "y column", "timestamp column", "polarity column"};
                for (int f = 0; f < 4; ++f) {
                    std::string current_name = "None";
                    int sel = config->options.columns[f];
                    if (sel >= 0 && static_cast<size_t>(sel) < cols.size()) {
                        current_name = cols[sel];
                    }
                    if (ImGui::BeginCombo(fields[f], current_name.c_str())) {
                        for (int c = 0; c < static_cast<int>(cols.size()); ++c) {
                            bool selected = (sel == c);
                            if (ImGui::Selectable(cols[c].c_str(), selected)) {
                                config->options.columns[f] = c;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::Separator();
                int unit_idx = static_cast<int>(config->options.timestamp_unit);
                const char* units[] = {"Microseconds (us)", "Seconds (s)", "Milliseconds (ms)", "Nanoseconds (ns)"};
                if (ImGui::Combo("Timestamp Unit", &unit_idx, units, 4)) {
                    config->options.timestamp_unit = static_cast<TimestampUnit>(unit_idx);
                }

                ImGui::InputInt("Sensor Width (0=infer)", &config->options.sensor_width);
                ImGui::InputInt("Sensor Height (0=infer)", &config->options.sensor_height);

                ImGui::Separator();
                ImGui::Text("Preview (first %d rows):", static_cast<int>(preview.rows.size()));
                if (ImGui::BeginChild("PreviewTable", ImVec2(500.0f, 150.0f), true)) {
                    if (ImGui::BeginTable("table_preview", static_cast<int>(cols.size()), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        for (const auto& col : cols) {
                            ImGui::TableSetupColumn(col.c_str());
                        }
                        ImGui::TableHeadersRow();

                        for (const auto& row : preview.rows) {
                            ImGui::TableNextRow();
                            for (int c = 0; c < static_cast<int>(row.size()); ++c) {
                                ImGui::TableSetColumnIndex(c);
                                ImGui::Text("%s", tableCellText(row[c]).c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();
                }
            }

            ImGui::Separator();
            if (import_->canGoBack() && ImGui::Button("Back")) import_->back();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) import_->cancel();
            ImGui::SameLine();
            if (ImGui::Button("Skip File")) import_->skipFile();
            ImGui::SameLine();
            if (ImGui::Button("Import", ImVec2(80.0f, 0.0f))) {
                import_->importFile();
            }
            ImGui::EndPopup();
        }
        return;
    }

    if (import_->phase() == EventImportSession::Phase::FileError) {
        ImGui::OpenPopup("Import Error");
        if (ImGui::BeginPopupModal("Import Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Error: %s", import_->error().describe().c_str());
            ImGui::Separator();
            if (ImGui::Button("Retry")) import_->retry();
            ImGui::SameLine();
            if (ImGui::Button("Skip File")) import_->skipFile();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) import_->cancel();
            ImGui::EndPopup();
        }
        return;
    }
}

} // namespace mustard
