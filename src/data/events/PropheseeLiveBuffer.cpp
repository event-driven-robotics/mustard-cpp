#include "mustard/data/events/PropheseeLiveBuffer.h"

#include <algorithm>

namespace mustard {

PropheseeLiveBuffer::PropheseeLiveBuffer(int64_t chunk_duration_us)
    : chunk_duration_us_(std::max<int64_t>(1, chunk_duration_us))
{}

void PropheseeLiveBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_.clear();
    base_camera_time_ = 0;
    latest_time_us_ = 0;
    has_base_ = false;
}

void PropheseeLiveBuffer::appendEvent(int64_t camera_t, uint16_t x, uint16_t y, bool polarity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_base_) {
        base_camera_time_ = camera_t;
        has_base_ = true;
    }

    const int64_t t = std::max<int64_t>(0, camera_t - base_camera_time_);
    const int64_t chunk_t0 = chunkStartFor(t);
    auto& chunk = chunks_[chunk_t0];
    if (chunk.t_end <= chunk.t_start) {
        chunk.t_start = chunk_t0;
        chunk.t_end = chunk_t0 + chunk_duration_us_;
    }

    DVSEvent ev;
    ev.t = t;
    ev.x = x;
    ev.y = y;
    ev.polarity = polarity;
    chunk.data.push_back(ev);
    latest_time_us_ = std::max(latest_time_us_, t);
}

std::optional<DataChunk<DVSEvent>> PropheseeLiveBuffer::chunkAt(int64_t t) const {
    const int64_t chunk_t0 = chunkStartFor(std::max<int64_t>(0, t));
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = chunks_.find(chunk_t0);
    if (it != chunks_.end()) {
        return it->second;
    }

    DataChunk<DVSEvent> empty;
    empty.t_start = chunk_t0;
    empty.t_end = chunk_t0 + chunk_duration_us_;
    return empty;
}

int64_t PropheseeLiveBuffer::latestTimeUs() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_time_us_;
}

int64_t PropheseeLiveBuffer::chunkStartFor(int64_t t) const noexcept {
    return (t / chunk_duration_us_) * chunk_duration_us_;
}

} // namespace mustard
