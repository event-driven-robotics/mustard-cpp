#include "mustard/data/events/IITDatalogLoader.h"
#include "mustard/data/DVSEvent.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mustard;

namespace {

/// Write a synthetic iitdatalog file to @p path.
/// Each entry in @p packets is {timestamp_us, raw_bytes[]}.
///
/// The binary payload is escaped according to the iitdatalog rules.
void writeSyntheticFile(const std::string& path,
                        const std::vector<std::pair<int64_t, std::vector<uint8_t>>>& packets)
{
    std::ofstream f(path, std::ios::binary);

    int seq = 0;
    for (const auto& [ts_us, raw] : packets) {
        int64_t sec  = ts_us / 1'000'000LL;
        int64_t usec = ts_us % 1'000'000LL;

        // Header: seq  sec.usec  AE  1000  "
        f << seq++ << " " << sec << "." << std::to_string(usec).substr(0, 6);
        // Pad usec to 6 digits
        std::string usec_str = std::to_string(usec);
        while (usec_str.size() < 6) usec_str = "0" + usec_str;
        // Rewrite the timestamp field properly
        f.seekp(-static_cast<std::streamoff>(
            std::to_string(sec).size() + 1 +
            std::to_string(usec).size()), std::ios::cur);
        f << sec << "." << usec_str << "  AE  1000  \"";

        // Escape and write binary payload
        for (uint8_t b : raw) {
            switch (b) {
                case 0x5C: f.put('\\'); f.put('\\'); break;
                case 0x0A: f.put('\\'); f.put('n');  break;
                case 0x0D: f.put('\\'); f.put('r');  break;
                case 0x00: f.put('\\'); f.put('0');  break;
                case 0x22: f.put('\\'); f.put('"');  break;
                default:   f.put(static_cast<char>(b)); break;
            }
        }
        f << "\"\n";
    }
}

/// Simpler write helper that avoids seekp issues.
void writeSyntheticFile2(const std::string& path,
                         const std::vector<std::pair<int64_t, std::vector<uint8_t>>>& packets)
{
    std::ofstream f(path, std::ios::binary);

    int seq = 0;
    for (const auto& [ts_us, raw] : packets) {
        int64_t sec  = ts_us / 1'000'000LL;
        int64_t usec = ts_us % 1'000'000LL;

        // Pad usec to 6 digits
        std::string usec_str = std::to_string(usec);
        while (usec_str.size() < 6) usec_str = "0" + usec_str;

        f << seq++ << "  " << sec << "." << usec_str << "  AE  1000  \"";

        // Escape binary payload
        for (uint8_t b : raw) {
            switch (b) {
                case 0x5C: f.put('\\'); f.put('\\'); break;
                case 0x0A: f.put('\\'); f.put('n');  break;
                case 0x0D: f.put('\\'); f.put('r');  break;
                case 0x00: f.put('\\'); f.put('0');  break;
                case 0x22: f.put('\\'); f.put('"');  break;
                default:   f.put(static_cast<char>(b)); break;
            }
        }
        f << "\"\n";
    }
}

/// Encode a DVS event as 4 raw bytes (little-endian).
std::vector<uint8_t> encodeEvent(bool pol, uint16_t x, uint16_t y) {
    uint32_t raw = (static_cast<uint32_t>(pol) & 0x1u)
                 | ((static_cast<uint32_t>(x) & 0x7FFu) << 1)
                 | ((static_cast<uint32_t>(y) & 0x3FFu) << 12);
    return {
        static_cast<uint8_t>(raw & 0xFF),
        static_cast<uint8_t>((raw >> 8)  & 0xFF),
        static_cast<uint8_t>((raw >> 16) & 0xFF),
        static_cast<uint8_t>((raw >> 24) & 0xFF),
    };
}

} // anonymous namespace

class IITDatalogLoaderTest : public ::testing::Test {
protected:
    std::string tmp_path_;

    void SetUp() override {
        tmp_path_ = (std::filesystem::temp_directory_path() / "test_iitdatalog.log").string();
    }

    void TearDown() override {
        std::filesystem::remove(tmp_path_);
    }
};

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

TEST_F(IITDatalogLoaderTest, OpenNonexistentFileReturnsFalse) {
    IITDatalogLoader loader;
    EXPECT_FALSE(loader.open("/nonexistent/path/to/file.log"));
    EXPECT_FALSE(loader.isOpen());
}

TEST_F(IITDatalogLoaderTest, OpenSyntheticFileSucceeds) {
    auto bytes = encodeEvent(true, 10, 20);
    writeSyntheticFile2(tmp_path_, { {0, bytes} });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));
    EXPECT_TRUE(loader.isOpen());
    EXPECT_EQ(loader.packetCount(), 1u);
    loader.close();
    EXPECT_FALSE(loader.isOpen());
}

