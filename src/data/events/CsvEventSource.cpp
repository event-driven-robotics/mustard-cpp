#include "mustard/data/events/TabularEventSource.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace mustard {
namespace {

constexpr std::size_t kMaxRows = 65'536;

bool space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::string trimmed(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), space);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), space).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool isComment(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\v\f");
    return first != std::string::npos &&
           (value[first] == '#' || value.compare(first, 2, "//") == 0);
}

CsvDelimiter detectDelimiter(const std::string& record) {
    std::size_t commas = 0, tabs = 0, semicolons = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < record.size(); ++i) {
        const char ch = record[i];
        if (ch == '"') {
            if (quoted && i + 1 < record.size() && record[i + 1] == '"') {
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted) {
            commas += ch == ',';
            tabs += ch == '\t';
            semicolons += ch == ';';
        }
    }
    if (commas && commas >= tabs && commas >= semicolons) return CsvDelimiter::Comma;
    if (tabs && tabs >= semicolons) return CsvDelimiter::Tab;
    if (semicolons) return CsvDelimiter::Semicolon;
    return CsvDelimiter::Whitespace;
}

// A mixed numeric/text record remains data, so a malformed first event is not
// silently discarded as a header. All-label records are editable column names.
bool looksLikeHeader(const std::vector<std::string>& cells) {
    if (cells.empty()) return false;
    return std::all_of(cells.begin(), cells.end(), [](const std::string& cell) {
        const auto value = trimmed(cell);
        if (value.empty()) return false;
        char* end = nullptr;
        std::strtold(value.c_str(), &end);
        return end != value.c_str() + value.size();
    });
}

class CsvEventSource final : public TabularEventSource {
public:
    CsvEventSource(std::unique_ptr<std::istream> input, std::string path,
                   const EventImportOptions& options, ImportCancellation cancel)
        : input_(std::move(input)), path_(std::move(path)), options_(options),
          cancel_(std::move(cancel)) {}

    bool initialize(ImportDiagnostic& error) {
        if (cancelled(error)) return false;
        if (!input_) return fail(error, "No CSV input stream was supplied.", 0);
        input_->clear();
        input_->seekg(0, std::ios::end);
        const auto end = input_->tellg();
        if (end == std::streampos(-1)) {
            return fail(error, "CSV input must be a readable, seekable stream.", 0);
        }
        size_ = static_cast<uint64_t>(end);
        if (!position(0, error)) return false;

        // Inspect at most three bytes for a UTF-8 BOM, then rewind if absent.
        bool bom = true;
        const unsigned char signature[] = {0xef, 0xbb, 0xbf};
        for (const auto byte : signature) {
            const int ch = take();
            if (ch == std::char_traits<char>::eof() ||
                static_cast<unsigned char>(ch) != byte) {
                bom = false;
                break;
            }
        }
        if (!bom && !position(0, error)) return false;

        std::string record;
        uint64_t start = cursor_;
        const auto result = readRecord(record, start, error);
        if (result == RecordResult::Error) return false;
        if (result == RecordResult::End) {
            return fail(error, "CSV input contains no records.", 0);
        }
        delimiter_ = options_.delimiter == CsvDelimiter::Auto
                         ? detectDelimiter(record) : options_.delimiter;
        std::vector<std::string> cells;
        if (!parse(record, cells, error)) return false;
        const bool header = options_.header == CsvHeader::Present ||
            (options_.header == CsvHeader::Auto && looksLikeHeader(cells));
        if (header) {
            columns_.reserve(cells.size());
            for (std::size_t i = 0; i < cells.size(); ++i) {
                auto name = trimmed(cells[i]);
                columns_.push_back(name.empty() ? columnName(i) : std::move(name));
            }
        } else {
            for (std::size_t i = 0; i < cells.size(); ++i) columns_.push_back(columnName(i));
            if (!position(start, error)) return false;
        }
        checkpoints_[cursor_] = 0;
        return true;
    }

    const std::vector<std::string>& columns() const override { return columns_; }

    uint64_t checkpoint() const override {
        checkpoints_[cursor_] = completed_rows_;
        return cursor_;
    }

