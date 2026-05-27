#include "mustard/data/ChunkCache.h"
#include "mustard/data/DVSEvent.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

using namespace mustard;

class ChunkCacheTest : public ::testing::Test {
protected:
    // Each DVSEvent is sizeof(DVSEvent) bytes
    static constexpr std::size_t kEventSize = sizeof(DVSEvent);

    DataChunk<DVSEvent> makeChunk(int64_t t0, int64_t t1, int n_events) {
        DataChunk<DVSEvent> c;
        c.t_start = t0;
        c.t_end   = t1;
        c.data.resize(static_cast<std::size_t>(n_events));
        return c;
    }
};

TEST_F(ChunkCacheTest, GetReturnsNulloptWhenEmpty) {
    ChunkCache<DVSEvent> cache;
    EXPECT_FALSE(cache.get(0, 1000).has_value());
}

TEST_F(ChunkCacheTest, PutAndGetReturnsChunk) {
    ChunkCache<DVSEvent> cache;
    auto chunk = makeChunk(0, 1000, 10);
    cache.put(chunk);

    auto result = cache.get(0, 1000);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->t_start, 0);
    EXPECT_EQ(result->t_end, 1000);
    EXPECT_EQ(result->data.size(), 10u);
}

TEST_F(ChunkCacheTest, SizeAndCurrentBytes) {
    ChunkCache<DVSEvent> cache;
    cache.put(makeChunk(0, 1000, 5));
    cache.put(makeChunk(1000, 2000, 3));

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.currentBytes(), 8u * kEventSize);
}

TEST_F(ChunkCacheTest, EvictsLRUWhenBudgetExceeded) {
    // Budget for exactly 10 events
    std::size_t budget = 10 * kEventSize;
    ChunkCache<DVSEvent> cache(budget);

    // Fill with chunks of 5 events each
    cache.put(makeChunk(0,    1000, 5)); // A
    cache.put(makeChunk(1000, 2000, 5)); // B — A+B = 10 events, at budget
    // Add C (5 events) → budget exceeded → A (LRU) should be evicted
    cache.put(makeChunk(2000, 3000, 5)); // C

    EXPECT_FALSE(cache.get(0, 1000).has_value());    // A evicted
    EXPECT_TRUE(cache.get(1000, 2000).has_value());  // B still present
    EXPECT_TRUE(cache.get(2000, 3000).has_value());  // C present
}

TEST_F(ChunkCacheTest, LRUPromotedOnAccess) {
    std::size_t budget = 10 * kEventSize;
    ChunkCache<DVSEvent> cache(budget);

    cache.put(makeChunk(0,    1000, 5)); // A
    cache.put(makeChunk(1000, 2000, 5)); // B

    // Access A — makes A MRU, B becomes LRU
    cache.get(0, 1000);

    // Insert C → B should be evicted (LRU), not A
    cache.put(makeChunk(2000, 3000, 5)); // C

    EXPECT_TRUE(cache.get(0, 1000).has_value());    // A still present (was promoted)
    EXPECT_FALSE(cache.get(1000, 2000).has_value()); // B evicted
    EXPECT_TRUE(cache.get(2000, 3000).has_value());  // C present
}

TEST_F(ChunkCacheTest, PutOverwriteExistingKey) {
    ChunkCache<DVSEvent> cache;
    cache.put(makeChunk(0, 1000, 5));
    cache.put(makeChunk(0, 1000, 9)); // same key, more events

    EXPECT_EQ(cache.size(), 1u);
    auto result = cache.get(0, 1000);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data.size(), 9u);
}

TEST_F(ChunkCacheTest, FindContaining) {
    ChunkCache<DVSEvent> cache;
    cache.put(makeChunk(0,    1000, 3));
    cache.put(makeChunk(1000, 2000, 3));

    EXPECT_TRUE(cache.findContaining(500).has_value());
    EXPECT_TRUE(cache.findContaining(0).has_value());
    EXPECT_FALSE(cache.findContaining(2000).has_value()); // half-open [1000,2000)
    EXPECT_FALSE(cache.findContaining(3000).has_value());
}

TEST_F(ChunkCacheTest, ClearEmptiesCache) {
    ChunkCache<DVSEvent> cache;
    cache.put(makeChunk(0, 1000, 5));
    cache.clear();

    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.currentBytes(), 0u);
    EXPECT_FALSE(cache.get(0, 1000).has_value());
}
