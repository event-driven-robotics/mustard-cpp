#include "mustard/core/TimeController.h"

#include <algorithm>

namespace mustard {

void TimeController::setRange(int64_t start, int64_t end) {
    start_time_   = start;
    end_time_     = end;
    current_time_ = std::clamp(current_time_, start_time_, end_time_);
}

void TimeController::seekTo(int64_t t) {
    int64_t clamped = std::clamp(t, start_time_, end_time_);
    if (clamped != current_time_) {
        current_time_ = clamped;
        notifyObservers();
    }
}

void TimeController::setPlaying(bool playing) {
    is_playing_ = playing;
}

void TimeController::setPlaybackSpeed(double speed) {
    playback_speed_ = speed;
}

void TimeController::tick(double delta_seconds) {
    if (!is_playing_) {
        return;
    }
    auto delta_us = static_cast<int64_t>(delta_seconds * 1e6 * playback_speed_);
    int64_t next  = current_time_ + delta_us;
    int64_t clamped = std::clamp(next, start_time_, end_time_);
    if (clamped != current_time_) {
        current_time_ = clamped;
        notifyObservers();
    }
    // Auto-stop at end
    if (current_time_ >= end_time_) {
        is_playing_ = false;
    }
}

void TimeController::addObserver(std::function<void(int64_t)> cb) {
    observers_.push_back(std::move(cb));
}

void TimeController::notifyObservers() {
    for (auto& cb : observers_) {
        cb(current_time_);
    }
}

} // namespace mustard
