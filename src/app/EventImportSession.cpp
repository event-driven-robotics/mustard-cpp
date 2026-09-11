#include "mustard/app/EventImportSession.h"
#include "mustard/data/events/IITDatalogStream.h"
#include "mustard/data/events/PropheseeRawStream.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace mustard {
namespace {
std::string lowerExtension(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

class FileImportBackend final : public EventImportBackend {
public:
    bool discover(const std::string& path, std::vector<ImportCandidate>& files,
                  ImportDiagnostic& error, ImportCancellation cancel, Progress progress) override {
        namespace fs = std::filesystem;
        error.path = path;
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) {
            error.message = "Cannot access the selected file or folder";
            return false;
        }
        if (!fs::is_directory(path, ec)) {
            auto file = EventImportSession::fileCandidate(path, true);
            if (!file) {
                error.message = "Unsupported file type. Select HDF5, CSV, TSV, TXT, LOG, RAW, or video.";
                return false;
            }
            files.push_back(*file);
            return true;
        }
        std::map<std::string, std::size_t> image_counts;
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        while (it != end) {
            if (importCancelled(cancel)) return false;
            const auto entry = *it;
            if (entry.is_regular_file(ec) && !ec) {
                const auto fp = entry.path().string();
                if (auto file = EventImportSession::fileCandidate(fp, false)) files.push_back(*file);
                const auto ext = lowerExtension(entry.path());
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                    ++image_counts[entry.path().parent_path().string()];
            }
            ec.clear();
            it.increment(ec);
            if (ec) ec.clear();
        }
        for (const auto& count : image_counts) {
            if (count.second > 50) files.push_back({count.first, fs::path(count.first).filename().string(),
                                                  ImportSourceKind::Images});
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        progress(1.f, "Folder scan complete");
        return !importCancelled(cancel);
    }

    bool browse(const std::string& path, std::vector<Hdf5Entry>& entries,
                ImportDiagnostic& error, ImportCancellation cancel) override {
        return browseHdf5(path, entries, error, std::move(cancel));
    }
    bool preview(const std::string& path, const EventImportOptions& options,
                 EventTablePreview& preview, ImportDiagnostic& error,
                 ImportCancellation cancel) override {
        return previewEventSource(path, options, preview, error, std::move(cancel));
    }
    bool index(const ImportCandidate& file, const std::vector<EventImportOptions>& options,
               std::vector<StagedEventImport>& streams, std::size_t& failed_selection,
               ImportDiagnostic& error, ImportCancellation cancel, Progress progress) override {
        error.path = file.path;
        if (file.kind == ImportSourceKind::Hdf5 || file.kind == ImportSourceKind::Csv) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                failed_selection = i;
                if (importCancelled(cancel)) return false;
                auto loader = std::make_unique<TabularEventLoader>();
                loader->setProgressCallback([progress, i, count = options.size()](float p, const std::string& stage) {
                    progress((static_cast<float>(i) + p) / static_cast<float>(count), stage);
                });
                if (!loader->open(file.path, options[i], cancel)) {
                    error = loader->lastError();
                    return false;
                }
                loader->setProgressCallback({});
                StagedEventImport imported;
                imported.source = file;
                imported.dataset = options[i].dataset;
                imported.loader = std::move(loader);
                streams.push_back(std::move(imported));
            }
            return !importCancelled(cancel);
        }
        std::shared_ptr<DVSEventStream> stream;
        bool opened = false;
        if (file.kind == ImportSourceKind::IITDatalog) {
            auto events = std::make_shared<IITDatalogStream>();
            opened = events->open(file.path, progress);
            stream = std::move(events);
        } else if (file.kind == ImportSourceKind::PropheseeRaw) {
            auto events = std::make_shared<PropheseeRawStream>();
            opened = events->open(file.path, progress);
            stream = std::move(events);
        }
        if (importCancelled(cancel)) return false;
        if (!opened || !stream || stream->sensorWidth() <= 0 || stream->sensorHeight() <= 0 ||
            stream->startTime() > stream->endTime()) {
            error.message = "Could not load an event stream from this file";
            return false;
        }
        StagedEventImport imported;
        imported.source = file;
        imported.stream = std::move(stream);
        streams.push_back(std::move(imported));
        return true;
    }
};
} // namespace

