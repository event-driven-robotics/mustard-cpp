#include "mustard/data/events/TabularEventLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace mustard {
namespace {

// Decimal parsing avoids routing integer timestamps through floating point,
// including on Windows where long double has only double precision.
struct Decimal {
    bool negative{false};
    std::string digits;
    int64_t exponent{0};
};

bool parseDecimal(const TableCell& cell, Decimal& value) {
    const std::string text = tableCellText(cell);
    std::size_t i = text.find_first_not_of(" \t\r\n");
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    if (i == std::string::npos) return false;
    const std::size_t end = last + 1;
    if (text[i] == '+' || text[i] == '-') value.negative = text[i++] == '-';
    bool dot = false;
    bool digit = false;
    for (; i < end; ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            value.digits.push_back(c);
            digit = true;
            if (dot) --value.exponent;
        } else if (c == '.' && !dot) dot = true;
        else break;
    }
    if (!digit) return false;
    if (i < end && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool minus = false;
        if (i < end && (text[i] == '+' || text[i] == '-')) minus = text[i++] == '-';
        const auto begin = i;
        int64_t exponent = 0;
        for (; i < end && text[i] >= '0' && text[i] <= '9'; ++i) {
            if (exponent > 1'000'000) return false;
            exponent = exponent * 10 + text[i] - '0';
        }
        if (begin == i) return false;
        value.exponent += minus ? -exponent : exponent;
    }
    if (i != end) return false;
    const auto nonzero = value.digits.find_first_not_of('0');
    if (nonzero == std::string::npos) {
        value.digits = "0";
        value.negative = false;
        value.exponent = 0;
    } else value.digits.erase(0, nonzero);
    return true;
}

bool unsignedValue(const Decimal& number, int scale, uint64_t maximum,
                   bool round, uint64_t& result) {
    if (number.negative) return false;
    const int64_t places = number.exponent + scale;
    const int64_t whole_digits = static_cast<int64_t>(number.digits.size()) + places;
    if (number.digits == "0") { result = 0; return true; }
    if (whole_digits > 20) return false;
    result = 0;
    for (int64_t i = 0; i < whole_digits; ++i) {
        const unsigned digit = i < static_cast<int64_t>(number.digits.size())
            ? static_cast<unsigned>(number.digits[static_cast<std::size_t>(i)] - '0') : 0;
        if (result > maximum / 10 || (result == maximum / 10 && digit > maximum % 10))
            return false;
        result = result * 10 + digit;
    }
    const auto fraction_begin = static_cast<std::size_t>(std::max<int64_t>(0, whole_digits));
    if (!round && fraction_begin < number.digits.size() &&
        number.digits.find_first_not_of('0', fraction_begin) != std::string::npos) return false;
    if (round && whole_digits >= 0 && fraction_begin < number.digits.size() &&
        number.digits[fraction_begin] >= '5') {
        if (result == maximum) return false;
        ++result;
    }
    return true;
}

bool numericUnsigned(const TableCell& cell, int scale, uint64_t maximum,
                     bool round, uint64_t& result) {
    if (const auto* value = std::get_if<uint64_t>(&cell)) {
        if (scale >= 0) {
            result = *value;
            for (int i = 0; i < scale; ++i) {
                if (result > maximum / 10) return false;
                result *= 10;
            }
            return result <= maximum;
        }
        uint64_t divisor = 1;
        for (int i = 0; i < -scale; ++i) {
            if (divisor > std::numeric_limits<uint64_t>::max() / 10) return false;
            divisor *= 10;
        }
        const uint64_t remainder = *value % divisor;
        result = *value / divisor;
        if (round && remainder >= (divisor + 1) / 2) {
            if (result == maximum) return false;
            ++result;
        } else if (!round && remainder != 0) return false;
        return result <= maximum;
    }
    if (const auto* value = std::get_if<int64_t>(&cell)) {
        if (*value < 0) return false;
        return numericUnsigned(TableCell{static_cast<uint64_t>(*value)}, scale,
                               maximum, round, result);
    }
    if (const auto* value = std::get_if<bool>(&cell)) {
        return numericUnsigned(TableCell{static_cast<uint64_t>(*value)}, scale,
                               maximum, round, result);
    }
    if (const auto* value = std::get_if<double>(&cell)) {
        if (!std::isfinite(*value) || *value < 0.0) return false;
        long double scaled = static_cast<long double>(*value);
        for (int i = 0; i < scale; ++i) scaled *= 10.0L;
        for (int i = 0; i < -scale; ++i) scaled /= 10.0L;
        const long double converted = round ? std::round(scaled) : scaled;
        if ((!round && std::floor(scaled) != scaled) || converted > maximum) return false;
        result = static_cast<uint64_t>(converted);
        return true;
    }
    Decimal value;
    return parseDecimal(cell, value) && unsignedValue(value, scale, maximum, round, result);
}

} // namespace

