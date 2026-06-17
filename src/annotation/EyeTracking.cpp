#include "mustard/annotation/EyeTracking.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mustard {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string extract_field(const std::string& s, const std::string& key) {
    const std::string search = key + "=";
    const auto pos = s.find(search);
    if (pos == std::string::npos) return {};
    const auto start = pos + search.size();
    const auto end = s.find(' ', start);
    if (end == std::string::npos) {
        std::string val = s.substr(start);
        while (!val.empty() &&
               (val.back() == '\n' || val.back() == '\r' || val.back() == ' '))
            val.pop_back();
        return val;
    }
    return s.substr(start, end - start);
}

ImVec2 project_sensor_point(ImVec2 origin, float scale, float x, float y) {
    return ImVec2(origin.x + x * scale, origin.y + y * scale);
}

} // namespace

EyeTracking::EyeTracking(int64_t t, float phi, float theta,
                         float center_x, float center_y, float radius)
    : t_(t),
      phi_(phi),
      theta_(theta),
      center_x_(center_x),
      center_y_(center_y),
      radius_(std::max(radius, 1.f))
{}

void EyeTracking::renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const {
    if (!dl || scale <= 0.f) return;

    constexpr float kIrisCircleRatio = 0.5f;
    const float c = std::sqrt(1.f - kIrisCircleRatio * kIrisCircleRatio);
    const float cos_phi = std::cos(phi_);
    const float sin_phi = std::sin(phi_);
    const float cos_theta = std::cos(theta_);
    const float sin_theta = std::sin(theta_);

    std::vector<ImVec2> ellipse_points;
    ellipse_points.reserve(22);
    for (float angle = -kPi; angle <= kPi + kPi / 20.f; angle += kPi / 10.f) {
        const float xhat = kIrisCircleRatio * std::cos(angle);
        const float yhat = kIrisCircleRatio * std::sin(angle);
        const float x = radius_ * (xhat * cos_phi + c * sin_phi) + center_x_;
        const float y = radius_ * ((xhat * sin_phi - c * cos_phi) * sin_theta +
                                   yhat * cos_theta) +
                        center_y_;
        ellipse_points.push_back(project_sensor_point(origin, scale, x, y));
    }

    const float iris_x = radius_ * (c * sin_phi) + center_x_;
    const float iris_y = radius_ * (c * (-sin_theta * cos_phi)) + center_y_;
    const float gaze_x = radius_ * 1.5f * (c * sin_phi) + center_x_;
    const float gaze_y = radius_ * 1.5f * (c * (-sin_theta * cos_phi)) + center_y_;

    const ImVec2 center = project_sensor_point(origin, scale, center_x_, center_y_);
    const ImVec2 iris = project_sensor_point(origin, scale, iris_x, iris_y);
    const ImVec2 gaze = project_sensor_point(origin, scale, gaze_x, gaze_y);

    constexpr ImU32 kEllipseColor = IM_COL32(255, 60, 60, 220);
    constexpr ImU32 kCenterColor = IM_COL32(0, 230, 110, 230);
    constexpr ImU32 kLineColor = IM_COL32(70, 145, 255, 230);
    constexpr float kPointRadius = 3.f;

    if (ellipse_points.size() >= 2) {
        dl->AddPolyline(ellipse_points.data(),
                        static_cast<int>(ellipse_points.size()),
                        kEllipseColor,
                        ImDrawFlags_None,
                        1.5f);
    }
    dl->AddCircleFilled(iris, kPointRadius, kCenterColor);
    dl->AddCircleFilled(center, kPointRadius, kCenterColor);
    dl->AddLine(center, gaze, kLineColor, 1.5f);
}

std::string EyeTracking::serialize() const {
    std::ostringstream oss;
    oss << std::setprecision(9)
        << "EyeTracking"
        << " t=" << t_
        << " phi=" << phi_
        << " theta=" << theta_
        << " center_x=" << center_x_
        << " center_y=" << center_y_
        << " radius=" << radius_
        << "\n";
    return oss.str();
}

std::unique_ptr<EyeTracking> EyeTracking::deserialize(const std::string& s) {
    try {
        const int64_t t = std::stoll(extract_field(s, "t"));
        const float phi = std::stof(extract_field(s, "phi"));
        const float theta = std::stof(extract_field(s, "theta"));
        const float center_x = std::stof(extract_field(s, "center_x"));
        const float center_y = std::stof(extract_field(s, "center_y"));
        const float radius = std::stof(extract_field(s, "radius"));

        return std::make_unique<EyeTracking>(t, phi, theta, center_x, center_y, radius);
    } catch (...) {
        return nullptr;
    }
}

} // namespace mustard