void ImportTimeRange::include(int64_t first, int64_t last) {
    if (first > last) return;
    if (empty) { start = first; end = last; empty = false; }
    else { start = std::min(start, first); end = std::max(end, last); }
}

struct EventImportSession::Worker {
    enum class Kind { Discover, Browse, Preview, Index };
    struct Job {
        Kind kind;
        uint64_t generation;
        ImportCandidate file;
        std::vector<EventImportOptions> options;
        std::size_t configuration{0};
        ImportCancellation cancel;
    };
    struct Result {
        Kind kind;
        uint64_t generation;
        std::size_t configuration{0};
        bool ok{false};
        ImportDiagnostic error;
        std::vector<ImportCandidate> files;
        std::vector<Hdf5Entry> entries;
        EventTablePreview preview;
        std::vector<StagedEventImport> streams;
    };

    explicit Worker(std::shared_ptr<EventImportBackend> backend)
        : backend(std::move(backend)), thread([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (cancellation) cancellation->store(true);
            pending.reset();
        }
        changed.notify_one();
        thread.join();
    }
    void invalidate(uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex);
        if (cancellation) cancellation->store(true);
        requested_generation = generation;
        pending.reset();
        result.reset();
        progress = 0.f;
        label.clear();
    }
    void submit(Job job) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancellation) cancellation->store(true);
            cancellation = std::make_shared<std::atomic_bool>(false);
            job.cancel = cancellation;
            requested_generation = job.generation;
            pending = std::make_unique<Job>(std::move(job));
            result.reset();
            progress = 0.f;
            switch (pending->kind) {
                case Kind::Discover: label = "Finding supported files"; break;
                case Kind::Browse: label = "Reading HDF5 hierarchy"; break;
                case Kind::Preview: label = "Reading sample rows"; break;
                case Kind::Index: label = "Validating and indexing events"; break;
            }
        }
        changed.notify_one();
    }
    std::unique_ptr<Result> take(uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex);
        if (result && result->generation == generation) return std::move(result);
        return {};
    }
    void run() {
        for (;;) {
            std::unique_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || pending; });
                if (stopping) return;
                job = std::move(pending);
            }
            auto completed = std::make_unique<Result>();
            completed->kind = job->kind;
            completed->generation = job->generation;
            completed->configuration = job->configuration;
            completed->error.path = job->file.path;
            auto report = [this, generation = job->generation](float p, const std::string& stage) {
                std::lock_guard<std::mutex> lock(mutex);
                if (generation == requested_generation) {
                    progress = std::clamp(p, 0.f, 1.f);
                    label = stage;
                }
            };
            try {
                switch (job->kind) {
                    case Kind::Discover:
                        completed->ok = backend->discover(job->file.path, completed->files,
                            completed->error, job->cancel, report); break;
                    case Kind::Browse:
                        completed->ok = backend->browse(job->file.path, completed->entries,
                            completed->error, job->cancel); break;
                    case Kind::Preview:
                        completed->ok = backend->preview(job->file.path, job->options.front(),
                            completed->preview, completed->error, job->cancel); break;
                    case Kind::Index:
                        completed->ok = backend->index(job->file, job->options, completed->streams,
                            completed->configuration, completed->error, job->cancel, report); break;
                }
            } catch (const std::exception& ex) {
                completed->error.message = ex.what();
            } catch (...) {
                completed->error.message = "Unexpected error while importing this source";
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!stopping && job->generation == requested_generation && !importCancelled(job->cancel))
                    result = std::move(completed);
            }
        }
    }
    std::shared_ptr<EventImportBackend> backend;
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool stopping{false};
    uint64_t requested_generation{0};
    ImportCancellation cancellation;
    std::unique_ptr<Job> pending;
    std::unique_ptr<Result> result;
    float progress{0.f};
    std::string label;
    std::thread thread;
};

