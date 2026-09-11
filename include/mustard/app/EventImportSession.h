#pragma once

#include "mustard/data/events/EventImport.h"
#include "mustard/data/events/DVSEventStream.h"
#include "mustard/data/events/TabularEventLoader.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mustard {

enum class ImportSourceKind { Hdf5, Csv, IITDatalog, PropheseeRaw, Video, Images };

struct ImportCandidate {
    std::string path;
    std::string label;
    ImportSourceKind kind{ImportSourceKind::Csv};
};

struct StagedEventImport {
    ImportCandidate source;
    std::string dataset;
    std::unique_ptr<TabularEventLoader> loader;
    std::shared_ptr<DVSEventStream> stream;
};

struct ImportTimeRange {
    bool empty{true};
    int64_t start{0};
    int64_t end{0};
    void include(int64_t first, int64_t last);
};

/// File access is isolated so import transitions can be tested without files or GL.
class EventImportBackend {
public:
    using Progress = DataLoader<DVSEvent>::ProgressCallback;
    virtual ~EventImportBackend() = default;
    virtual bool discover(const std::string& path, std::vector<ImportCandidate>& files,
                          ImportDiagnostic& error, ImportCancellation cancel, Progress progress) = 0;
    virtual bool browse(const std::string& path, std::vector<Hdf5Entry>& entries,
                        ImportDiagnostic& error, ImportCancellation cancel) = 0;
    virtual bool preview(const std::string& path, const EventImportOptions& options,
                         EventTablePreview& preview, ImportDiagnostic& error,
                         ImportCancellation cancel) = 0;
    virtual bool index(const ImportCandidate& file, const std::vector<EventImportOptions>& options,
                       std::vector<StagedEventImport>& streams, std::size_t& failed_selection,
                       ImportDiagnostic& error, ImportCancellation cancel, Progress progress) = 0;
};

/// A staged batch driven on the UI thread, serviced by one background worker.
/// Existing viewers are untouched until the caller takes a Ready batch.
class EventImportSession {
public:
    enum class Phase { Idle, Discovering, Inspecting, Browsing, Previewing, Configuring,
                       Indexing, FileError, Ready, Empty };
    struct Configuration {
        EventImportOptions options;
        EventTablePreview preview;
        bool preview_ready{false};
    };

    explicit EventImportSession(std::shared_ptr<EventImportBackend> backend = {});
    ~EventImportSession();
    EventImportSession(const EventImportSession&) = delete;
    EventImportSession& operator=(const EventImportSession&) = delete;

    void begin(const std::string& path);
    void poll();
    void cancel();
    void skipFile();
    void retry();
    void selectDatasets(const std::vector<std::string>& paths);
    void selectConfiguration(std::size_t index);
    void refreshPreview();
    void back();
    void importFile();
    std::vector<StagedEventImport> takeStaged();

    Phase phase() const noexcept { return phase_; }
    bool busy() const noexcept;
    bool active() const noexcept { return phase_ != Phase::Idle; }
    bool canGoBack() const noexcept;
    const std::string& rootPath() const noexcept { return root_path_; }
    const ImportCandidate* currentFile() const noexcept;
    std::size_t fileIndex() const noexcept { return file_index_; }
    std::size_t fileCount() const noexcept { return files_.size(); }
    std::size_t stagedCount() const noexcept { return staged_.size(); }
    const std::vector<Hdf5Entry>& entries() const noexcept { return entries_; }
    const std::vector<Configuration>& configurations() const noexcept { return configurations_; }
    std::size_t configurationIndex() const noexcept { return configuration_index_; }
    Configuration* configuration() noexcept;
    const ImportDiagnostic& error() const noexcept { return error_; }
    float progress() const;
    std::string progressLabel() const;
    static std::optional<ImportCandidate> fileCandidate(const std::string& path, bool allow_text);

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    void advanceFile();
    void inspectFile();
    void invalidateJob();
    uint64_t generation_{0};
    Phase phase_{Phase::Idle};
    std::string root_path_;
    std::vector<ImportCandidate> files_;
    std::size_t file_index_{0};
    std::vector<Hdf5Entry> entries_;
    std::vector<Configuration> configurations_;
    std::size_t configuration_index_{0};
    std::vector<StagedEventImport> staged_;
    ImportDiagnostic error_;
};

} // namespace mustard