bool TabularEventLoader::open(const std::string& path) {
    EventImportOptions options;
    return open(path, options);
}

bool TabularEventLoader::open(const std::string& path, const EventImportOptions& options,
                              ImportCancellation cancel) {
    close();
    error_ = {};
    error_.path = path;
    error_.dataset = options.dataset;
    auto source = options.format == EventSourceFormat::Hdf5
        ? createHdf5Source(path, options, error_, cancel)
        : createCsvSource(path, options, error_, cancel);
    if (!source) return false;
    return openSource(std::move(source), options, std::move(cancel), path);
}

bool TabularEventLoader::fail(const std::string& message, uint64_t row,
                              const std::string& field) {
    error_.message = message;
    error_.row = row;
    error_.field = field;
    return false;
}

bool TabularEventLoader::openSource(std::unique_ptr<TabularEventSource> source,
                                    const EventImportOptions& options,
                                    ImportCancellation cancel, const std::string& path) {
    close();
    error_ = {};
    error_.path = path;
    error_.dataset = options.dataset;
    options_ = options;
    cancel_ = std::move(cancel);
    source_ = std::move(source);
    auto abort = [&]() {
        // Source errors generally carry their own path. Preserve loader context
        // when an adapter only supplied the message.
        if (error_.path.empty()) error_.path = path;
        if (error_.dataset.empty()) error_.dataset = options.dataset;
        close();
        return false;
    };
    if (!source_) { fail("Could not open event data"); return abort(); }
    if (options.sensor_width < 0 || options.sensor_width > 65536 ||
        options.sensor_height < 0 || options.sensor_height > 65536) {
        fail("Sensor dimensions must be 1–65536 pixels, or 0 to infer"); return abort();
    }
    const auto inferred = inferEventColumns(source_->columns());
    const char* fields[] = {"x", "y", "timestamp", "polarity"};
    for (std::size_t i = 0; i < 4; ++i) {
        if (options_.columns[i] < 0) options_.columns[i] = inferred[i];
        const int column = options_.columns[i];
        if (column < 0 || static_cast<std::size_t>(column) >= source_->columns().size()) {
            fail("Choose a column for this field", 0, fields[i]); return abort();
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (column == options_.columns[j]) {
                fail("Each event field must use a different column", 0, fields[i]); return abort();
            }
        }
    }
    reportProgress(0.0f, "Validating and indexing events");
    int64_t previous = 0;
    std::vector<TableRow> rows;
    for (;;) {
        if (importCancelled(cancel_)) {
            error_.cancelled = true; fail("Import cancelled"); return abort();
        }
        const uint64_t checkpoint = source_->checkpoint();
        std::vector<DVSEvent> direct_events;
        const bool direct = source_->readsEventsDirectly();
        if (direct) {
            if (!source_->readEvents(kBlockRows, direct_events, error_)) return abort();
        } else if (!source_->readRows(kBlockRows, rows, error_)) return abort();
        const std::size_t count = direct ? direct_events.size() : rows.size();
        if (count == 0) break;
        Block block{checkpoint, event_count_ + 1, count, 0, 0};
        for (std::size_t i = 0; i < count; ++i) {
            if ((i % 1024) == 0 && importCancelled(cancel_)) {
                error_.cancelled = true; fail("Import cancelled"); return abort();
            }
            DVSEvent event;
            if (direct) event = direct_events[i];
            else if (!convert(rows[i], event_count_ + 1, event)) return abort();
            if (direct && ((options_.sensor_width && event.x >= options_.sensor_width) ||
                           (options_.sensor_height && event.y >= options_.sensor_height))) {
                fail("Coordinate exceeds the supplied sensor dimension", event_count_ + 1,
                     options_.sensor_width && event.x >= options_.sensor_width ? "x" : "y");
                return abort();
            }
            if (event_count_ && event.t < previous) {
                fail("Timestamps must be nondecreasing; check the timestamp mapping or sort the source data",
                     event_count_ + 1, "timestamp"); return abort();
            }
            if (i == 0) block.first_time = event.t;
            block.last_time = event.t;
            previous = event.t;
            width_ = std::max(width_, static_cast<int>(event.x) + 1);
            height_ = std::max(height_, static_cast<int>(event.y) + 1);
            ++event_count_;
        }
        index_.push_back(block);
        reportProgress(source_->progress(), "Validating and indexing events");
    }
    if (index_.empty()) { fail("The selected table contains no event records"); return abort(); }
    start_time_ = index_.front().first_time;
    end_time_ = index_.back().last_time;
    if (options_.sensor_width) width_ = options_.sensor_width;
    if (options_.sensor_height) height_ = options_.sensor_height;
    // A completed source must not retain an import cancellation request. The
    // adapter's token stays false; a later import receives a fresh token.
    source_->finishImport();
    cancel_.reset();
    reportProgress(1.0f, "Ready");
    return true;
}

