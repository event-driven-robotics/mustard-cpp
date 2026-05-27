#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace mustard {

/// Shared playback state for all panels.
///
/// Panels register observer callbacks via addObserver().  TimeController::tick()
/// advances the playhead and notifies observers when the time changes.
class TimeController {
public:
    TimeController() = default;

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------
    int64_t currentTime()    const noexcept { return current_time_; }
    int64_t startTime()      const noexcept { return start_time_; }
    int64_t endTime()        const noexcept { return end_time_; }
    bool    isPlaying()      const noexcept { return is_playing_; }
    double  playbackSpeed()  const noexcept { return playback_speed_; }

    // ------------------------------------------------------------------
    // Mutators
    // ------------------------------------------------------------------
    void setRange(int64_t start, int64_t end);
    void seekTo(int64_t t);
    void setPlaying(bool playing);
    void setPlaybackSpeed(double speed);

    /// Advance the playhead by @p delta_seconds of wall-clock time.
    /// Notifies observers if the time changed.
    void tick(double delta_seconds);

    // ------------------------------------------------------------------
    // Observer pattern
    // ------------------------------------------------------------------
    /// Register a callback to be called whenever currentTime changes.
    void addObserver(std::function<void(int64_t)> cb);

private:
    void notifyObservers();

    int64_t current_time_{0};
    int64_t start_time_{0};
    int64_t end_time_{0};
    bool    is_playing_{false};
    double  playback_speed_{1.0};

    std::vector<std::function<void(int64_t)>> observers_;
};

} // namespace mustard
