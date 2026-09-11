#include "mustard/data/events/TabularEventSource.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>

using namespace mustard;

namespace {

std::unique_ptr<TabularEventSource> source(const std::string& csv,
    ImportDiagnostic& error, EventImportOptions options = {}, ImportCancellation cancel = {}) {
    return createCsvSource(std::make_unique<std::istringstream>(csv), options, error, std::move(cancel));
}

std::string cell(const TableRow& row, std::size_t index) {
    return std::get<std::string>(row.at(index));
}

struct ReadCounter {
    std::size_t bytes{0};
    std::size_t cancel_at{0};
    ImportCancellation cancel;
};

// A seekable stream without a filebuf, as used by the HDF5 text-record adapter.
class CountingBuffer final : public std::streambuf {
public:
    CountingBuffer(std::string text, std::shared_ptr<ReadCounter> counter)
        : text_(std::move(text)), counter_(std::move(counter)) {}
protected:
    int_type underflow() override {
        return position_ == text_.size() ? traits_type::eof() : traits_type::to_int_type(text_[position_]);
    }
    int_type uflow() override {
        const auto result = underflow();
        if (result != traits_type::eof()) {
            ++position_;
            ++counter_->bytes;
            if (counter_->cancel && counter_->cancel_at && counter_->bytes >= counter_->cancel_at) {
                counter_->cancel->store(true);
            }
        }
        return result;
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                     std::ios_base::openmode) override {
        const auto base = direction == std::ios::beg ? 0 :
            direction == std::ios::end ? text_.size() : position_;
        const auto position = static_cast<off_type>(base) + offset;
        if (position < 0 || static_cast<std::size_t>(position) > text_.size()) return pos_type(off_type(-1));
        position_ = static_cast<std::size_t>(position);
        return pos_type(position);
    }
    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
        return seekoff(static_cast<off_type>(position), std::ios::beg, mode);
    }
private:
    std::string text_;
    std::shared_ptr<ReadCounter> counter_;
    std::size_t position_{0};
};

class CountingStream final : public std::istream {
public:
    CountingStream(std::string text, std::shared_ptr<ReadCounter> counter)
        : std::istream(nullptr), buffer_(std::move(text), std::move(counter)) {
        rdbuf(&buffer_);
    }
private:
    CountingBuffer buffer_;
};

} // namespace

TEST(CsvEventSource, DetectsReorderedNamedColumnsWithoutConvertingValues) {
    ImportDiagnostic error;
    auto input = source("Timestamp,POLARITY,Y,X,metadata\n9007199254740993,-1,2,1,ignored\n", error);
    ASSERT_NE(input, nullptr) << error.message;
    EXPECT_EQ(input->columns(), (std::vector<std::string>{"Timestamp", "POLARITY", "Y", "X", "metadata"}));
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 0), "9007199254740993");
    EXPECT_EQ(cell(rows[0], 1), "-1");
    EXPECT_EQ(cell(rows[0], 4), "ignored");
}

TEST(CsvEventSource, DetectsEachDelimiterAndHeaderlessColumns) {
    for (const char* csv : {"1,2,30,1\n", "1\t2\t30\t1\n", "1;2;30;1\n", "  1  2   30  1 \n"}) {
        SCOPED_TRACE(csv);
        ImportDiagnostic error;
        auto input = source(csv, error);
        ASSERT_NE(input, nullptr) << error.message;
        EXPECT_EQ(input->columns(), (std::vector<std::string>{"Column 1", "Column 2", "Column 3", "Column 4"}));
        std::vector<TableRow> rows;
        ASSERT_TRUE(input->readRows(1, rows, error));
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(cell(rows[0], 0), "1");
        EXPECT_EQ(cell(rows[0], 2), "30");
    }
}

TEST(CsvEventSource, HonorsDelimiterAndHeaderOverrides) {
    ImportDiagnostic error;
    EventImportOptions options;
    options.delimiter = CsvDelimiter::Semicolon;
    options.header = CsvHeader::Present;
    auto input = source("0;1;2;3\n1;2;30;1\n", error, options);
    ASSERT_NE(input, nullptr) << error.message;
    EXPECT_EQ(input->columns(), (std::vector<std::string>{"0", "1", "2", "3"}));
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(10, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 2), "30");

    options.header = CsvHeader::Absent;
    input = source("x;y;ts;p\n1;2;30;1", error, options);
    ASSERT_NE(input, nullptr);
    ASSERT_TRUE(input->readRows(10, rows, error));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(cell(rows[0], 0), "x");

    options.delimiter = CsvDelimiter::Tab;
    input = source("1,2\t3;4\t5\ttrue", error, options);
    ASSERT_NE(input, nullptr);
    ASSERT_TRUE(input->readRows(1, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 0), "1,2");
    EXPECT_EQ(cell(rows[0], 1), "3;4");
}

