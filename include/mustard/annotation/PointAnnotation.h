#pragma once

#include "mustard/annotation/Annotation.h"

#include <memory>
#include <string>
#include <utility>

namespace mustard {

class PointAnnotation : public Annotation {
public:
    PointAnnotation(int64_t t, float x, float y, std::string label = {})
        : t_(t), x_(x), y_(y), label_(std::move(label)) {}

    void renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const override;
    std::string serialize() const override;
    int64_t timestamp() const noexcept override { return t_; }
    std::string typeName() const noexcept override { return "Point"; }

    float x() const noexcept { return x_; }
    float y() const noexcept { return y_; }
    const std::string& label() const noexcept override { return label_; }

    static std::unique_ptr<PointAnnotation> deserialize(const std::string& s);

private:
    int64_t t_;
    float x_;
    float y_;
    std::string label_;
};

} // namespace mustard
