#include "mustard/data/events/EventImport.h"
#include "mustard/data/events/TabularEventSource.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace mustard {

std::string ImportDiagnostic::describe() const {
    std::string result = path;
    if (!dataset.empty()) result += (result.empty() ? "" : " :: ") + dataset;
    if (row) result += " (row " + std::to_string(row) + ")";
    if (!field.empty()) result += " [" + field + "]";
    if (!result.empty() && !message.empty()) result += ": ";
    result += message;
    if (result.empty() && cancelled) result = "Import cancelled";
    return result;
}

std::string tableCellText(const TableCell& cell) {
    return std::visit([](const auto& value) -> std::string {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) return value;
        else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
        else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
            return out.str();
        } else return std::to_string(value);
    }, cell);
}

std::array<int, 4> inferEventColumns(const std::vector<std::string>& names) {
    std::array<int, 4> result{{-1, -1, -1, -1}};
    std::array<unsigned, 4> matches{{0, 0, 0, 0}};
    bool positional = true;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] != "Column " + std::to_string(i + 1)) positional = false;
        std::string name = names[i];
        const auto slash = name.find_last_of('/');
        if (slash != std::string::npos) name.erase(0, slash + 1);
        const auto first = name.find_first_not_of(" \t\r\n");
        const auto last = name.find_last_not_of(" \t\r\n");
        name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        int field = -1;
        if (name == "x" || name == "xs") field = 0;
        else if (name == "y" || name == "ys") field = 1;
        else if (name == "ts" || name == "t" || name == "timestamp") field = 2;
        else if (name == "p" || name == "ps" || name == "polarity") field = 3;
        if (field >= 0) {
            ++matches[field];
            result[field] = static_cast<int>(i);
        }
    }
    if (positional) {
        for (std::size_t i = 0; i < 4 && i < names.size(); ++i)
            result[i] = static_cast<int>(i);
    } else {
        for (std::size_t i = 0; i < 4; ++i)
            if (matches[i] != 1) result[i] = -1;
    }
    return result;
}

bool previewEventSource(const std::string& path, const EventImportOptions& options,
                        EventTablePreview& preview, ImportDiagnostic& error,
                        ImportCancellation cancel) {
    preview = {};
    error = {};
    error.path = path;
    error.dataset = options.dataset;
    auto source = options.format == EventSourceFormat::Hdf5
        ? createHdf5Source(path, options, error, cancel)
        : createCsvSource(path, options, error, cancel);
    if (!source) return false;
    preview.columns = source->columns();
    preview.suggested_columns = inferEventColumns(preview.columns);
    if (!source->readRows(20, preview.rows, error)) return false;
    if (preview.rows.empty()) {
        error.message = "The selected table contains no event records";
        return false;
    }
    return true;
}

} // namespace mustard
