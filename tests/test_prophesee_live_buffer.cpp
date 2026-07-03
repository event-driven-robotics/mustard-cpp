#include "mustard/data/events/PropheseeLiveBuffer.h"

#include <gtest/gtest.h>

using namespace mustard;

TEST(PropheseeLiveBufferTest, RebasesFirstEventToZero) {
    PropheseeLiveBuffer buffer(10'000);

    buffer.appendEvent(1'000'000, 10, 20, true);

    const auto chunk = buffer.chunkAt(0);
    ASSERT_TRUE(chunk.has_value());
    ASSERT_EQ(chunk->data.size(), 1u);
    EXPECT_EQ(chunk->data[0].t, 0);
    EXPECT_EQ(chunk->data[0].x, 10);
    EXPECT_EQ(chunk->data[0].y, 20);
    EXPECT_TRUE(chunk->data[0].polarity);
}

TEST(PropheseeLiveBufferTest, GroupsEventsIntoChunks) {
    PropheseeLiveBuffer buffer(10'000);

    buffer.appendEvent(1'000'000, 1, 2, true);
    buffer.appendEvent(1'005'000, 3, 4, false);
    buffer.appendEvent(1'012'000, 5, 6, true);

    const auto first = buffer.chunkAt(5'000);
    const auto second = buffer.chunkAt(12'000);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->t_start, 0);
    EXPECT_EQ(first->t_end, 10'000);
    EXPECT_EQ(first->data.size(), 2u);
    EXPECT_EQ(second->t_start, 10'000);
    EXPECT_EQ(second->t_end, 20'000);
    EXPECT_EQ(second->data.size(), 1u);
    EXPECT_EQ(buffer.latestTimeUs(), 12'000);
}

TEST(PropheseeLiveBufferTest, EmptyChunkIsReturnedForMissingRange) {
    PropheseeLiveBuffer buffer(10'000);

    const auto chunk = buffer.chunkAt(25'000);

    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(chunk->t_start, 20'000);
    EXPECT_EQ(chunk->t_end, 30'000);
    EXPECT_TRUE(chunk->data.empty());
}

TEST(PropheseeLiveBufferTest, ClearResetsBaseTimestamp) {
    PropheseeLiveBuffer buffer(10'000);

    buffer.appendEvent(1'000'000, 1, 2, true);
    buffer.clear();
    buffer.appendEvent(2'000'000, 3, 4, false);

    const auto chunk = buffer.chunkAt(0);
    ASSERT_TRUE(chunk.has_value());
    ASSERT_EQ(chunk->data.size(), 1u);
    EXPECT_EQ(chunk->data[0].t, 0);
    EXPECT_EQ(buffer.latestTimeUs(), 0);
}