EventImportSession::EventImportSession(std::shared_ptr<EventImportBackend> backend)
    : worker_(std::make_unique<Worker>(backend ? std::move(backend) : std::make_shared<FileImportBackend>())) {}
EventImportSession::~EventImportSession() = default;

std::optional<ImportCandidate> EventImportSession::fileCandidate(const std::string& path, bool allow_text) {
    namespace fs = std::filesystem;
    const auto ext = lowerExtension(fs::path(path));
    ImportSourceKind kind;
    if (ext == ".h5" || ext == ".hdf5") kind = ImportSourceKind::Hdf5;
    else if (ext == ".csv" || ext == ".tsv" || (allow_text && ext == ".txt")) kind = ImportSourceKind::Csv;
    else if (ext == ".log") kind = ImportSourceKind::IITDatalog;
    else if (ext == ".raw") kind = ImportSourceKind::PropheseeRaw;
    else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov") kind = ImportSourceKind::Video;
    else return std::nullopt;
    return ImportCandidate{path, fs::path(path).filename().string(), kind};
}
void EventImportSession::invalidateJob() { worker_->invalidate(++generation_); }
void EventImportSession::begin(const std::string& path) {
    cancel();
    root_path_ = path;
    phase_ = Phase::Discovering;
    Worker::Job job{Worker::Kind::Discover, ++generation_, {path, {}, ImportSourceKind::Csv}, {}};
    worker_->submit(std::move(job));
}
void EventImportSession::cancel() {
    invalidateJob();
    phase_ = Phase::Idle;
    files_.clear();
    entries_.clear();
    configurations_.clear();
    staged_.clear();
    file_index_ = configuration_index_ = 0;
    error_ = {};
}
bool EventImportSession::busy() const noexcept {
    return phase_ == Phase::Discovering || phase_ == Phase::Inspecting ||
           phase_ == Phase::Previewing || phase_ == Phase::Indexing;
}
const ImportCandidate* EventImportSession::currentFile() const noexcept {
    return file_index_ < files_.size() ? &files_[file_index_] : nullptr;
}
EventImportSession::Configuration* EventImportSession::configuration() noexcept {
    return configuration_index_ < configurations_.size() ? &configurations_[configuration_index_] : nullptr;
}
float EventImportSession::progress() const {
    std::lock_guard<std::mutex> lock(worker_->mutex);
    return worker_->progress;
}
std::string EventImportSession::progressLabel() const {
    std::lock_guard<std::mutex> lock(worker_->mutex);
    return worker_->label;
}

