#include "mustard/data/events/IITDatalogLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#ifndef IITDATALOG_SAMPLE_PATH
#error "IITDATALOG_SAMPLE_PATH must be defined (see tests/CMakeLists.txt)"
#endif

using namespace mustard;

class IITDatalogIntegrationTest : public ::testing::Test {
protected:
    static IITDatalogLoader loader_;
    static bool             opened_;

    static void SetUpTestSuite() {
        opened_ = loader_.open(IITDATALOG_SAMPLE_PATH);
    }

    static void TearDownTestSuite() {
        loader_.close();
    }
};

IITDatalogLoader IITDatalogIntegrationTest::loader_;
bool             IITDatalogIntegrationTest::opened_ = false;

// ---------------------------------------------------------------------------

TEST_F(IITDatalogIntegrationTest, FileOpensSuccessfully) {
    ASSERT_TRUE(opened_) << "Failed to open " << IITDATALOG_SAMPLE_PATH;
}

TEST_F(IITDatalogIntegrationTest, PacketCountIsReasonable) {
    ASSERT_TRUE(opened_);
    // The sample has 55170 packets
    EXPECT_GT(loader_.packetCount(), 1000u);
}

TEST_F(IITDatalogIntegrationTest, TimestampRangeCoversFullRecording) {
    ASSERT_TRUE(opened_);
    EXPECT_EQ(loader_.startTime(), 0);
    // Recording is ~54 seconds; end time should be > 50 seconds in µs
    EXPECT_GT(loader_.endTime(), 50'000'000LL);
}

TEST_F(IITDatalogIntegrationTest, FirstChunkContainsEvents) {
    ASSERT_TRUE(opened_);
    // Read the first 1 ms
    auto chunk = loader_.readChunk(loader_.startTime(),
                                   loader_.startTime() + 1'000);
    EXPECT_FALSE(chunk.data.empty());
}

TEST_F(IITDatalogIntegrationTest, EventFieldsAreInRange) {
    ASSERT_TRUE(opened_);
    // Sample 10 ms in the middle of the recording
    int64_t mid = (loader_.startTime() + loader_.endTime()) / 2;
    auto chunk = loader_.readChunk(mid, mid + 10'000);
    ASSERT_FALSE(chunk.data.empty());

    for (const auto& ev : chunk.data) {
        EXPECT_GE(ev.x, 0);
        EXPECT_LT(ev.x, 2048);  // 11-bit field maximum
        EXPECT_GE(ev.y, 0);
        EXPECT_LT(ev.y, 1024);  // 10-bit field maximum
        EXPECT_GE(ev.t, loader_.startTime());
        EXPECT_LE(ev.t, loader_.endTime());
    }
}

TEST_F(IITDatalogIntegrationTest, SensorResolutionDetected) {
    ASSERT_TRUE(opened_);
    // iitdatalog sensor is ~640×480
    EXPECT_GT(loader_.sensorWidth(),  0);
    EXPECT_GT(loader_.sensorHeight(), 0);
    EXPECT_LE(loader_.sensorWidth(),  2048);
    EXPECT_LE(loader_.sensorHeight(), 1024);
}

TEST_F(IITDatalogIntegrationTest, KnownFirstEventDecode) {
    ASSERT_TRUE(opened_);
    // Verified in analysis: packet 0, event 0 → polarity=1, x=473, y=120
    auto chunk = loader_.readChunk(loader_.startTime(),
                                   loader_.startTime() + 1);
    ASSERT_FALSE(chunk.data.empty());
    const auto& ev = chunk.data[0];
    EXPECT_EQ(ev.polarity, true);
    EXPECT_EQ(ev.x, 473);
    EXPECT_EQ(ev.y, 120);
}
