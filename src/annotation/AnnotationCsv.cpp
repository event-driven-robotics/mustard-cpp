#include "mustard/annotation/AnnotationCsv.h"

#include "mustard/annotation/AnnotationInterpolation.h"
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "mustard/annotation/PointAnnotation.h"
#include "mustard/annotation/InterpolationEndpoint.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <algorithm>
#include <cctype>

namespace mustard {
namespace {

using Parsed = std::vector<std::unique_ptr<Annotation>>;

bool parseCsvRow(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
        } else if (ch == ',' ) {
            fields.push_back(std::move(field));
            field.clear();
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) return false;
    fields.push_back(std::move(field));
    return true;
}

std::string stripCr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

bool parseInt64(const std::string& s, int64_t& value) {
    try {
        std::size_t used = 0;
        value = std::stoll(s, &used);
        return used == s.size();
    } catch (...) { return false; }
}

bool parseFloat(const std::string& s, float& value) {
    try {
        std::size_t used = 0;
        value = std::stof(s, &used);
        return used == s.size() && std::isfinite(value);
    } catch (...) { return false; }
}

bool pointInBounds(float x, float y, int width, int height) {
    return x >= 0.f && y >= 0.f && (width <= 0 || x < width) &&
           (height <= 0 || y < height);
}

bool identify(const std::string& header, AnnotationCsvType& type,
              std::size_t& columns) {
    if (header == "timestamp,x,y") {
        type = AnnotationCsvType::kPoints; columns = 3; return true;
    }
    if (header == "timestamp,x,y,label") {
        type = AnnotationCsvType::kPoints; columns = 4; return true;
    }
    if (header == "timestamp,phi,theta,center_x,center_y,radius") {
        type = AnnotationCsvType::kEyeTracking; columns = 6; return true;
    }
    if (header == "timestamp,phi,theta,center_x,center_y,radius,label") {
        type = AnnotationCsvType::kEyeTracking; columns = 7; return true;
    }
    if (header == "timestamp,x,y,w,h,label") {
        type = AnnotationCsvType::kBoundingBoxes; columns = 6; return true;
    }
    return false;
}

bool parseFile(const std::string& path, int width, int height, Parsed& parsed,
               std::string& error) {
    std::ifstream input(path);
    if (!input) { error = "Cannot open annotation file: " + path; return false; }
    std::string line;
    if (!std::getline(input, line)) { error = "Annotation CSV is empty"; return false; }
    line = stripCr(std::move(line));
    AnnotationCsvType type{};
    std::size_t expected = 0;
    if (!identify(line, type, expected)) {
        error = "Unknown annotation CSV header: " + line;
        return false;
    }
    std::size_t row = 1;
    while (std::getline(input, line)) {
        ++row;
        line = stripCr(std::move(line));
        if (line.empty()) continue;
        std::vector<std::string> f;
        if (!parseCsvRow(line, f) || f.size() != expected) {
            error = "Invalid CSV row " + std::to_string(row);
            return false;
        }
        int64_t t = 0;
        if (!parseInt64(f[0], t)) {
            error = "Invalid timestamp at row " + std::to_string(row);
            return false;
        }
        if (type == AnnotationCsvType::kPoints) {
            if (f[1].empty() && f[2].empty()) {
                parsed.push_back(std::make_unique<InterpolationEndpoint>(
                    t, AnnotationKind::kPoint));
                continue;
            }
            float x, y;
            if (!parseFloat(f[1], x) || !parseFloat(f[2], y) ||
                !pointInBounds(x, y, width, height)) {
                error = "Invalid point at row " + std::to_string(row);
                return false;
            }
            parsed.push_back(std::make_unique<PointAnnotation>(
                t, x, y, expected == 4 ? f[3] : std::string{}));
        } else if (type == AnnotationCsvType::kEyeTracking) {
            if (f[1].empty() && f[2].empty() && f[3].empty() &&
                f[4].empty() && f[5].empty()) {
                parsed.push_back(std::make_unique<InterpolationEndpoint>(
                    t, AnnotationKind::kEyeTracking));
                continue;
            }
            float phi, theta, x, y, radius;
            if (!parseFloat(f[1], phi) || !parseFloat(f[2], theta) ||
                !parseFloat(f[3], x) || !parseFloat(f[4], y) ||
                !parseFloat(f[5], radius) || radius <= 0.f ||
                !pointInBounds(x, y, width, height)) {
                error = "Invalid eye-tracking annotation at row " +
                        std::to_string(row);
                return false;
            }
            parsed.push_back(std::make_unique<EyeTracking>(
                t, phi, theta, x, y, radius,
                expected == 7 ? f[6] : std::string{}));
        } else {
            if (f[1].empty() && f[2].empty() && f[3].empty() &&
                f[4].empty() && f[5].empty()) {
                parsed.push_back(std::make_unique<InterpolationEndpoint>(
                    t, AnnotationKind::kBoundingBox));
                continue;
            }
            float x, y, w, h;
            if (!parseFloat(f[1], x) || !parseFloat(f[2], y) ||
                !parseFloat(f[3], w) || !parseFloat(f[4], h) ||
                w < 0.f || h < 0.f || !pointInBounds(x, y, width, height) ||
                (width > 0 && x + w > width) ||
                (height > 0 && y + h > height)) {
                error = "Invalid bounding box at row " + std::to_string(row);
                return false;
            }
            parsed.push_back(std::make_unique<BoundingBox>(t, x, y, w, h, f[5]));
        }
    }
    return true;
}

std::string csvQuote(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (char ch : value) result += ch == '"' ? "\"\"" : std::string(1, ch);
    return result + "\"";
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

bool supportedDataFile(const std::filesystem::path& path) {
    const std::string ext = lowerExtension(path);
    return ext == ".h5" || ext == ".hdf5" || ext == ".csv" ||
           ext == ".tsv" || ext == ".txt" || ext == ".log" ||
           ext == ".raw" || ext == ".mp4" || ext == ".mkv" ||
           ext == ".avi" || ext == ".mov";
}

bool imageSequenceDirectory(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) return false;
        if (!entry.is_regular_file()) continue;
        const std::string ext = lowerExtension(entry.path());
        if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg") && ++count > 50)
            return true;
    }
    return false;
}

} // namespace