void EventImportSession::poll() {
    auto result = worker_->take(generation_);
    if (!result) return;
    error_ = std::move(result->error);
    if (!result->ok) {
        if (error_.message.empty()) error_.message = "Unable to read this source";
        if (result->kind == Worker::Kind::Preview ||
            (result->kind == Worker::Kind::Index && !configurations_.empty())) {
            phase_ = Phase::Configuring;
            configuration_index_ = std::min(result->configuration, configurations_.size() - 1);
        } else phase_ = Phase::FileError;
        return;
    }
    error_ = {};
    switch (result->kind) {
        case Worker::Kind::Discover:
            files_ = std::move(result->files);
            file_index_ = 0;
            inspectFile();
            break;
        case Worker::Kind::Browse:
            entries_ = std::move(result->entries);
            phase_ = Phase::Browsing;
            break;
        case Worker::Kind::Preview: {
            auto& config = configurations_[result->configuration];
            config.preview = std::move(result->preview);
            config.preview_ready = true;
            for (std::size_t i = 0; i < config.options.columns.size(); ++i)
                if (config.options.columns[i] < 0) config.options.columns[i] = config.preview.suggested_columns[i];
            phase_ = Phase::Configuring;
            break;
        }
        case Worker::Kind::Index:
            for (auto& stream : result->streams) staged_.push_back(std::move(stream));
            advanceFile();
            break;
    }
}
void EventImportSession::inspectFile() {
    entries_.clear();
    configurations_.clear();
    configuration_index_ = 0;
    error_ = {};
    // Videos/images own GL resources and are constructed by App at commit time.
    while (const auto* file = currentFile()) {
        if (file->kind != ImportSourceKind::Video && file->kind != ImportSourceKind::Images) break;
        StagedEventImport imported;
        imported.source = *file;
        staged_.push_back(std::move(imported));
        ++file_index_;
    }
    const auto* file = currentFile();
    if (!file) { phase_ = staged_.empty() ? Phase::Empty : Phase::Ready; return; }
    if (file->kind == ImportSourceKind::Hdf5) {
        phase_ = Phase::Inspecting;
        worker_->submit({Worker::Kind::Browse, ++generation_, *file, {}});
    } else if (file->kind == ImportSourceKind::Csv) {
        Configuration config;
        config.options.format = EventSourceFormat::Csv;
        if (lowerExtension(std::filesystem::path(file->path)) == ".tsv") config.options.delimiter = CsvDelimiter::Tab;
        configurations_.push_back(std::move(config));
        refreshPreview();
    } else {
        phase_ = Phase::Indexing;
        worker_->submit({Worker::Kind::Index, ++generation_, *file, {}});
    }
}
void EventImportSession::advanceFile() { ++file_index_; inspectFile(); }
void EventImportSession::skipFile() {
    if (!active()) return;
    invalidateJob();
    advanceFile();
}
void EventImportSession::retry() {
    if (!currentFile()) { const auto path = root_path_; begin(path); }
    else if (phase_ == Phase::Configuring) refreshPreview();
    else inspectFile();
}
void EventImportSession::selectDatasets(const std::vector<std::string>& paths) {
    if (phase_ != Phase::Browsing || !currentFile()) return;
    configurations_.clear();
    for (const auto& path : paths) {
        if (std::any_of(configurations_.begin(), configurations_.end(), [&](const auto& c) {
            return c.options.dataset == path;
        })) continue;
        const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) {
            return e.path == path && e.selectable;
        });
        if (entry == entries_.end()) continue;
        Configuration config;
        config.options.format = EventSourceFormat::Hdf5;
        config.options.dataset = path;
        configurations_.push_back(std::move(config));
    }
    if (configurations_.empty()) return;
    configuration_index_ = 0;
    refreshPreview();
}
void EventImportSession::selectConfiguration(std::size_t index) {
    if (index >= configurations_.size()) return;
    invalidateJob();
    configuration_index_ = index;
    error_ = {};
    if (configurations_[index].preview_ready) phase_ = Phase::Configuring;
    else refreshPreview();
}
void EventImportSession::refreshPreview() {
    if (!configuration() || !currentFile()) return;
    configuration()->preview_ready = false;
    error_ = {};
    phase_ = Phase::Previewing;
    worker_->submit({Worker::Kind::Preview, ++generation_, *currentFile(),
                     {configuration()->options}, configuration_index_});
}
bool EventImportSession::canGoBack() const noexcept {
    return !configurations_.empty() && currentFile() &&
        (configuration_index_ > 0 || currentFile()->kind == ImportSourceKind::Hdf5);
}
void EventImportSession::back() {
    if (!canGoBack()) return;
    invalidateJob();
    error_ = {};
    if (configuration_index_ > 0) selectConfiguration(configuration_index_ - 1);
    else phase_ = Phase::Browsing;
}
void EventImportSession::importFile() {
    if (!currentFile() || configurations_.empty() || phase_ != Phase::Configuring) return;
    std::vector<EventImportOptions> options;
    for (const auto& config : configurations_) {
        if (!config.preview_ready) return;
        options.push_back(config.options);
    }
    error_ = {};
    phase_ = Phase::Indexing;
    worker_->submit({Worker::Kind::Index, ++generation_, *currentFile(), std::move(options)});
}
std::vector<StagedEventImport> EventImportSession::takeStaged() {
    if (phase_ != Phase::Ready) return {};
    auto result = std::move(staged_);
    phase_ = Phase::Idle;
    return result;
}

} // namespace mustard
