#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mustard {

enum class EventSourceFormat { Csv, Hdf5 };
enum class TimestampUnit { Microseconds, Seconds, Milliseconds, Nanoseconds };
enum class CsvDelimiter { Auto, Comma, Tab, Semicolon, Whitespace };
enum class CsvHeader { Auto, Present, Absent };

using ImportCancellation = std::shared_ptr<std::atomic_bool>;
inline bool importCancelled(const ImportCancellation& cancel) {
    return cancel && cancel->load();
}

struct EventImportOptions {
    EventSourceFormat format{EventSourceFormat::Csv};
    std::string dataset;
    // x, y, timestamp, polarity. -1 means infer from names or positional order.
    std::array<int, 4> columns{{-1, -1, -1, -1}};
    TimestampUnit timestamp_unit{TimestampUnit::Microseconds};
    CsvDelimiter delimiter{CsvDelimiter::Auto};
    CsvHeader header{CsvHeader::Auto};
    bool events_in_columns{false};
    int sensor_width{0};  // 0 means infer from all events.
    int sensor_height{0};
};

struct ImportDiagnostic {
    std::string path;
    std::string dataset;
    uint64_t row{0}; // 1-based data record, 0 when not applicable.
    std::string field;
    std::string message;
    bool cancelled{false};
    std::string describe() const;
};

using TableCell = std::variant<int64_t, uint64_t, double, std::string, bool>;
using TableRow = std::vector<TableCell>;
std::string tableCellText(const TableCell& cell);
/// Recognizes x/xs, y/ys, t/ts/timestamp, and p/ps/polarity.
std::array<int, 4> inferEventColumns(const std::vector<std::string>& names);

struct EventTablePreview {
    std::vector<std::string> columns;
    std::vector<TableRow> rows;
    std::array<int, 4> suggested_columns{{-1, -1, -1, -1}};
};

struct Hdf5Entry {
    std::string path;
    bool group{false};
    std::vector<uint64_t> shape;
    std::string datatype;
    bool selectable{false};
    std::string reason;
};

bool browseHdf5(const std::string& path, std::vector<Hdf5Entry>& entries,
                ImportDiagnostic& error, ImportCancellation cancel = {});
bool previewEventSource(const std::string& path, const EventImportOptions& options,
                        EventTablePreview& preview, ImportDiagnostic& error,
                        ImportCancellation cancel = {});

} // namespace mustard
