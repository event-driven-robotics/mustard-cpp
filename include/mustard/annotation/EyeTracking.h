#pragma once
#include "mustard/annotation/Annotation.h"
#include <cstdint>
#include <memory>
#include <string>

namespace mustard {

/// Eye-gaze annotation.
///
/// phi and theta are gaze pitch/yaw angles in radians.  center_x, center_y,
/// and radius are in sensor pixels and describe the eyeball center/radius,
/// not the iris.
class EyeTracking : public Annotation {
public:
    EyeTracking(int64_t t, float phi, float theta,
                float center_x, float center_y, float radius);

    void renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const override;

    std::string serialize() const override;
    int64_t     timestamp() const noexcept override { return t_; }
    std::string typeName()  const noexcept override { return "EyeTracking"; }

    float phi()      const noexcept { return phi_; }
    float theta()    const noexcept { return theta_; }
    float centerX()  const noexcept { return center_x_; }
    float centerY()  const noexcept { return center_y_; }
    float radius()   const noexcept { return radius_; }

    static std::unique_ptr<EyeTracking> deserialize(const std::string& s);

private:
    int64_t t_;
    float   phi_;
    float   theta_;
    float   center_x_;
    float   center_y_;
    float   radius_;
};

} // namespace mustard