    bool seek(uint64_t checkpoint, ImportDiagnostic& error) override {
        try {
            if (cancelled(error)) return false;
            const auto found = checkpoints_.find(checkpoint);
            if (found == checkpoints_.end()) {
                return fail(error, "Invalid CSV checkpoint.", 0);
            }
            if (!position(checkpoint, error)) return false;
            completed_rows_ = found->second;
            return true;
        } catch (const std::exception& ex) {
            return fail(error, std::string("Cannot seek CSV input: ") + ex.what(), 0);
        }
    }

    bool readRows(std::size_t max_rows, std::vector<TableRow>& rows,
                  ImportDiagnostic& error) override {
        rows.clear();
        try {
            if (cancelled(error)) return false;
            max_rows = std::min(max_rows, kMaxRows);
            rows.reserve(max_rows);
            while (rows.size() < max_rows) {
                std::string record;
                uint64_t start = cursor_;
                const auto result = readRecord(record, start, error);
                if (result == RecordResult::End) return true;
                if (result == RecordResult::Error) {
                    rows.clear();
                    return false;
                }
                std::vector<std::string> cells;
                if (!parse(record, cells, error)) {
                    rows.clear();
                    return false;
                }
                if (cells.size() != columns_.size()) {
                    rows.clear();
                    const auto index = std::min(cells.size(), columns_.size());
                    const auto field = index < columns_.size() ? columns_[index] : columnName(index);
                    return fail(error, "Expected " + std::to_string(columns_.size()) +
                                " columns but found " + std::to_string(cells.size()) + ".",
                                completed_rows_ + 1, field);
                }
                TableRow row;
                row.reserve(cells.size());
                for (auto& cell : cells) row.emplace_back(std::move(cell));
                rows.push_back(std::move(row));
                ++completed_rows_;
            }
            return true;
        } catch (const std::exception& ex) {
            rows.clear();
            return fail(error, std::string("Cannot read CSV input: ") + ex.what(), completed_rows_ + 1);
        }
    }

    float progress() const override {
        return size_ ? static_cast<float>(std::min(1.0, static_cast<double>(cursor_) / size_)) : 1.0f;
    }

    void finishImport() override { cancel_.reset(); }

private:
    enum class RecordResult { Record, End, Error };

    static std::string columnName(std::size_t index) {
        return "Column " + std::to_string(index + 1);
    }

    bool fail(ImportDiagnostic& error, std::string message, uint64_t row,
              std::string field = {}) const {
        error = {path_, options_.dataset, row, std::move(field), std::move(message), false};
        return false;
    }

    bool cancelled(ImportDiagnostic& error) const {
        if (!importCancelled(cancel_)) return false;
        error = {path_, options_.dataset, completed_rows_ + 1, {}, "Import cancelled.", true};
        return true;
    }

