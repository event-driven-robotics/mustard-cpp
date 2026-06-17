#include "mustard/data/events/PropheseeRawStream.h"

namespace mustard {

PropheseeRawStream::PropheseeRawStream(std::size_t cache_bytes)
    : cache_(cache_bytes)
{}

bool PropheseeRawStream::open(const std::string& path) {
    return open(path, {});
}

bool PropheseeRawStream::open(const std::string& path,
                              std::function<void(float, const std::string&)> progress_cb) {
    path_ = path;
    cache_.clear();
    loader_.setProgressCallback(std::move(progress_cb));
    return loader_.open(path);
}

void PropheseeRawStream::close() {
    loader_.close();
    cache_.clear();
}

bool PropheseeRawStream::isOpen() const {
    return loader_.isOpen();
}

std::optional<DataChunk<DVSEvent>> PropheseeRawStream::getDataAtTime(int64_t t) {
    if (!isOpen()) return std::nullopt;

    const int64_t t0 = (t / kChunkDurationUs) * kChunkDurationUs;
    const int64_t t1 = t0 + kChunkDurationUs;

    auto cached = cache_.get(t0, t1);
    if (cached) return cached;

    DataChunk<DVSEvent> chunk = loader_.readChunk(t0, t1);
    cache_.put(chunk);
    return chunk;
}

int64_t PropheseeRawStream::startTime() const { return loader_.startTime(); }
int64_t PropheseeRawStream::endTime()   const { return loader_.endTime();   }

int PropheseeRawStream::sensorWidth()  const noexcept { return loader_.sensorWidth(); }
int PropheseeRawStream::sensorHeight() const noexcept { return loader_.sensorHeight(); }
int64_t PropheseeRawStream::chunkDurationUs() const noexcept { return kChunkDurationUs; }

const std::string& PropheseeRawStream::path() const noexcept { return path_; }

} // namespace mustard
