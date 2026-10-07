#pragma once

#include <string>
#include <vector>

namespace mustard {

class AnnotationStore;

enum class AnnotationCsvType { kPoints, kEyeTracking, kBoundingBoxes };

/// Parse a typed annotation CSV (type is detected from its exact header) and
/// merge it atomically into @p store. Point records replace points at the same
/// timestamp. width/height <= 0 disable upper-bound coordinate validation.
bool loadAnnotationCsv(const std::string& path, AnnotationStore& store,
                       int width, int height, std::string& error);

/// Write one .csv file per non-empty annotation type. Existing files are only
/// replaced when @p overwrite is true.
bool saveAnnotationCsvFiles(const std::string& directory,
                            const std::string& video_stem,
                            const AnnotationStore& store, bool overwrite,
                            std::vector<std::string>& written,
                            std::string& error);

/// Find annotations for a file-backed source or image-sequence directory.
/// Source-specific names take precedence over folder-level names. Ambiguous
/// types are skipped and described in @p warnings.
std::vector<std::string> discoverAnnotationCsvFiles(
    const std::string& source_path, std::vector<std::string>& warnings);

/// Identify an annotation filename. source_stem is empty for folder-level
/// names and contains the paired source stem for source-specific names.
bool parseAnnotationFilename(const std::string& path, std::string& type_token,
                             std::string& source_stem);

/// Resolve a directly selected annotation companion. Folder-level files map
/// to their parent directory; source-specific files map to a unique sibling
/// data source. Returns false when @p selected_path is not a resolvable
/// annotation companion. An ambiguity is reported through @p error.
bool resolveDirectAnnotationSelection(const std::string& selected_path,
                                      std::string& import_path,
                                      std::string& error);

} // namespace mustard
