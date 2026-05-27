#pragma once
#include "mustard/data/DataChunk.h"

#include <cstdint>
#include <optional>
#include <string>

namespace mustard {

/// Abstract base for typed data streams.
///
/// A DataStream owns or wraps a DataLoader and a ChunkCache.  It exposes a
/// high-level time-based access API and is responsible for prefetching chunks
/// around the current playhead.
///
/// Concrete subclasses (e.g. IITDatalogStream) couple a specific loader type
/// with a cache eviction policy.
template <typename T>
class DataStream {
public:
    virtual ~DataStream() = default;

    /// Open the underlying source.  Returns true on success.
    virtual bool open(const std::string& path) = 0;

    /// Close the underlying source.
    virtual void close() = 0;

    /// Returns true when the stream is open and ready.
    virtual bool isOpen() const = 0;

    /// Retrieve the data chunk whose window contains @p t (microseconds).
    /// Returns std::nullopt when no data is available (source not open, t out
    /// of range, or I/O error).
    virtual std::optional<DataChunk<T>> getDataAtTime(int64_t t) = 0;

    /// Earliest timestamp in the dataset (microseconds).
    virtual int64_t startTime() const = 0;

    /// Latest timestamp in the dataset (microseconds).
    virtual int64_t endTime() const = 0;
};

} // namespace mustard
