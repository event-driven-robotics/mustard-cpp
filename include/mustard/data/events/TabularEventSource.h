#pragma once
#include "mustard/data/events/EventImport.h"
#include "mustard/data/DVSEvent.h"

#include <cstddef>
#include <istream>
#include <memory>

namespace mustard {

/// Seekable raw table. Checkpoints are opaque byte/row positions, not timestamps.
/// readRows returns true with an empty result at EOF, false on error/cancellation.
class TabularEventSource {
public:
    virtual ~TabularEventSource() = default;
    virtual const std::vector<std::string>& columns() const = 0;
    virtual uint64_t checkpoint() const = 0;
    virtual bool seek(uint64_t checkpoint, ImportDiagnostic& error) = 0;
    virtual bool readRows(std::size_t max_rows, std::vector<TableRow>& rows,
                          ImportDiagnostic& error) = 0;
    virtual float progress() const = 0;
    /// Numeric sources may bypass generic cells for the configured event fields.
    virtual bool readsEventsDirectly() const noexcept { return false; }
    virtual bool readEvents(std::size_t, std::vector<DVSEvent>&,
                            ImportDiagnostic&) { return false; }
    /// Detach import cancellation after successful indexing, before playback.
    virtual void finishImport() {}
};

std::unique_ptr<TabularEventSource> createCsvSource(
    const std::string& path, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel = {});
std::unique_ptr<TabularEventSource> createCsvSource(
    std::unique_ptr<std::istream> input, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel = {});
std::unique_ptr<TabularEventSource> createHdf5Source(
    const std::string& path, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel = {});
// File-image entry points also permit fixtures without filesystem I/O.
std::unique_ptr<TabularEventSource> createHdf5SourceFromImage(
    const std::vector<uint8_t>& image, const EventImportOptions& options,
    ImportDiagnostic& error, ImportCancellation cancel = {});
bool browseHdf5Image(const std::vector<uint8_t>& image, std::vector<Hdf5Entry>& entries,
                     ImportDiagnostic& error, ImportCancellation cancel = {});

} // namespace mustard
