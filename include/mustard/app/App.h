#pragma once
#include "mustard/core/TimeController.h"
#include "mustard/ui/DVSViewerPanel.h"

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
    void drawOpenFolderModal();
    void drawPlaybackPanel();

    /// Recursively scan @p path for iitdatalog files, open each in its own
    /// IITDatalogStream and create a DVSViewerPanel for it.
    void openFolder(const std::string& path);

    static bool isIITDatalogCandidate(const std::string& filepath);

    std::shared_ptr<TimeController>              time_ctrl_;
    std::vector<std::unique_ptr<DVSViewerPanel>> viewers_;

    bool        wants_quit_{false};
    bool        show_open_dialog_{false};
    bool        layout_pending_{false};
    char        folder_path_buf_[4096]{};
    std::string status_message_;
};

} // namespace mustard