TEST(CsvEventSource, ParsesBomCommentsCrLfQuotedFieldsAndMultilineRecords) {
    ImportDiagnostic error;
    auto input = source("\xef\xbb\xbf" "# unmatched comment quote \"\r\n\r\n"
                        " // another comment \"\n"
                        " x , y , ts , p , note \r\n"
                        "\"1\", 2,3,true,\"first\r\nsecond \"\"quoted\"\", ;\"\r\n"
                        "# interspersed comment\n"
                        "4,5,6,false,\"\"\n", error);
    ASSERT_NE(input, nullptr) << error.message;
    EXPECT_EQ(input->columns(), (std::vector<std::string>{"x", "y", "ts", "p", "note"}));
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(cell(rows[0], 0), "1");
    EXPECT_EQ(cell(rows[0], 1), "2");
    EXPECT_EQ(cell(rows[0], 4), "first\r\nsecond \"quoted\", ;");
    EXPECT_EQ(cell(rows[1], 4), "");
}

TEST(CsvEventSource, WhitespaceDelimiterHandlesQuotedFieldsAndRepeatedSpacing) {
    ImportDiagnostic error;
    EventImportOptions options;
    options.delimiter = CsvDelimiter::Whitespace;
    auto input = source("x y ts p note\n 1\t  2  3  true  \"hello world\"   \n", error, options);
    ASSERT_NE(input, nullptr) << error.message;
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(10, rows, error)) << error.message;
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 4), "hello world");
}

TEST(CsvEventSource, DoesNotTreatMalformedFirstNumericRecordAsHeader) {
    ImportDiagnostic error;
    auto input = source("broken,2,3,true\n1,2,4,false\n", error);
    ASSERT_NE(input, nullptr) << error.message;
    EXPECT_EQ(input->columns()[0], "Column 1");
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(1, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 0), "broken");
}

TEST(CsvEventSource, PreservesEmptyFieldsForEventValidation) {
    ImportDiagnostic error;
    auto input = source("x,y,ts,p\n1,,3,\n", error);
    ASSERT_NE(input, nullptr);
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(1, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].size(), 4u);
    EXPECT_EQ(cell(rows[0], 1), "");
    EXPECT_EQ(cell(rows[0], 3), "");
}

TEST(CsvEventSource, RejectsMalformedRecordsWithDataRowAndField) {
    for (const auto& malformed : {"1,2,3\n", "1,2,3,1,extra\n", "1,\"2\"junk,3,1\n", "1,a\"b\",3,1\n"}) {
        SCOPED_TRACE(malformed);
        ImportDiagnostic error;
        EventImportOptions options;
        options.dataset = "/events/text";
        auto input = source(std::string("x,y,ts,p\n0,0,0,0\n# ignored\n") + malformed, error, options);
        ASSERT_NE(input, nullptr) << error.message;
        std::vector<TableRow> rows;
        EXPECT_FALSE(input->readRows(100, rows, error));
        EXPECT_TRUE(rows.empty());
        EXPECT_EQ(error.path, "<stream>");
        EXPECT_EQ(error.dataset, "/events/text");
        EXPECT_EQ(error.row, 2u);
        EXPECT_FALSE(error.field.empty());
        EXPECT_FALSE(error.message.empty());
        EXPECT_FALSE(error.cancelled);
    }
}

TEST(CsvEventSource, RejectsUnterminatedQuotesEmptyAndUnreadableStreams) {
    ImportDiagnostic error;
    auto input = source("x,y,ts,p\n1,\"2,3,1\n", error);
    ASSERT_NE(input, nullptr);
    std::vector<TableRow> rows;
    EXPECT_FALSE(input->readRows(10, rows, error));
    EXPECT_EQ(error.row, 1u);
    EXPECT_NE(error.message.find("Unterminated"), std::string::npos);

    EXPECT_EQ(source("# just comments\r\n \r\n", error), nullptr);
    EXPECT_EQ(error.row, 0u);
    EXPECT_EQ(createCsvSource(std::unique_ptr<std::istream>{}, {}, error), nullptr);
    EXPECT_EQ(createCsvSource(std::make_unique<std::istream>(nullptr), {}, error), nullptr);
}

