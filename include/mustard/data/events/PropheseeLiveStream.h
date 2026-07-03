#pragma once
#include "mustard/data/events/DVSEventStream.h"
#include "mustard/data/events/PropheseeLiveBuffer.h"

#include <memory>
#include <string>

namespace mustard {

/// Live DVSEventStream backed by the first available Prophesee camera.
class PropheseeLiveStream : public DVSEventStream {
public:
    static constexpr int64_t kChunkDurationUs = 10'000;

    PropheseeLiveStream();
    ~PropheseeLiveStream() override;

    bool open(const std::string& path) override;
    bool openFirstAvailable();
    void close() override;
    bool isOpen() const override;

    std::optional<DataChunk<DVSEvent>> getDataAtTime(int64_t t) override;

    int64_t startTime() const override;
    int64_t endTime() const override;

    int sensorWidth() const noexcept override;
    int sensorHeight() const noexcept override;
    int64_t chunkDurationUs() const noexcept override;
    const std::string& path() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mustard
