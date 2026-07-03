#pragma once
#include "mustard/data/DataChunk.h"
#include "mustard/data/DVSEvent.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

namespace mustard {

/// Thread-safe chunk buffer for live Prophesee events.
///
/// Incoming camera timestamps are rebased to zero at the first event and then
/// grouped into fixed-duration chunks so DVSViewerPanel can read them through
/// the same DVSEventStream interface as file-backed streams.
class PropheseeLiveBuffer {
public:
    explicit PropheseeLiveBuffer(int64_t chunk_duration_us);

    void clear();
    void appendEvent(int64_t camera_t, uint16_t x, uint16_t y, bool polarity);
    std::optional<DataChunk<DVSEvent>> chunkAt(int64_t t) const;

    int64_t latestTimeUs() const noexcept;
    int64_t chunkDurationUs() const noexcept { return chunk_duration_us_; }

private:
    int64_t chunkStartFor(int64_t t) const noexcept;

    const int64_t chunk_duration_us_;
    mutable std::mutex mutex_;
    std::map<int64_t, DataChunk<DVSEvent>> chunks_;
    int64_t base_camera_time_{0};
    int64_t latest_time_us_{0};
    bool has_base_{false};
};

} // namespace mustard
