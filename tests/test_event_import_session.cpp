#include "mustard/app/EventImportSession.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <thread>

using namespace mustard;

namespace {

void waitForWorker(EventImportSession& session) {
    for (int i = 0; i < 100 && session.busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        session.poll();
    }
}

class MockImportBackend : public EventImportBackend {
public:
    bool discover(const std::string& path, std::vector<ImportCandidate>& files,
                  ImportDiagnostic& error, ImportCancellation cancel, Progress progress) override {
        if (importCancelled(cancel)) return false;
        files.push_back({path, "test.csv", ImportSourceKind::Csv});
        if (progress) progress(1.0f, "Done");
        return true;
    }

    bool browse(const std::string& path, std::vector<Hdf5Entry>& entries,
                ImportDiagnostic& error, ImportCancellation cancel) override {
        if (importCancelled(cancel)) return false;
        entries.push_back({"/dataset1", false, {100, 4}, "uint32", true, ""});
        return true;
    }

    bool preview(const std::string& path, const EventImportOptions& options,
                 EventTablePreview& preview, ImportDiagnostic& error,
                 ImportCancellation cancel) override {
        if (importCancelled(cancel)) return false;
        preview.columns = {"x", "y", "ts", "p"};
        preview.rows = {{{10}, {20}, {100}, {1}}};
        preview.suggested_columns = {0, 1, 2, 3};
        return true;
    }

    bool index(const ImportCandidate& file, const std::vector<EventImportOptions>& options,
               std::vector<StagedEventImport>& streams, std::size_t& failed_selection,
               ImportDiagnostic& error, ImportCancellation cancel, Progress progress) override {
        if (importCancelled(cancel)) return false;
        StagedEventImport staged;
        staged.source = file;
        streams.push_back(std::move(staged));
        if (progress) progress(1.0f, "Ready");
        return true;
    }
};

} // namespace

TEST(EventImportSession, AdvancesThroughCsvImportLifecycle) {
    auto backend = std::make_shared<MockImportBackend>();
    EventImportSession session(backend);

    session.begin("dummy.csv");
    EXPECT_TRUE(session.active());

    waitForWorker(session);
    EXPECT_EQ(session.phase(), EventImportSession::Phase::Configuring);

    auto* config = session.configuration();
    ASSERT_NE(config, nullptr);
    EXPECT_TRUE(config->preview_ready);
    EXPECT_EQ(config->preview.columns.size(), 4u);

    session.importFile();
    waitForWorker(session);

    EXPECT_EQ(session.phase(), EventImportSession::Phase::Ready);
    auto staged = session.takeStaged();
    EXPECT_EQ(staged.size(), 1u);
    EXPECT_EQ(session.phase(), EventImportSession::Phase::Idle);
}

TEST(EventImportSession, CancelsActiveImport) {
    auto backend = std::make_shared<MockImportBackend>();
    EventImportSession session(backend);

    session.begin("dummy.csv");
    session.cancel();

    EXPECT_FALSE(session.active());
    EXPECT_EQ(session.phase(), EventImportSession::Phase::Idle);
}

TEST(EventImportSession, TimeRangeIncludesMultipleStreams) {
    ImportTimeRange range;
    EXPECT_TRUE(range.empty);

    range.include(100, 500);
    EXPECT_FALSE(range.empty);
    EXPECT_EQ(range.start, 100);
    EXPECT_EQ(range.end, 500);

    range.include(50, 600);
    EXPECT_EQ(range.start, 50);
    EXPECT_EQ(range.end, 600);
}