    bool position(uint64_t offset, ImportDiagnostic& error) {
        if (offset > size_ || offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            return fail(error, "CSV position is outside the input.", 0);
        }
        input_->clear();
        input_->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!*input_) return fail(error, "Cannot seek CSV input.", 0);
        cursor_ = offset;
        return true;
    }

    int take() {
        const auto ch = input_->get();
        if (ch != std::char_traits<char>::eof()) ++cursor_;
        return ch;
    }

    RecordResult readRecord(std::string& record, uint64_t& start, ImportDiagnostic& error) {
        for (;;) {
            if (cancelled(error)) return RecordResult::Error;
            record.clear();
            start = cursor_;
            bool quoted = false;
            bool comment = false;
            for (;;) {
                if ((cursor_ & 4095u) == 0 && cancelled(error)) return RecordResult::Error;
                const int value = take();
                if (value == std::char_traits<char>::eof()) {
                    if (input_->bad() || (!input_->eof() && input_->fail())) {
                        fail(error, "I/O failure while reading CSV input.", completed_rows_ + 1);
                        return RecordResult::Error;
                    }
                    if (quoted) {
                        fail(error, "Unterminated quoted field.", completed_rows_ + 1);
                        return RecordResult::Error;
                    }
                    if (record.empty() || comment || trimmed(record).empty()) return RecordResult::End;
                    return RecordResult::Record;
                }
                const char ch = static_cast<char>(value);
                if (!quoted && (ch == '\r' || ch == '\n')) {
                    if (ch == '\r' && input_->peek() == '\n') take();
                    break;
                }
                record.push_back(ch);
                if (!quoted && !comment && (ch == '#' || ch == '/')) comment = isComment(record);
                if (!comment && ch == '"') {
                    if (quoted && input_->peek() == '"') {
                        record.push_back(static_cast<char>(take()));
                    } else {
                        quoted = !quoted;
                    }
                }
            }
            if (!comment && !trimmed(record).empty()) return RecordResult::Record;
        }
    }

    bool parse(const std::string& record, std::vector<std::string>& cells,
               ImportDiagnostic& error) const {
        const bool whitespace = delimiter_ == CsvDelimiter::Whitespace;
        const char delimiter = delimiter_ == CsvDelimiter::Tab ? '\t' :
                               delimiter_ == CsvDelimiter::Semicolon ? ';' : ',';
        const auto separates = [&](char ch) { return whitespace ? space(ch) : ch == delimiter; };
        std::size_t offset = 0;
        for (;;) {
            while (offset < record.size() && space(record[offset]) &&
                   (whitespace || record[offset] != delimiter)) ++offset;
            if (whitespace && offset == record.size()) return true;
            std::string cell;
            if (offset < record.size() && record[offset] == '"') {
                ++offset;
                bool closed = false;
                while (offset < record.size()) {
                    const char ch = record[offset++];
                    if (ch != '"') {
                        cell.push_back(ch);
                    } else if (offset < record.size() && record[offset] == '"') {
                        cell.push_back('"');
                        ++offset;
                    } else {
                        closed = true;
                        break;
                    }
                }
                if (!closed) return fail(error, "Unterminated quoted field.", completed_rows_ + 1, columnName(cells.size()));
                while (!whitespace && offset < record.size() &&
                       space(record[offset]) && !separates(record[offset])) ++offset;
                if (offset < record.size() && !separates(record[offset])) {
                    return fail(error, "Unexpected text after a closing quote.", completed_rows_ + 1, columnName(cells.size()));
                }
            } else {
                const auto first = offset;
                while (offset < record.size() && !separates(record[offset])) {
                    if (record[offset] == '"') {
                        return fail(error, "Unexpected quote in an unquoted field.", completed_rows_ + 1, columnName(cells.size()));
                    }
                    ++offset;
                }
                cell = trimmed(record.substr(first, offset - first));
            }
            cells.push_back(std::move(cell));
            if (offset == record.size()) return true;
            ++offset;
        }
    }

    std::unique_ptr<std::istream> input_;
    std::string path_;
    EventImportOptions options_;
    ImportCancellation cancel_;
    CsvDelimiter delimiter_{CsvDelimiter::Comma};
    std::vector<std::string> columns_;
    uint64_t size_{0};
    uint64_t cursor_{0};
    uint64_t completed_rows_{0};
    mutable std::map<uint64_t, uint64_t> checkpoints_;
};

std::unique_ptr<TabularEventSource> makeSource(std::unique_ptr<std::istream> input,
    const std::string& path, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel) {
    try {
        auto source = std::make_unique<CsvEventSource>(std::move(input), path, options, std::move(cancel));
        if (!source->initialize(error)) return {};
        return source;
    } catch (const std::exception& ex) {
        error = {path, options.dataset, 0, {}, std::string("Cannot open CSV input: ") + ex.what(), false};
        return {};
    }
}

} // namespace

std::unique_ptr<TabularEventSource> createCsvSource(const std::string& path,
    const EventImportOptions& options, ImportDiagnostic& error, ImportCancellation cancel) {
    auto input = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!input->is_open()) {
        error = {path, options.dataset, 0, {}, "Cannot open CSV input.", false};
        return {};
    }
    return makeSource(std::move(input), path, options, error, std::move(cancel));
}

std::unique_ptr<TabularEventSource> createCsvSource(std::unique_ptr<std::istream> input,
    const EventImportOptions& options, ImportDiagnostic& error, ImportCancellation cancel) {
    return makeSource(std::move(input), "<stream>", options, error, std::move(cancel));
}

} // namespace mustard