void TabularEventLoader::close() {
    source_.reset();
    index_.clear();
    cancel_.reset();
    start_time_ = end_time_ = 0;
    event_count_ = 0;
    width_ = height_ = 0;
    cached_block_ = std::numeric_limits<std::size_t>::max();
    cached_events_.clear();
}

bool TabularEventLoader::convert(const TableRow& row, uint64_t row_number, DVSEvent& event) {
    const char* fields[] = {"x", "y", "timestamp", "polarity"};
    for (std::size_t i = 0; i < 4; ++i) {
        if (options_.columns[i] < 0 || static_cast<std::size_t>(options_.columns[i]) >= row.size())
            return fail("Record is missing the selected column", row_number, fields[i]);
    }
    uint64_t coordinates[2]{};
    for (int i = 0; i < 2; ++i) {
        if (!numericUnsigned(row[options_.columns[i]], 0, 65535, false, coordinates[i]))
            return fail("Coordinate must be a finite integer between 0 and 65535", row_number, fields[i]);
        const int dimension = i == 0 ? options_.sensor_width : options_.sensor_height;
        if (dimension && coordinates[i] >= static_cast<uint64_t>(dimension))
            return fail("Coordinate exceeds the supplied sensor dimension", row_number, fields[i]);
    }
    event.x = static_cast<uint16_t>(coordinates[0]);
    event.y = static_cast<uint16_t>(coordinates[1]);
    int scale = 0;
    switch (options_.timestamp_unit) {
        case TimestampUnit::Seconds: scale = 6; break;
        case TimestampUnit::Milliseconds: scale = 3; break;
        case TimestampUnit::Nanoseconds: scale = -3; break;
        case TimestampUnit::Microseconds: break;
    }
    uint64_t micros = 0;
    constexpr uint64_t kMaximumTime = static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - 10'000);
    if (!numericUnsigned(row[options_.columns[2]], scale, kMaximumTime, true, micros))
        return fail("Timestamp must be finite, nonnegative, and fit the microsecond playback range",
                    row_number, "timestamp");
    event.t = static_cast<int64_t>(micros);
    const TableCell& polarity_cell = row[options_.columns[3]];
    if (const auto* value = std::get_if<bool>(&polarity_cell)) {
        event.polarity = *value;
        return true;
    }
    if (const auto* value = std::get_if<uint64_t>(&polarity_cell)) {
        if (*value > 1) return fail("Polarity must be 0, 1, -1, true, or false", row_number, "polarity");
        event.polarity = *value == 1;
        return true;
    }
    if (const auto* value = std::get_if<int64_t>(&polarity_cell)) {
        if (*value != -1 && *value != 0 && *value != 1)
            return fail("Polarity must be 0, 1, -1, true, or false", row_number, "polarity");
        event.polarity = *value == 1;
        return true;
    }
    if (const auto* value = std::get_if<double>(&polarity_cell)) {
        if (!std::isfinite(*value) || (*value != -1.0 && *value != 0.0 && *value != 1.0))
            return fail("Polarity must be 0, 1, -1, true, or false", row_number, "polarity");
        event.polarity = *value == 1.0;
        return true;
    }
    std::string polarity = tableCellText(polarity_cell);
    const auto first = polarity.find_first_not_of(" \t\r\n");
    const auto last = polarity.find_last_not_of(" \t\r\n");
    polarity = first == std::string::npos ? "" : polarity.substr(first, last - first + 1);
    std::transform(polarity.begin(), polarity.end(), polarity.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (polarity == "true" || polarity == "false") event.polarity = polarity == "true";
    else {
        Decimal value;
        uint64_t magnitude = 0;
        if (!parseDecimal(row[options_.columns[3]], value))
            return fail("Polarity must be 0, 1, -1, true, or false", row_number, "polarity");
        const bool negative = value.negative;
        value.negative = false;
        if (!unsignedValue(value, 0, 1, false, magnitude))
            return fail("Polarity must be 0, 1, -1, true, or false", row_number, "polarity");
        event.polarity = !negative && magnitude == 1;
    }
    return true;
}

bool TabularEventLoader::loadBlock(std::size_t block_index,
                                   const std::vector<DVSEvent>*& events) {
    if (cached_block_ == block_index) {
        events = &cached_events_;
        return true;
    }
    const Block& block = index_[block_index];
    if (!source_->seek(block.checkpoint, error_)) return false;
    std::vector<DVSEvent> decoded;
    if (!readSourceBlock(block.count, block.first_row, decoded) || decoded.size() != block.count) {
        if (error_.message.empty()) fail("Event source changed or could not be read", block.first_row);
        return false;
    }
    cached_events_ = std::move(decoded);
    cached_block_ = block_index;
    events = &cached_events_;
    return true;
}

bool TabularEventLoader::readSourceBlock(std::size_t max_rows, uint64_t first_row,
                                         std::vector<DVSEvent>& events) {
    events.clear();
    if (source_->readsEventsDirectly())
        return source_->readEvents(max_rows, events, error_);
    std::vector<TableRow> rows;
    if (!source_->readRows(max_rows, rows, error_)) return false;
    events.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        DVSEvent event;
        if (!convert(rows[i], first_row + i, event)) return false;
        events.push_back(event);
    }
    return true;
}

DataChunk<DVSEvent> TabularEventLoader::readChunkImpl(int64_t t0, int64_t t1) {
    DataChunk<DVSEvent> chunk{t0, t1, {}};
    if (!isOpen() || t0 >= t1) return chunk;
    auto it = std::lower_bound(index_.begin(), index_.end(), t0,
        [](const Block& block, int64_t time) { return block.last_time < time; });
    for (; it != index_.end() && it->first_time < t1; ++it) {
        const auto block_index = static_cast<std::size_t>(it - index_.begin());
        const std::vector<DVSEvent>* events = nullptr;
        if (!loadBlock(block_index, events)) { chunk.data.clear(); return chunk; }
        for (const auto& event : *events) {
            if (event.t >= t0 && event.t < t1) chunk.data.push_back(event);
        }
    }
    return chunk;
}

} // namespace mustard
