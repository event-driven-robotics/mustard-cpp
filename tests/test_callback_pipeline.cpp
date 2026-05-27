#include "mustard/data/events/IITDatalogLoader.h"
#include "mustard/data/DVSEvent.h"

#include <gtest/gtest.h>

#include <vector>

using namespace mustard;

class CallbackPipelineTest : public ::testing::Test {
protected:
    /// Build a chunk with @p n events, all with polarity=true, t=0.
    DataChunk<DVSEvent> makeChunk(int n) {
        DataChunk<DVSEvent> c;
        c.t_start = 0;
        c.t_end   = 1000;
        c.data.resize(static_cast<std::size_t>(n));
        for (auto& ev : c.data) {
            ev.polarity = true;
            ev.t = 0;
        }
        return c;
    }
};

// ---------------------------------------------------------------------------
// DataLoader callback pipeline (tested via a concrete subclass)
// ---------------------------------------------------------------------------

/// Minimal concrete DataLoader that returns a preset chunk for testing.
class FakeLoader : public DataLoader<DVSEvent> {
public:
    DataChunk<DVSEvent> preset;

    bool open(const std::string&) override { open_ = true;  return true; }
    void close() override                  { open_ = false; }
    bool isOpen() const override           { return open_; }
    int64_t startTime() const override     { return 0; }
    int64_t endTime()   const override     { return 1000; }

protected:
    DataChunk<DVSEvent> readChunkImpl(int64_t, int64_t) override { return preset; }

private:
    bool open_{false};
};

TEST_F(CallbackPipelineTest, NoCallbacksReturnPreset) {
    FakeLoader loader;
    loader.preset = makeChunk(5);
    loader.open("");

    auto chunk = loader.readChunk(0, 1000);
    EXPECT_EQ(chunk.data.size(), 5u);
}

TEST_F(CallbackPipelineTest, SingleCallbackApplied) {
    FakeLoader loader;
    loader.preset = makeChunk(5);
    loader.open("");

    // Callback: remove all events
    loader.addCallback([](DataChunk<DVSEvent>& c) {
        c.data.clear();
    });

    auto chunk = loader.readChunk(0, 1000);
    EXPECT_EQ(chunk.data.size(), 0u);
}

TEST_F(CallbackPipelineTest, TwoCallbacksAppliedInOrder) {
    FakeLoader loader;
    loader.open("");

    std::vector<int> call_order;

    loader.addCallback([&call_order](DataChunk<DVSEvent>&) {
        call_order.push_back(1);
    });
    loader.addCallback([&call_order](DataChunk<DVSEvent>&) {
        call_order.push_back(2);
    });

    loader.preset = makeChunk(1);
    loader.readChunk(0, 1000);

    ASSERT_EQ(call_order.size(), 2u);
    EXPECT_EQ(call_order[0], 1);
    EXPECT_EQ(call_order[1], 2);
}

TEST_F(CallbackPipelineTest, CallbackCanFilterEvents) {
    FakeLoader loader;
    loader.open("");

    // 10 events: y=0..9
    DataChunk<DVSEvent> preset;
    preset.t_start = 0;
    preset.t_end   = 1000;
    for (int i = 0; i < 10; ++i) {
        DVSEvent ev;
        ev.y = static_cast<uint16_t>(i);
        preset.data.push_back(ev);
    }
    loader.preset = std::move(preset);

    // Filter: keep only events with y >= 5
    loader.addCallback([](DataChunk<DVSEvent>& c) {
        c.data.erase(
            std::remove_if(c.data.begin(), c.data.end(),
                           [](const DVSEvent& e) { return e.y < 5; }),
            c.data.end());
    });

    auto chunk = loader.readChunk(0, 1000);
    ASSERT_EQ(chunk.data.size(), 5u);
    for (const auto& ev : chunk.data) {
        EXPECT_GE(ev.y, 5);
    }
}

TEST_F(CallbackPipelineTest, CallbackModifiesData) {
    FakeLoader loader;
    loader.preset = makeChunk(3);
    loader.open("");

    // Flip polarity of all events
    loader.addCallback([](DataChunk<DVSEvent>& c) {
        for (auto& ev : c.data) {
            ev.polarity = !ev.polarity;
        }
    });

    auto chunk = loader.readChunk(0, 1000);
    for (const auto& ev : chunk.data) {
        EXPECT_FALSE(ev.polarity); // was true, now false
    }
}
