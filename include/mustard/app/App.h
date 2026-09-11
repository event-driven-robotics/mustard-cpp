#pragma once
#include "mustard/core/TimeController.h"
#include "mustard/app/EventImportSession.h"
#include "mustard/ui/DVSViewerPanel.h"
#include "mustard/ui/ViewerPanel.h"

#include <deque>
#include <memory>
#include <string>
#include <set>
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
    void drawFileDialog();
    void drawPlaybackPanel();
    void drawImportDialog();
    void commitImport();
    void setEventTheme(DVSViewerPanel::EventTheme theme);

    void addRecentPath(const std::string path);
    void loadRecentPaths();
    void saveRecentPaths();
    static std::string recentPathsFile();

    static constexpr int kMaxRecentPaths = 10;

    std::shared_ptr<TimeController>             time_ctrl_;
    std::vector<std::unique_ptr<ViewerPanel>>   viewers_;
    std::deque<std::string>                     recent_paths_;
    std::unique_ptr<EventImportSession>          import_;
    std::set<std::string>                       selected_datasets_;
    std::string                                selection_file_;

    bool        wants_quit_{false};
    bool        show_open_file_dialog_{false};
    bool        show_open_folder_dialog_{false};
    bool        layout_pending_{false};
    DVSViewerPanel::EventTheme event_theme_{DVSViewerPanel::EventTheme::kJaer};
    std::string status_message_;
};

} // namespace mustard
