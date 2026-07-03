#include "mustard/core/TimeController.h"

#include <gtest/gtest.h>

using namespace mustard;

TEST(TimeControllerLiveModeTest, TickExtendsEndTimeWhilePlaying) {
    TimeController tc;
    tc.setRange(0, 1);
    tc.setLiveMode(true);
    tc.setPlaying(true);

    tc.tick(0.25);

    EXPECT_EQ(tc.currentTime(), 250'000);
    EXPECT_EQ(tc.endTime(), 250'000);
}

TEST(TimeControllerLiveModeTest, PauseStopsAdvancement) {
    TimeController tc;
    tc.setRange(0, 1);
    tc.setLiveMode(true);
    tc.setPlaying(false);

    tc.tick(0.25);

    EXPECT_EQ(tc.currentTime(), 0);
    EXPECT_EQ(tc.endTime(), 1);
}

TEST(TimeControllerLiveModeTest, SeekClampsWithinElapsedRange) {
    TimeController tc;
    tc.setRange(0, 1);
    tc.setLiveMode(true);
    tc.setPlaying(true);
    tc.tick(0.25);

    tc.seekTo(1'000'000);

    EXPECT_EQ(tc.currentTime(), 250'000);
}

TEST(TimeControllerLiveModeTest, NonLiveStillStopsAtEnd) {
    TimeController tc;
    tc.setRange(0, 100'000);
    tc.setPlaying(true);

    tc.tick(0.25);

    EXPECT_EQ(tc.currentTime(), 100'000);
    EXPECT_FALSE(tc.isPlaying());
}
