#include "mustard/data/events/TabularEventLoader.h"
#include "mustard/data/events/TabularEventSource.h"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace mustard;

namespace {

struct SourceStats { std::size_t reads{0}; };

class CountingDirectSource final : public TabularEventSource {
public:
    CountingDirectSource(uint64_t count, std::shared_ptr<SourceStats> stats)
        : count_(count), stats_(std::move(stats)) {}
    const std::vector<std::string>& columns() const override { return columns_; }
    uint64_t checkpoint() const override { return row_; }
    bool seek(uint64_t checkpoint, ImportDiagnostic&) override {
        if (checkpoint > count_) return false;
        row_ = checkpoint;
        return true;
    }
    bool readRows(std::size_t, std::vector<TableRow>&, ImportDiagnostic&) override {
        return false;
    }
    float progress() const override { return count_ ? static_cast<float>(row_) / count_ : 1.f; }
    bool readsEventsDirectly() const noexcept override { return true; }
    bool readEvents(std::size_t maximum, std::vector<DVSEvent>& events,
                    ImportDiagnostic&) override {
        ++stats_->reads;
        events.clear();
        const uint64_t end = std::min<uint64_t>(count_, row_ + maximum);
        events.reserve(static_cast<std::size_t>(end - row_));
        while (row_ < end) {
            events.push_back({static_cast<int64_t>(row_),
                              static_cast<uint16_t>(row_ % 128),
                              static_cast<uint16_t>(row_ % 64), (row_ & 1) != 0});
            ++row_;
        }
        return true;
    }
private:
    uint64_t count_;
    uint64_t row_{0};
    std::shared_ptr<SourceStats> stats_;
    std::vector<std::string> columns_{"x", "y", "ts", "p"};
};

std::unique_ptr<TabularEventLoader> openCsvLoader(const std::string& csv, EventImportOptions options = {}) {
    auto loader = std::make_unique<TabularEventLoader>();
    ImportDiagnostic error;
    auto source = createCsvSource(std::make_unique<std::istringstream>(csv), options, error);
    if (!source) return nullptr;
    if (!loader->openSource(std::move(source), options)) return nullptr;
    return loader;
}

} // namespace

TEST(TabularEventLoader, LoadsEventsAndInfersGeometry) {
    std::string csv = "x,y,ts,p\n10,20,100,1\n30,40,200,0\n";
    auto loader = openCsvLoader(csv);
    ASSERT_NE(loader, nullptr);

    EXPECT_EQ(loader->sensorWidth(), 31);
    EXPECT_EQ(loader->sensorHeight(), 41);
    EXPECT_EQ(loader->startTime(), 100);
    EXPECT_EQ(loader->endTime(), 200);
    EXPECT_EQ(loader->eventCount(), 2u);

    DataChunk<DVSEvent> chunk = loader->readChunk(100, 150);
    ASSERT_EQ(chunk.data.size(), 1u);
    EXPECT_EQ(chunk.data[0].x, 10);
    EXPECT_EQ(chunk.data[0].y, 20);
    EXPECT_EQ(chunk.data[0].t, 100);
    EXPECT_TRUE(chunk.data[0].polarity);
}

TEST(TabularEventLoader, SupportsTimestampUnits) {
    std::string csv = "x,y,ts,p\n1,1,1.5,1\n2,2,2.0,0\n";
    EventImportOptions options;
    options.timestamp_unit = TimestampUnit::Seconds;

    auto loader = openCsvLoader(csv, options);
    ASSERT_NE(loader, nullptr);

    EXPECT_EQ(loader->startTime(), 1'500'000);
    EXPECT_EQ(loader->endTime(), 2'000'000);
}

TEST(TabularEventLoader, InfersPluralEventColumnNames) {
    auto loader = openCsvLoader("xs,ys,ts,ps\n3,4,500,1\n4,5,600,0\n");
    ASSERT_NE(loader, nullptr);
    const auto chunk = loader->readChunk(500, 601);
    ASSERT_EQ(chunk.data.size(), 2u);
    EXPECT_EQ(chunk.data.front().x, 3);
    EXPECT_EQ(chunk.data.front().y, 4);
    EXPECT_TRUE(chunk.data.front().polarity);
}

TEST(TabularEventLoader, RejectsDescendingTimestamps) {
    std::string csv = "x,y,ts,p\n1,1,200,1\n2,2,100,0\n";
    auto loader = openCsvLoader(csv);
    EXPECT_EQ(loader, nullptr);
}

TEST(TabularEventLoader, EnforcesSensorBoundsWhenSupplied) {
    std::string csv = "x,y,ts,p\n100,100,100,1\n";
    EventImportOptions options;
    options.sensor_width = 50;
    options.sensor_height = 50;

    auto loader = openCsvLoader(csv, options);
    EXPECT_EQ(loader, nullptr);
}

TEST(TabularEventLoader, AppliesCallbackPipelineToChunks) {
    std::string csv = "x,y,ts,p\n1,1,100,1\n2,2,120,0\n3,3,150,1\n";
    auto loader = openCsvLoader(csv);
    ASSERT_NE(loader, nullptr);

    // Filter callback to keep only ON polarity events
    loader->addCallback([](DataChunk<DVSEvent>& chunk) {
        auto it = std::remove_if(chunk.data.begin(), chunk.data.end(), [](const DVSEvent& e) {
            return !e.polarity;
        });
        chunk.data.erase(it, chunk.data.end());
    });

    DataChunk<DVSEvent> chunk = loader->readChunk(100, 200);
    ASSERT_EQ(chunk.data.size(), 2u);
    EXPECT_EQ(chunk.data[0].x, 1);
    EXPECT_EQ(chunk.data[1].x, 3);
}

TEST(TabularEventLoader, HalfOpenIntervalReadChunk) {
    std::string csv = "x,y,ts,p\n1,1,100,1\n2,2,150,0\n3,3,200,1\n";
    auto loader = openCsvLoader(csv);
    ASSERT_NE(loader, nullptr);

    DataChunk<DVSEvent> chunk = loader->readChunk(100, 200);
    ASSERT_EQ(chunk.data.size(), 2u); // 100, 150 (200 is excluded because interval is [t0, t1))
}

TEST(TabularEventLoader, ReusesDecodedSourceBlockAcrossAdjacentChunks) {
    auto stats = std::make_shared<SourceStats>();
    auto source = std::make_unique<CountingDirectSource>(70'000, stats);
    TabularEventLoader loader;
    ASSERT_TRUE(loader.openSource(std::move(source), EventImportOptions{}));
    const auto indexing_reads = stats->reads;

    EXPECT_EQ(loader.readChunk(0, 10'000).data.size(), 10'000u);
    EXPECT_EQ(stats->reads, indexing_reads + 1);
    EXPECT_EQ(loader.readChunk(10'000, 20'000).data.size(), 10'000u);
    EXPECT_EQ(stats->reads, indexing_reads + 1);

    EXPECT_EQ(loader.readChunk(65'536, 70'000).data.size(), 4'464u);
    EXPECT_EQ(stats->reads, indexing_reads + 2);
}
