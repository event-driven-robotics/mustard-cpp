#include "mustard/data/events/IITDatalogStream.h"

namespace mustard {

IITDatalogStream::IITDatalogStream(std::size_t cache_bytes)
    : cache_(cache_bytes)
{}

bool IITDatalogStream::open(const std::string& path) {
    return open(path, {});
}

bool IITDatalogStream::open(const std::string& path,
                            std::function<void(float, const std::string&)> progress_cb) {
    path_ = path;
    cache_.clear();
    loader_.setProgressCallback(std::move(progress_cb));
    return loader_.open(path);
}

void IITDatalogStream::close() {
    loader_.close();
    cache_.clear();
}

bool IITDatalogStream::isOpen() const {
    return loader_.isOpen();
}

std::optional<DataChunk<DVSEvent>> IITDatalogStream::getDataAtTime(int64_t t) {
    if (!isOpen()) return std::nullopt;

    // Align to chunk boundary so the same window is always cached under the same key
    const int64_t t0 = (t / kChunkDurationUs) * kChunkDurationUs;
    const int64_t t1 = t0 + kChunkDurationUs;

    auto cached = cache_.get(t0, t1);
    if (cached) return cached;

    DataChunk<DVSEvent> chunk = loader_.readChunk(t0, t1);
    cache_.put(chunk);
    return chunk;
}

int64_t IITDatalogStream::startTime() const { return loader_.startTime(); }
int64_t IITDatalogStream::endTime()   const { return loader_.endTime();   }

int IITDatalogStream::sensorWidth()  const noexcept { return loader_.sensorWidth();  }
int IITDatalogStream::sensorHeight() const noexcept { return loader_.sensorHeight(); }
int64_t IITDatalogStream::chunkDurationUs() const noexcept { return kChunkDurationUs; }

const std::string& IITDatalogStream::path() const noexcept { return path_; }

} // namespace mustard