// ---------------------------------------------------------------------------
// Event decode
// ---------------------------------------------------------------------------

TEST_F(IITDatalogLoaderTest, DecodesSingleEvent) {
    // polarity=1, x=473, y=120 → raw=0x000783B3
    auto bytes = encodeEvent(true, 473, 120);
    writeSyntheticFile2(tmp_path_, { {1000, bytes} });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));

    // readChunk [0, 2000) should capture the packet at t=1000
    auto chunk = loader.readChunk(0, 2000);
    ASSERT_EQ(chunk.data.size(), 1u);
    const auto& ev = chunk.data[0];
    EXPECT_EQ(ev.t, 1000);
    EXPECT_EQ(ev.x, 473);
    EXPECT_EQ(ev.y, 120);
    EXPECT_TRUE(ev.polarity);
}

TEST_F(IITDatalogLoaderTest, DecodesMultipleEventsPerPacket) {
    std::vector<uint8_t> payload;
    auto a = encodeEvent(true,  10, 20);
    auto b = encodeEvent(false, 30, 40);
    payload.insert(payload.end(), a.begin(), a.end());
    payload.insert(payload.end(), b.begin(), b.end());

    writeSyntheticFile2(tmp_path_, { {5000, payload} });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));
    auto chunk = loader.readChunk(0, 10000);
    ASSERT_EQ(chunk.data.size(), 2u);
    EXPECT_EQ(chunk.data[0].x, 10);
    EXPECT_EQ(chunk.data[0].y, 20);
    EXPECT_TRUE(chunk.data[0].polarity);
    EXPECT_EQ(chunk.data[1].x, 30);
    EXPECT_EQ(chunk.data[1].y, 40);
    EXPECT_FALSE(chunk.data[1].polarity);
}

// ---------------------------------------------------------------------------
// Timestamp and range queries
// ---------------------------------------------------------------------------

TEST_F(IITDatalogLoaderTest, StartTimeAndEndTime) {
    auto b = encodeEvent(false, 0, 0);
    writeSyntheticFile2(tmp_path_, {
        {0,     b},
        {1000,  b},
        {54000000, b}, // 54 seconds
    });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));
    EXPECT_EQ(loader.startTime(), 0);
    EXPECT_EQ(loader.endTime(),   54000000);
}

TEST_F(IITDatalogLoaderTest, ChunkRangeIsHalfOpen) {
    auto b = encodeEvent(true, 1, 1);
    writeSyntheticFile2(tmp_path_, {
        {0,    b},
        {1000, b},
        {2000, b},
    });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));

    // [0, 2000) should include t=0 and t=1000, exclude t=2000
    auto chunk = loader.readChunk(0, 2000);
    ASSERT_EQ(chunk.data.size(), 2u);
    EXPECT_EQ(chunk.data[0].t, 0);
    EXPECT_EQ(chunk.data[1].t, 1000);
}

TEST_F(IITDatalogLoaderTest, EmptyChunkForOutOfRangeQuery) {
    auto b = encodeEvent(false, 0, 0);
    writeSyntheticFile2(tmp_path_, { {1000, b} });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));

    auto chunk = loader.readChunk(5000, 6000);
    EXPECT_TRUE(chunk.data.empty());
}

// ---------------------------------------------------------------------------
// Escape sequences
// ---------------------------------------------------------------------------

TEST_F(IITDatalogLoaderTest, UnescapedNulByteDecoded) {
    // Manually build an event with raw byte 0x00 (encoded as \0 in file)
    // polarity=0, x=0, y=0 → raw=0x00000000 (all bytes are 0x00)
    auto bytes = encodeEvent(false, 0, 0); // {0x00, 0x00, 0x00, 0x00}
    writeSyntheticFile2(tmp_path_, { {500, bytes} });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));
    auto chunk = loader.readChunk(0, 1000);
    ASSERT_EQ(chunk.data.size(), 1u);
    EXPECT_EQ(chunk.data[0].x, 0);
    EXPECT_EQ(chunk.data[0].y, 0);
    EXPECT_FALSE(chunk.data[0].polarity);
}

// ---------------------------------------------------------------------------
// Sensor resolution detection
// ---------------------------------------------------------------------------

TEST_F(IITDatalogLoaderTest, SensorResolutionDetected) {
    // max x=639, max y=479 → sensor 640×480
    auto b1 = encodeEvent(true,  639, 0);
    auto b2 = encodeEvent(false, 0,   479);
    writeSyntheticFile2(tmp_path_, {
        {0,    b1},
        {1000, b2},
    });

    IITDatalogLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_));
    EXPECT_EQ(loader.sensorWidth(),  640);
    EXPECT_EQ(loader.sensorHeight(), 480);
}
