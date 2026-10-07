#include "mustard/annotation/InterpolationEndpoint.h"

#include <sstream>

namespace mustard {
namespace {

const char* kindName(AnnotationKind kind) {
    switch (kind) {
        case AnnotationKind::kPoint: return "Point";
        case AnnotationKind::kEyeTracking: return "EyeTracking";
        case AnnotationKind::kBoundingBox: return "BoundingBox";
    }
    return "";
}

bool parseKind(const std::string& value, AnnotationKind& kind) {
    if (value == "Point") kind = AnnotationKind::kPoint;
    else if (value == "EyeTracking") kind = AnnotationKind::kEyeTracking;
    else if (value == "BoundingBox") kind = AnnotationKind::kBoundingBox;
    else return false;
    return true;
}

} // namespace

std::string InterpolationEndpoint::serialize() const {
    std::ostringstream out;
    out << "InterpolationEndpoint t=" << t_ << " kind=" << kindName(kind_) << '\n';
    return out.str();
}

std::unique_ptr<InterpolationEndpoint>
InterpolationEndpoint::deserialize(const std::string& s) {
    try {
        const auto t_pos = s.find("t=");
        const auto kind_pos = s.find("kind=");
        if (t_pos == std::string::npos || kind_pos == std::string::npos) return nullptr;
        const int64_t t = std::stoll(s.substr(t_pos + 2));
        std::string name = s.substr(kind_pos + 5);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' ||
                                 name.back() == ' '))
            name.pop_back();
        AnnotationKind kind{};
        if (!parseKind(name, kind)) return nullptr;
        return std::make_unique<InterpolationEndpoint>(t, kind);
    } catch (...) {
        return nullptr;
    }
}

} // namespace mustard
