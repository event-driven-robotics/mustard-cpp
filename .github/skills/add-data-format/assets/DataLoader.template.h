#pragma once
// DataLoader<T> canonical template — copy and adapt for each new format.
// Replace:
//   FormatLoader  → your loader class name (e.g. BinaryEventLoader)
//   T             → your event/data type (e.g. DVSEvent, uint8_t)
//   FormatChunk   → DataChunk<T> (no replacement needed, just use it)
//
// See .github/skills/add-data-format/SKILL.md for the full workflow.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "mustard/data/DataChunk.h"  // template<typename T> struct DataChunk

namespace mustard {

/// Abstract base — do not instantiate directly.
/// Subclasses implement open(), close(), and readChunkImpl().
template <typename T>
class DataLoader {
public:
    virtual ~DataLoader() = default;

    // --- Lifecycle ---

    /// Open the data source (file, stream, device).
    /// Returns true on success. Must be called before readChunk().
    virtual bool open(const std::string& path) = 0;

    /// Close and release all resources.
    virtual void close() = 0;

    /// Time range of the entire data source (microseconds).
    /// Valid only after a successful open().
    virtual int64_t startTime() const = 0;
    virtual int64_t endTime()   const = 0;

    // --- Callback Pipeline ---

    /// Add a processing callback invoked (in order) on every chunk returned by readChunk().
    /// Callbacks may filter, transform, or annotate events in-place.
    void addCallback(std::function<void(DataChunk<T>&)> cb) {
        callbacks_.push_back(std::move(cb));
    }

    // --- Chunk Reading ---

    /// Read and return all data in [t0, t1] (microseconds, inclusive).
    /// Applies the callback pipeline before returning.
    /// Returns std::nullopt if the loader is not open or t0 > t1.
    std::optional<DataChunk<T>> readChunk(int64_t t0, int64_t t1) {
        if (t0 > t1) return std::nullopt;
        auto chunk = readChunkImpl(t0, t1);
        if (!chunk) return std::nullopt;
        for (auto& cb : callbacks_) {
            cb(*chunk);
        }
        return chunk;
    }

protected:
    /// Subclass implements the actual decode/seek/read here.
    /// Must NOT apply callbacks — the base class does that.
    virtual std::optional<DataChunk<T>> readChunkImpl(int64_t t0, int64_t t1) = 0;

private:
    std::vector<std::function<void(DataChunk<T>&)>> callbacks_;
};

}  // namespace mustard
