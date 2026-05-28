#pragma once
#include "mustard/data/ChunkCache.h"
#include "mustard/data/DataStream.h"
#include "mustard/data/DVSEvent.h"
#include "mustard/data/events/IITDatalogLoader.h"

#include <cstddef>
#include <optional>
#include <string>

namespace mustard {

/// Concrete DataStream<DVSEvent> backed by IITDatalogLoader with an LRU cache.
///
/// Chunks are aligned to kChunkDurationUs boundaries so repeated queries
/// for the same time window hit the cache.
class IITDatalogStream : public DataStream<DVSEvent> {
public:
    /// Duration of each cached chunk in microseconds (10 ms).
    static constexpr int64_t kChunkDurationUs = 10'000;

    explicit IITDatalogStream(std::size_t cache_bytes = 64ULL * 1024 * 1024);
    ~IITDatalogStream() override = default;

    bool open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;

    /// Returns the chunk whose aligned window contains @p t.
    /// Returns std::nullopt only when the stream is not open.
    std::optional<DataChunk<DVSEvent>> getDataAtTime(int64_t t) override;

    int64_t startTime() const override;
    int64_t endTime()   const override;

    int sensorWidth()  const noexcept;
    int sensorHeight() const noexcept;

    const std::string& path() const noexcept;

private:
    IITDatalogLoader     loader_;
    ChunkCache<DVSEvent> cache_;
    std::string          path_;
};

} // namespace mustard
