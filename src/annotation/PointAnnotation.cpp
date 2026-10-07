#include "mustard/annotation/PointAnnotation.h"

#include <iomanip>
#include <sstream>

namespace mustard {
namespace {

std::string field(const std::string& s, const std::string& key) {
    const std::string prefix = key + "=";
    const auto pos = s.find(prefix);
    if (pos == std::string::npos) return {};
    const auto start = pos + prefix.size();
    const auto end = s.find(' ', start);
    return s.substr(start, end == std::string::npos ? end : end - start);
}

} // namespace

void PointAnnotation::renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const {
    if (!dl || scale <= 0.f) return;
    constexpr ImU32 kColor = IM_COL32(0, 230, 255, 235);
    constexpr float kScreenRadius = 5.f;
    dl->AddCircleFilled(ImVec2(origin.x + x_ * scale, origin.y + y_ * scale),
                        kScreenRadius, kColor);
}

std::string PointAnnotation::serialize() const {
    std::ostringstream out;
    out << std::setprecision(9) << "Point t=" << t_ << " x=" << x_
        << " y=" << y_ << '\n';
    return out.str();
}

std::unique_ptr<PointAnnotation> PointAnnotation::deserialize(const std::string& s) {
    try {
        return std::make_unique<PointAnnotation>(
            std::stoll(field(s, "t")), std::stof(field(s, "x")),
            std::stof(field(s, "y")));
    } catch (...) {
        return nullptr;
    }
}

} // namespace mustard
