#pragma once
#include "mustard/core/TimeController.h"
#include "mustard/ui/ViewerPanel.h"

#include <deque>
#include <atomic>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace mustard {

/// Top-level application state and per-frame GUI logic.
///
/// Owns the TimeController, all DVSViewerPanels, and the folder-open dialog
/// state.  Call tick() once per frame to advance playback, then draw() to
/// emit all ImGui commands for the frame.
class App {
public:
    App();
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    /// Advance playback by @p dt wall-clock seconds.
    void tick(double dt);

    /// Emit all ImGui windows for this frame.
    void draw();

    /// True when the user chose File → Quit.
    bool wantsQuit() const noexcept { return wants_quit_; }

private:
    void drawMenuBar();
    void openFileOrFolder(const std::string &p);
    void openFileOrFolderBlocking(const std::string& p);
    void drawFileDialog();
    void drawPlaybackPanel();
    void drawLoadingOverlay();
    void finishLoadingIfDone();

    /// Recursively scan @p path for iitdatalog files and open a panel for each.
    void openFolder(const std::string& path);
    /// Open a single data file directly (user selected it in the browser).
    void openSingleFile(const std::string& filepath);

    void addRecentPath(const std::string path);
    void loadRecentPaths();
    void saveRecentPaths();
    static std::string recentPathsFile();

    /// Try to open @p filepath as an iitdatalog event stream and append a panel.
    /// Updates @p t_min / @p t_max with the stream's time range.
    bool tryAddIITDatalog(const std::string& filepath, const std::string& label,
                          int64_t& t_min, int64_t& t_max);

    /// Try to open @p filepath as a Prophesee RAW event stream and append a panel.
    /// Updates @p t_min / @p t_max with the stream's time range.
    bool tryAddPropheseeRaw(const std::string& filepath, const std::string& label,
                            int64_t& t_min, int64_t& t_max);

    /// Try to open @p filepath as a video and append a panel.
    /// Updates @p t_min / @p t_max with the resulting time range.
    bool tryAddVideo(const std::string& filepath, const std::string& label,
                     int64_t& t_min, int64_t& t_max);

    /// Try to open @p dir_path as an image-list panel and append it.
    /// Updates @p t_min / @p t_max with the resulting time range.
    bool tryAddImageList(const std::string& dir_path, const std::string& label,
                         int64_t& t_min, int64_t& t_max);

    static bool isIITDatalogCandidate(const std::string& filepath);
    static bool isPropheseeRawCandidate(const std::string& filepath);
    static bool isMp4Candidate(const std::string& filepath);
    /// Returns true when @p dir_path is a directory containing more than 50
    /// PNG/JPG image files (non-recursive scan).
    static bool isImageListCandidate(const std::string& dir_path);

    static constexpr int kMaxRecentPaths = 10;

    std::shared_ptr<TimeController>             time_ctrl_;
    std::vector<std::unique_ptr<ViewerPanel>>   viewers_;
    std::deque<std::string>                     recent_paths_;

    bool        wants_quit_{false};
    bool        show_open_file_dialog_{false};
    bool        show_open_folder_dialog_{false};
    bool        layout_pending_{false};
    std::atomic<bool>   loading_active_{false};
    std::atomic<bool>   loading_done_{false};
    std::atomic<float>  loading_progress_{0.f};
    std::thread         loading_thread_;
    std::mutex          loading_mutex_;
    std::string         loading_stage_;
    std::function<void(float, const std::string&)> load_progress_cb_;
    std::string status_message_;
};

} // namespace mustard
