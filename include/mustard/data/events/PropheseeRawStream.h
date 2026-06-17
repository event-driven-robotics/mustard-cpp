#pragma once
#include "mustard/data/ChunkCache.h"
#include "mustard/data/events/DVSEventStream.h"
#include "mustard/data/events/PropheseeRawLoader.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace mustard {

/// Concrete DVS stream for Prophesee RAW recordings.
class PropheseeRawStream : public DVSEventStream {
public:
    static constexpr int64_t kChunkDurationUs = 10'000;

    explicit PropheseeRawStream(std::size_t cache_bytes = 64ULL * 1024 * 1024);
    ~PropheseeRawStream() override = default;

    bool open(const std::string& path) override;
    bool open(const std::string& path, std::function<void(float, const std::string&)> progress_cb);
    void close() override;
    bool isOpen() const override;

    std::optional<DataChunk<DVSEvent>> getDataAtTime(int64_t t) override;

    int64_t startTime() const override;
    int64_t endTime() const override;

    int sensorWidth()  const noexcept override;
    int sensorHeight() const noexcept override;
    int64_t chunkDurationUs() const noexcept override;
    const std::string& path() const noexcept override;

private:
    PropheseeRawLoader    loader_;
    ChunkCache<DVSEvent>  cache_;
    std::string           path_;
};

} // namespace mustard
