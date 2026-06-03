#pragma once
#include <cstdint>
#include <string>

namespace mustard {

/// Abstract base class for all viewer panels.
///
/// Concrete panels inherit from this class and implement draw() and
/// onTimeChanged().  The TimeController notifies every registered panel
/// via onTimeChanged(); each panel manages its own rendering state.
///
/// label_ and open_ are stored here so the ImGui window identity and close
/// state are consistent across all panel types.
class ViewerPanel {
public:
    explicit ViewerPanel(std::string label) : label_(std::move(label)) {}
    virtual ~ViewerPanel() = default;

    // Non-copyable; moveable (explicitly defaulted because the virtual
    // destructor suppresses the implicit move constructor).
    ViewerPanel(const ViewerPanel&)            = delete;
    ViewerPanel& operator=(const ViewerPanel&) = delete;
    ViewerPanel(ViewerPanel&&)                 = default;
    ViewerPanel& operator=(ViewerPanel&&)      = default;

    /// Emit all ImGui commands for this panel (one standalone window).
    virtual void draw() = 0;

    /// Called by the TimeController observer on every time change.
    virtual void onTimeChanged(int64_t t) = 0;

    const std::string& label()  const noexcept { return label_; }
    /// Returns false after the user clicks the window's close (✕) button.
    bool               isOpen() const noexcept { return open_; }

protected:
    std::string label_;
    bool        open_{true};
};

} // namespace mustard