bool loadAnnotationCsv(const std::string& path, AnnotationStore& store,
                       int width, int height, std::string& error) {
    Parsed parsed;
    if (!parseFile(path, width, height, parsed, error)) return false;
    for (auto& ann : parsed) {
        if (const auto* endpoint =
                dynamic_cast<const InterpolationEndpoint*>(ann.get())) {
            store.setInterpolationEndpoint(endpoint->timestamp(), endpoint->kind());
            continue;
        }
        store.add(std::move(ann));
    }
    return true;
}

bool saveAnnotationCsvFiles(const std::string& directory,
                            const std::string& video_stem,
                            const AnnotationStore& store, bool overwrite,
                            std::vector<std::string>& written,
                            std::string& error,
                            AnnotationCsvSaveSettings settings) {
    namespace fs = std::filesystem;
    written.clear();
    if (video_stem.empty()) { error = "Video stem is empty"; return false; }
    struct Output { std::string token, header; std::vector<const Annotation*> rows; };
    Output points{"points", "timestamp,x,y,label"};
    Output eyes{"eye_tracking", "timestamp,phi,theta,center_x,center_y,radius,label"};
    Output boxes{"bounding_boxes", "timestamp,x,y,w,h,label"};
    const auto authored = store.all();
    std::vector<std::unique_ptr<Annotation>> generated;
    if (settings.include_interpolated) {
        if (settings.interpolation_fps < 1 || settings.interpolation_fps > 240) {
            error = "Interpolation FPS must be between 1 and 240";
            return false;
        }
        if (!authored.empty()) {
            const int64_t first_t = authored.front()->timestamp();
            const int64_t last_t = authored.back()->timestamp();
            const long double fps = settings.interpolation_fps;
            int64_t frame = static_cast<int64_t>(
                std::floor(static_cast<long double>(first_t) * fps / 1'000'000.L));
            if (frame < 0) frame = 0;
            for (;; ++frame) {
                const long double sampled =
                    static_cast<long double>(frame) * 1'000'000.L / fps;
                if (sampled > static_cast<long double>(last_t)) break;
                const int64_t t = static_cast<int64_t>(sampled);
                auto at_t = interpolateAnnotationsAt(store, t);
                for (auto& ann : at_t) generated.push_back(std::move(ann));
                if (frame == std::numeric_limits<int64_t>::max()) break;
            }
        }
    }

    const auto add_row = [&](const Annotation* ann) {
        if (dynamic_cast<const PointAnnotation*>(ann)) points.rows.push_back(ann);
        else if (dynamic_cast<const EyeTracking*>(ann)) eyes.rows.push_back(ann);
        else if (dynamic_cast<const BoundingBox*>(ann)) boxes.rows.push_back(ann);
        else if (const auto* endpoint =
                     dynamic_cast<const InterpolationEndpoint*>(ann)) {
            if (endpoint->kind() == AnnotationKind::kPoint) points.rows.push_back(ann);
            else if (endpoint->kind() == AnnotationKind::kEyeTracking)
                eyes.rows.push_back(ann);
            else boxes.rows.push_back(ann);
        }
    };
    for (const Annotation* ann : authored) add_row(ann);
    for (const auto& ann : generated) add_row(ann.get());
    for (Output* output : {&points, &eyes, &boxes}) {
        std::stable_sort(output->rows.begin(), output->rows.end(),
                         [](const Annotation* a, const Annotation* b) {
                             return a->timestamp() < b->timestamp();
                         });
    }
    for (const Output* output : {&points, &eyes, &boxes}) {
        if (output->rows.empty()) continue;
        const fs::path path = fs::path(directory) /
            (video_stem + "_" + output->token + ".csv");
        if (!overwrite && fs::exists(path)) {
            error = "Annotation file already exists: " + path.string();
            return false;
        }
    }
    for (const Output* output : {&points, &eyes, &boxes}) {
        if (output->rows.empty()) continue;
        const fs::path path = fs::path(directory) /
            (video_stem + "_" + output->token + ".csv");
        std::ofstream out(path, std::ios::trunc);
        if (!out) { error = "Cannot write annotation file: " + path.string(); return false; }
        out << output->header << '\n' << std::setprecision(9);
        for (const Annotation* ann : output->rows) {
            if (const auto* endpoint =
                    dynamic_cast<const InterpolationEndpoint*>(ann)) {
                out << endpoint->timestamp();
                const int blanks = output == &points ? 3 : (output == &eyes ? 6 : 5);
                for (int i = 0; i < blanks; ++i) out << ',';
                out << '\n';
            } else if (const auto* p = dynamic_cast<const PointAnnotation*>(ann))
                out << p->timestamp() << ',' << p->x() << ',' << p->y() << ','
                    << csvQuote(p->label()) << '\n';
            else if (const auto* e = dynamic_cast<const EyeTracking*>(ann))
                out << e->timestamp() << ',' << e->phi() << ',' << e->theta()
                    << ',' << e->centerX() << ',' << e->centerY() << ','
                    << e->radius() << ',' << csvQuote(e->label()) << '\n';
            else if (const auto* b = dynamic_cast<const BoundingBox*>(ann))
                out << b->timestamp() << ',' << b->x() << ',' << b->y()
                    << ',' << b->w() << ',' << b->h() << ','
                    << csvQuote(b->label()) << '\n';
        }
        if (!out) { error = "Failed writing annotation file: " + path.string(); return false; }
        written.push_back(path.string());
    }
    return true;
}

