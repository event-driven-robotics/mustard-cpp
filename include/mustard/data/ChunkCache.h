#pragma once
#include "mustard/data/DataChunk.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace mustard {

namespace detail {

struct PairHash {
    std::size_t operator()(const std::pair<int64_t, int64_t>& p) const noexcept {
        // Combine using Knuth multiplicative hashing
        std::size_t h1 = std::hash<int64_t>{}(p.first);
        std::size_t h2 = std::hash<int64_t>{}(p.second);
        return h1 ^ (h2 * 2654435761ULL);
    }
};

} // namespace detail

/// LRU cache for DataChunk<T> objects keyed by {t_start, t_end}.
///
/// Chunks are evicted in least-recently-used order once the total byte budget
/// is exceeded.  Byte size is estimated as sizeof(T) * data.size().
template <typename T>
class ChunkCache {
public:
    using Key   = std::pair<int64_t, int64_t>;
    using Chunk = DataChunk<T>;

    /// @param max_bytes  Maximum memory budget in bytes (default: 256 MiB).
    explicit ChunkCache(std::size_t max_bytes = 256ULL * 1024 * 1024)
        : max_bytes_(max_bytes) {}

    /// Store a chunk.  If the budget is exceeded, evicts LRU entries first.
    void put(Chunk chunk) {
        Key key{chunk.t_start, chunk.t_end};

        // Remove existing entry (will be re-inserted at front)
        auto it = map_.find(key);
        if (it != map_.end()) {
            current_bytes_ -= chunkBytes(it->second->second);
            order_.erase(it->second);
            map_.erase(it);
        }

        std::size_t bytes = chunkBytes(chunk);

        // Evict LRU until the budget allows the new entry
        while (!order_.empty() && current_bytes_ + bytes > max_bytes_) {
            evictLRU();
        }

        // Insert at front (most-recently-used)
        order_.push_front({key, std::move(chunk)});
        map_[key] = order_.begin();
        current_bytes_ += bytes;
    }

    /// Retrieve a chunk by exact key.  Moves it to the MRU position.
    std::optional<Chunk> get(int64_t t_start, int64_t t_end) {
        Key key{t_start, t_end};
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        // Promote to MRU
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    /// Find the first cached chunk whose interval contains @p t.
    std::optional<Chunk> findContaining(int64_t t) {
        for (auto& [key, chunk] : order_) {
            if (chunk.t_start <= t && t < chunk.t_end) {
                return chunk;
            }
        }
        return std::nullopt;
    }

    /// Current number of cached chunks.
    std::size_t size()         const noexcept { return map_.size(); }

    /// Current estimated memory usage in bytes.
    std::size_t currentBytes() const noexcept { return current_bytes_; }

    /// Memory budget in bytes.
    std::size_t maxBytes()     const noexcept { return max_bytes_; }

    /// Remove all entries.
    void clear() {
        order_.clear();
        map_.clear();
        current_bytes_ = 0;
    }

private:
    using Entry     = std::pair<Key, Chunk>;
    using OrderList = std::list<Entry>;
    using Map       = std::unordered_map<Key, typename OrderList::iterator, detail::PairHash>;

    static std::size_t chunkBytes(const Chunk& c) noexcept {
        return sizeof(T) * c.data.size();
    }

    void evictLRU() {
        auto& lru = order_.back();
        current_bytes_ -= chunkBytes(lru.second);
        map_.erase(lru.first);
        order_.pop_back();
    }

    std::size_t max_bytes_;
    std::size_t current_bytes_{0};
    OrderList   order_;
    Map         map_;
};

} // namespace mustard
