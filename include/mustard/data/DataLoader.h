#pragma once
#include "mustard/data/DataChunk.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace mustard {

/// Abstract base class for data-format loaders.
///
/// Subclasses implement the three lifecycle methods (open/close/readChunkImpl)
/// and this base class applies the registered callback pipeline before returning
/// each chunk to callers.
template <typename T>
class DataLoader {
public:
    using ProgressCallback = std::function<void(float, const std::string&)>;

    virtual ~DataLoader() = default;

    /// Open the data source at @p path.  Returns true on success.
    virtual bool open(const std::string& path) = 0;

    /// Close the data source and release all resources.
    virtual void close() = 0;

    /// Returns true if the source is currently open.
    virtual bool isOpen() const = 0;

    /// Earliest timestamp in the dataset (microseconds).
    virtual int64_t startTime() const = 0;

    /// Latest timestamp in the dataset (microseconds).
    virtual int64_t endTime() const = 0;

    /// Read all events in the half-open interval [t0, t1).
    /// Applies every registered callback to the chunk before returning.
    DataChunk<T> readChunk(int64_t t0, int64_t t1) {
        DataChunk<T> chunk = readChunkImpl(t0, t1);
        for (auto& cb : callbacks_) {
            cb(chunk);
        }
        return chunk;
    }

    /// Append a callback that is invoked (in order) after every readChunk.
    /// Callbacks may modify or filter the chunk in place.
    void addCallback(std::function<void(DataChunk<T>&)> cb) {
        callbacks_.push_back(std::move(cb));
    }

    /// Register a callback used by long-running open/read operations to
    /// publish load progress in the range [0, 1] with a short stage label.
    void setProgressCallback(ProgressCallback cb) {
        progress_cb_ = std::move(cb);
    }

protected:
    /// Subclass-provided chunk-reading implementation (no callbacks applied).
    virtual DataChunk<T> readChunkImpl(int64_t t0, int64_t t1) = 0;

    void reportProgress(float progress, const std::string& stage) const {
        if (progress_cb_) {
            progress_cb_(std::clamp(progress, 0.0f, 1.0f), stage);
        }
    }

private:
    std::vector<std::function<void(DataChunk<T>&)>> callbacks_;
    ProgressCallback progress_cb_;
};

} // namespace mustard
