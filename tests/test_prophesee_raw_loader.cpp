#include "mustard/data/events/PropheseeRawLoader.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mustard;

namespace {

template <typename T>
void writeLE(std::ofstream& f, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        f.put(static_cast<char>((static_cast<uint64_t>(value) >> (8U * i)) & 0xFFu));
    }
}

void writeHeader(std::ofstream& f, const std::string& format_line, int width, int height) {
    f << "% camera_integrator_name Prophesee\n";
    f << "% plugin_integrator_name Prophesee\n";
    f << "% " << format_line << ";height=" << height << ";width=" << width << "\n";
    f << "% end\n";
}

uint32_t evt2TimeHigh(uint32_t high) {
    return (0x8u << 28) | (high & 0x0FFFFFFFu);
}

uint32_t evt2Cd(bool pol, uint32_t low, uint32_t x, uint32_t y) {
    return ((pol ? 0x1u : 0x0u) << 28)
         | ((low & 0x3Fu) << 22)
         | ((x & 0x7FFu) << 11)
         | (y & 0x7FFu);
}

} // namespace

class PropheseeRawLoaderTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_path_;

    void SetUp() override {
        tmp_path_ = std::filesystem::temp_directory_path() / "test_prophesee_raw.raw";

        std::ofstream f(tmp_path_, std::ios::binary);
        writeHeader(f, "format EVT2", 640, 480);
        writeLE<uint32_t>(f, evt2TimeHigh(0));
        writeLE<uint32_t>(f, evt2Cd(true, 40, 10, 20));
        writeLE<uint32_t>(f, evt2Cd(false, 50, 11, 21));
    }

    void TearDown() override {
        std::filesystem::remove(tmp_path_);
        std::filesystem::remove(tmp_path_.string() + ".tmp_index");
    }
};

TEST_F(PropheseeRawLoaderTest, OpensWithOpenEbMetadata) {
    PropheseeRawLoader loader;

    ASSERT_TRUE(loader.open(tmp_path_.string()));
    EXPECT_EQ(loader.sensorWidth(), 640);
    EXPECT_EQ(loader.sensorHeight(), 480);
    EXPECT_EQ(loader.format(), PropheseeRawLoader::Format::kEvt2);
    EXPECT_EQ(loader.startTime(), 40);
    EXPECT_EQ(loader.endTime(), 50);
}

TEST_F(PropheseeRawLoaderTest, ReadsHalfOpenWindowsThroughOpenEbSeek) {
    PropheseeRawLoader loader;
    ASSERT_TRUE(loader.open(tmp_path_.string()));

    auto all = loader.readChunk(0, 100);
    ASSERT_EQ(all.data.size(), 2u);
    EXPECT_EQ(all.data[0].t, 40);
    EXPECT_EQ(all.data[0].x, 10);
    EXPECT_EQ(all.data[0].y, 20);
    EXPECT_TRUE(all.data[0].polarity);
    EXPECT_EQ(all.data[1].t, 50);
    EXPECT_EQ(all.data[1].x, 11);
    EXPECT_EQ(all.data[1].y, 21);
    EXPECT_FALSE(all.data[1].polarity);

    auto second = loader.readChunk(45, 55);
    ASSERT_EQ(second.data.size(), 1u);
    EXPECT_EQ(second.data[0].t, 50);

    auto excluded = loader.readChunk(40, 50);
    ASSERT_EQ(excluded.data.size(), 1u);
    EXPECT_EQ(excluded.data[0].t, 40);
}