TEST(CsvEventSource, SeeksByteCheckpointsBackwardsAndRestoresDiagnosticRow) {
    ImportDiagnostic error;
    auto input = source("x,y,ts,p\n1,2,3,1\n# between\n4,5,6,0\n7,8,9\n", error);
    ASSERT_NE(input, nullptr);
    const auto first = input->checkpoint();
    EXPECT_GT(first, 0u);
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(1, rows, error));
    const auto second = input->checkpoint();
    ASSERT_GT(second, first);
    ASSERT_TRUE(input->readRows(1, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(cell(rows[0], 0), "4");
    ASSERT_TRUE(input->seek(first, error));
    ASSERT_TRUE(input->readRows(1, rows, error));
    EXPECT_EQ(cell(rows[0], 0), "1");
    ASSERT_TRUE(input->seek(second, error));
    ASSERT_TRUE(input->readRows(1, rows, error));
    EXPECT_FALSE(input->readRows(1, rows, error));
    EXPECT_EQ(error.row, 3u);
    EXPECT_FALSE(input->seek(second + 1, error));
    EXPECT_EQ(error.row, 0u);
}

TEST(CsvEventSource, ReportsEofRepeatedlyAndSeeksAfterEof) {
    ImportDiagnostic error;
    auto input = source("1,2,3,1", error);
    ASSERT_NE(input, nullptr);
    const auto first = input->checkpoint();
    std::vector<TableRow> rows;
    EXPECT_FLOAT_EQ(input->progress(), 0.0f);
    ASSERT_TRUE(input->readRows(0, rows, error));
    EXPECT_TRUE(rows.empty());
    EXPECT_EQ(input->checkpoint(), first);
    ASSERT_TRUE(input->readRows(10, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FLOAT_EQ(input->progress(), 1.0f);
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(input->readRows(10, rows, error));
        EXPECT_TRUE(rows.empty());
    }
    ASSERT_TRUE(input->seek(first, error));
    ASSERT_TRUE(input->readRows(10, rows, error));
    EXPECT_EQ(rows.size(), 1u);
}

TEST(CsvEventSource, StartupAndRequestedReadsAreBounded) {
    ImportDiagnostic error;
    auto counter = std::make_shared<ReadCounter>();
    std::string csv = "x,y,ts,p\n";
    for (int i = 0; i < 70'000; ++i) csv += "1,2,3,1\n";
    auto input = createCsvSource(std::make_unique<CountingStream>(csv, counter), {}, error);
    ASSERT_NE(input, nullptr) << error.message;
    EXPECT_LT(counter->bytes, 20u);
    std::vector<TableRow> rows;
    ASSERT_TRUE(input->readRows(1, rows, error));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_LT(counter->bytes, 30u);
    ASSERT_TRUE(input->readRows(100'000, rows, error));
    EXPECT_EQ(rows.size(), 65'536u);
    EXPECT_LT(counter->bytes, csv.size());
    ASSERT_TRUE(input->readRows(100'000, rows, error));
    EXPECT_EQ(rows.size(), 4'463u);
}

TEST(CsvEventSource, CancellationStopsStartupReadsAndSeeks) {
    ImportDiagnostic error;
    auto cancel = std::make_shared<std::atomic_bool>(true);
    EXPECT_EQ(source("x,y,ts,p\n1,2,3,1\n", error, {}, cancel), nullptr);
    EXPECT_TRUE(error.cancelled);
    cancel->store(false);
    auto input = source("x,y,ts,p\n1,2,3,1\n", error, {}, cancel);
    ASSERT_NE(input, nullptr);
    const auto first = input->checkpoint();
    cancel->store(true);
    std::vector<TableRow> rows;
    EXPECT_FALSE(input->readRows(10, rows, error));
    EXPECT_TRUE(rows.empty());
    EXPECT_TRUE(error.cancelled);
    EXPECT_FALSE(input->seek(first, error));
    EXPECT_TRUE(error.cancelled);
}

TEST(CsvEventSource, CancellationInterruptsLargeQuotedRecord) {
    ImportDiagnostic error;
    auto counter = std::make_shared<ReadCounter>();
    counter->cancel = std::make_shared<std::atomic_bool>(false);
    counter->cancel_at = 100;
    auto input = createCsvSource(std::make_unique<CountingStream>(
        "x,y,ts,p,note\n1,2,3,1,\"" + std::string(100'000, 'a') + "\"\n", counter),
        {}, error, counter->cancel);
    ASSERT_NE(input, nullptr) << error.message;
    std::vector<TableRow> rows;
    EXPECT_FALSE(input->readRows(10, rows, error));
    EXPECT_TRUE(error.cancelled);
    EXPECT_TRUE(rows.empty());
    EXPECT_LT(counter->bytes, 5'000u);
}
