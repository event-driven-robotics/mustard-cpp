#include "mustard/data/events/TabularEventStream.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mustard {

TabularEventStream::TabularEventStream(std::size_t cache_bytes)
    : loader_(std::make_unique<TabularEventLoader>()), cache_(cache_bytes) {}

bool TabularEventStream::open(const std::string& path) {
    return open(path, EventImportOptions{});
}

bool TabularEventStream::open(const std::string& path, const EventImportOptions& options,
                             ImportCancellation cancel, DataLoader<DVSEvent>::ProgressCallback progress) {
    close();
    if (!loader_) loader_ = std::make_unique<TabularEventLoader>();
    path_ = path;
    loader_->setProgressCallback(std::move(progress));
    return loader_->open(path, options, std::move(cancel));
}

void TabularEventStream::adopt(std::unique_ptr<TabularEventLoader> loader, const std::string& path) {
    close();
    loader_ = std::move(loader);
    if (loader_) loader_->setProgressCallback({});
    path_ = path;
}

void TabularEventStream::close() {
    if (loader_) loader_->close();
    cache_.clear();
    path_.clear();
}

bool TabularEventStream::isOpen() const { return loader_ && loader_->isOpen(); }

std::optional<DataChunk<DVSEvent>> TabularEventStream::getDataAtTime(int64_t t) {
    if (!isOpen()) return std::nullopt;
    // Imported timestamps are nonnegative. Requests before their range should
    // yield an empty chunk without negative division or integer overflow.
    if (t < 0) return DataChunk<DVSEvent>{t, 0, {}};
    constexpr int64_t kMaximum = std::numeric_limits<int64_t>::max();
    const int64_t t0 = (t / kChunkDurationUs) * kChunkDurationUs;
    const int64_t t1 = t0 > kMaximum - kChunkDurationUs ? kMaximum : t0 + kChunkDurationUs;
    if (auto cached = cache_.get(t0, t1)) return cached;
    auto chunk = loader_->readChunk(t0, t1);
    cache_.put(chunk);
    return chunk;
}

int64_t TabularEventStream::startTime() const { return loader_ ? loader_->startTime() : 0; }
int64_t TabularEventStream::endTime() const { return loader_ ? loader_->endTime() : 0; }
int TabularEventStream::sensorWidth() const noexcept { return loader_ ? loader_->sensorWidth() : 0; }
int TabularEventStream::sensorHeight() const noexcept { return loader_ ? loader_->sensorHeight() : 0; }
const ImportDiagnostic& TabularEventStream::lastError() const noexcept {
    static const ImportDiagnostic empty;
    return loader_ ? loader_->lastError() : empty;
}

} // namespace mustard
