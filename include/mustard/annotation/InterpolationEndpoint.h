#pragma once

#include "mustard/annotation/Annotation.h"

#include <memory>

namespace mustard {

enum class AnnotationKind { kPoint, kEyeTracking, kBoundingBox };

/// An authored keyframe declaring that an annotation type is absent at a
/// timestamp. It is invisible, but prevents interpolation across that time.
class InterpolationEndpoint : public Annotation {
public:
    InterpolationEndpoint(int64_t t, AnnotationKind kind) : t_(t), kind_(kind) {}

    void renderOverlay(ImDrawList*, ImVec2, float) const override {}
    std::string serialize() const override;
    int64_t timestamp() const noexcept override { return t_; }
    std::string typeName() const noexcept override { return "InterpolationEndpoint"; }
    AnnotationKind kind() const noexcept { return kind_; }

    static std::unique_ptr<InterpolationEndpoint> deserialize(const std::string& s);

private:
    int64_t t_;
    AnnotationKind kind_;
};

} // namespace mustard
