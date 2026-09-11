#pragma once
#include "mustard/data/ChunkCache.h"
#include "mustard/data/events/DVSEventStream.h"
#include "mustard/data/events/TabularEventLoader.h"

namespace mustard {

class TabularEventStream : public DVSEventStream {
public:
    static constexpr int64_t kChunkDurationUs = 10'000;
    explicit TabularEventStream(std::size_t cache_bytes = 64ULL * 1024 * 1024);
    bool open(const std::string& path) override;
    bool open(const std::string& path, const EventImportOptions& options,
              ImportCancellation cancel = {}, DataLoader<DVSEvent>::ProgressCallback progress = {});
    // Adopts an indexed loader after an import worker has completed.
    void adopt(std::unique_ptr<TabularEventLoader> loader, const std::string& path);
    void close() override;
    bool isOpen() const override;
    std::optional<DataChunk<DVSEvent>> getDataAtTime(int64_t t) override;
    int64_t startTime() const override;
    int64_t endTime() const override;
    int sensorWidth() const noexcept override;
    int sensorHeight() const noexcept override;
    int64_t chunkDurationUs() const noexcept override { return kChunkDurationUs; }
    const std::string& path() const noexcept override { return path_; }
    const ImportDiagnostic& lastError() const noexcept;
private:
    std::unique_ptr<TabularEventLoader> loader_;
    ChunkCache<DVSEvent> cache_;
    std::string path_;
};

} // namespace mustard
