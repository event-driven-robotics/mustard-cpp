#pragma once
#include "mustard/data/DataLoader.h"
#include "mustard/data/DVSEvent.h"
#include "mustard/data/events/TabularEventSource.h"

#include <limits>

namespace mustard {

class TabularEventLoader : public DataLoader<DVSEvent> {
public:
    static constexpr std::size_t kBlockRows = 65'536;
    bool open(const std::string& path) override;
    bool open(const std::string& path, const EventImportOptions& options,
              ImportCancellation cancel = {});
    bool openSource(std::unique_ptr<TabularEventSource> source,
                    const EventImportOptions& options, ImportCancellation cancel = {},
                    const std::string& path = {});
    void close() override;
    bool isOpen() const override { return source_ && !index_.empty(); }
    int64_t startTime() const override { return start_time_; }
    int64_t endTime() const override { return end_time_; }
    int sensorWidth() const noexcept { return width_; }
    int sensorHeight() const noexcept { return height_; }
    const ImportDiagnostic& lastError() const noexcept { return error_; }
    std::size_t indexSize() const noexcept { return index_.size(); }
    uint64_t eventCount() const noexcept { return event_count_; }

protected:
    DataChunk<DVSEvent> readChunkImpl(int64_t t0, int64_t t1) override;

private:
    struct Block {
        uint64_t checkpoint;
        uint64_t first_row;
        std::size_t count;
        int64_t first_time;
        int64_t last_time;
    };
    bool loadBlock(std::size_t index, const std::vector<DVSEvent>*& events);
    bool readSourceBlock(std::size_t max_rows, uint64_t first_row,
                         std::vector<DVSEvent>& events);
    bool convert(const TableRow& row, uint64_t row_number, DVSEvent& event);
    bool fail(const std::string& message, uint64_t row = 0, const std::string& field = {});
    std::unique_ptr<TabularEventSource> source_;
    EventImportOptions options_;
    ImportCancellation cancel_;
    ImportDiagnostic error_;
    std::vector<Block> index_;
    int64_t start_time_{0};
    int64_t end_time_{0};
    uint64_t event_count_{0};
    int width_{0};
    int height_{0};
    std::size_t cached_block_{std::numeric_limits<std::size_t>::max()};
    std::vector<DVSEvent> cached_events_;
};

} // namespace mustard
