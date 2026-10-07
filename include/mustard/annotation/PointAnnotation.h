#pragma once

#include "mustard/annotation/Annotation.h"

#include <memory>
#include <string>

namespace mustard {

class PointAnnotation : public Annotation {
public:
    PointAnnotation(int64_t t, float x, float y) : t_(t), x_(x), y_(y) {}

    void renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const override;
    std::string serialize() const override;
    int64_t timestamp() const noexcept override { return t_; }
    std::string typeName() const noexcept override { return "Point"; }

    float x() const noexcept { return x_; }
    float y() const noexcept { return y_; }

    static std::unique_ptr<PointAnnotation> deserialize(const std::string& s);

private:
    int64_t t_;
    float x_;
    float y_;
};

} // namespace mustard