std::vector<std::string> discoverAnnotationCsvFiles(
    const std::string& source_path, std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    const fs::path source(source_path);
    std::error_code ec;
    const bool source_is_directory = fs::is_directory(source, ec) && !ec;
    const fs::path directory = source_is_directory ? source : source.parent_path();
    const std::string stem = source.stem().string();
    std::vector<std::string> result;
    for (const std::string& token : {"points", "eye_tracking", "bounding_boxes"}) {
        std::vector<fs::path> specific;
        std::vector<fs::path> generic;
        ec.clear();
        for (const auto& entry : fs::directory_iterator(directory, ec)) {
            if (entry.is_regular_file() && entry.path().has_extension() &&
                entry.path().stem().string() == stem + "_" + token)
                specific.push_back(entry.path());
            else if (entry.is_regular_file() && entry.path().has_extension() &&
                     entry.path().stem().string() == token)
                generic.push_back(entry.path());
        }
        if (specific.size() == 1) result.push_back(specific.front().string());
        else if (specific.size() > 1)
            warnings.push_back("Ambiguous " + token + " annotations for " +
                               source.filename().string());
        else if (generic.size() == 1) result.push_back(generic.front().string());
        else if (generic.size() > 1)
            warnings.push_back("Ambiguous folder-level " + token +
                               " annotations in " + directory.string());
    }
    return result;
}

bool parseAnnotationFilename(const std::string& path, std::string& type_token,
                             std::string& source_stem) {
    const std::string stem = std::filesystem::path(path).stem().string();
    for (const std::string& token : {"points", "eye_tracking", "bounding_boxes"}) {
        if (stem == token) {
            type_token = token;
            source_stem.clear();
            return true;
        }
        const std::string suffix = "_" + token;
        if (stem.size() > suffix.size() &&
            stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
            type_token = token;
            source_stem = stem.substr(0, stem.size() - suffix.size());
            return true;
        }
    }
    return false;
}

bool resolveDirectAnnotationSelection(const std::string& selected_path,
                                      std::string& import_path,
                                      std::string& error) {
    namespace fs = std::filesystem;
    std::string token;
    std::string source_stem;
    if (!parseAnnotationFilename(selected_path, token, source_stem)) return false;
    const fs::path selected(selected_path);
    if (source_stem.empty()) {
        import_path = selected.parent_path().string();
        return true;
    }
    std::vector<fs::path> matches;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(selected.parent_path(), ec)) {
        if (entry.path() == selected) continue;
        if (entry.path().stem().string() != source_stem) continue;
        if ((entry.is_regular_file() && supportedDataFile(entry.path())) ||
            (entry.is_directory() && imageSequenceDirectory(entry.path())))
            matches.push_back(entry.path());
    }
    if (matches.size() == 1) {
        import_path = matches.front().string();
        return true;
    }
    if (matches.size() > 1)
        error = "Ambiguous data source for annotation file: " + selected_path;
    return false;
}

} // namespace mustard
