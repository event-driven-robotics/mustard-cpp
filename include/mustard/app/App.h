#pragma once
#include "mustard/core/TimeController.h"
#include "mustard/ui/DVSViewerPanel.h"

#include <deque>
#include <memory>
#include <string>
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
    ~App() = default;

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
    void drawFileDialog();
    void drawPlaybackPanel();

    /// Recursively scan @p path for iitdatalog files and open a panel for each.
    void openFolder(const std::string& path);
    /// Open a single data file directly (user selected it in the browser).
    void openSingleFile(const std::string& filepath);

    void addRecentPath(const std::string& path);
    void loadRecentPaths();
    void saveRecentPaths();
    static std::string recentPathsFile();

    static bool isIITDatalogCandidate(const std::string& filepath);

    static constexpr int kMaxRecentPaths = 10;

    std::shared_ptr<TimeController>              time_ctrl_;
    std::vector<std::unique_ptr<DVSViewerPanel>> viewers_;
    std::deque<std::string>                      recent_paths_;

    bool        wants_quit_{false};
    bool        show_open_file_dialog_{false};
    bool        show_open_folder_dialog_{false};
    bool        layout_pending_{false};
    int64_t     accum_window_us_{33'333};
    std::string status_message_;
};

} // namespace mustard
