#include "mustard/annotation/AnnotationInterpolation.h"

#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "mustard/annotation/PointAnnotation.h"
#include "mustard/annotation/InterpolationEndpoint.h"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace mustard {
namespace {

float lerp(float a, float b, double amount) {
    return static_cast<float>(a + (b - a) * amount);
}

template <typename T>
using Keyframes = std::map<int64_t, std::vector<const T*>>;

template <typename T>
std::pair<const T*, const T*> surrounding(const Keyframes<T>& frames,
                                           int64_t t) {
    const auto right = frames.upper_bound(t);
    if (right == frames.end() || right == frames.begin()) return {nullptr, nullptr};
    const auto left = std::prev(right);
    if (left->first >= t || right->first <= t ||
        left->second.size() != 1 || right->second.size() != 1)
        return {nullptr, nullptr};
    return {left->second.front(), right->second.front()};
}

double amountBetween(int64_t t, int64_t left, int64_t right) {
    return static_cast<double>(t - left) / static_cast<double>(right - left);
}

} // namespace

std::vector<std::unique_ptr<Annotation>> interpolateAnnotationsAt(
    const AnnotationStore& store, int64_t t) {
    std::map<std::string, Keyframes<PointAnnotation>> points;
    std::map<std::string, Keyframes<EyeTracking>> eyes;
    std::map<std::string, Keyframes<BoundingBox>> boxes;
    std::map<AnnotationKind, std::set<int64_t>> endpoints;

    for (const Annotation* ann : store.all()) {
        if (const auto* endpoint = dynamic_cast<const InterpolationEndpoint*>(ann))
            endpoints[endpoint->kind()].insert(endpoint->timestamp());
        else if (const auto* point = dynamic_cast<const PointAnnotation*>(ann))
            points[point->label()][point->timestamp()].push_back(point);
        else if (const auto* eye = dynamic_cast<const EyeTracking*>(ann))
            eyes[eye->label()][eye->timestamp()].push_back(eye);
        else if (const auto* box = dynamic_cast<const BoundingBox*>(ann))
            boxes[box->label()][box->timestamp()].push_back(box);
    }

    std::vector<std::unique_ptr<Annotation>> result;
    for (const auto& [label, frames] : points) {
      if (const auto [left, right] = surrounding(frames, t); left && right) {
        const auto boundary = endpoints[AnnotationKind::kPoint].upper_bound(left->timestamp());
        if (boundary == endpoints[AnnotationKind::kPoint].end() ||
            *boundary >= right->timestamp()) {
            const double amount = amountBetween(t, left->timestamp(), right->timestamp());
            result.push_back(std::make_unique<PointAnnotation>(
                t, lerp(left->x(), right->x(), amount),
                lerp(left->y(), right->y(), amount), label));
        }
      }
    }
    for (const auto& [label, frames] : eyes) {
      if (const auto [left, right] = surrounding(frames, t); left && right) {
        const auto boundary = endpoints[AnnotationKind::kEyeTracking].upper_bound(
            left->timestamp());
        if (boundary == endpoints[AnnotationKind::kEyeTracking].end() ||
            *boundary >= right->timestamp()) {
            const double amount = amountBetween(t, left->timestamp(), right->timestamp());
            result.push_back(std::make_unique<EyeTracking>(
                t, lerp(left->phi(), right->phi(), amount),
                lerp(left->theta(), right->theta(), amount),
                lerp(left->centerX(), right->centerX(), amount),
                lerp(left->centerY(), right->centerY(), amount),
                lerp(left->radius(), right->radius(), amount), label));
        }
      }
    }
    for (const auto& [label, frames] : boxes) {
        const auto [left, right] = surrounding(frames, t);
        if (!left || !right) continue;
        const auto boundary = endpoints[AnnotationKind::kBoundingBox].upper_bound(
            left->timestamp());
        if (boundary != endpoints[AnnotationKind::kBoundingBox].end() &&
            *boundary < right->timestamp())
            continue;
        const double amount = amountBetween(t, left->timestamp(), right->timestamp());
        result.push_back(std::make_unique<BoundingBox>(
            t, lerp(left->x(), right->x(), amount),
            lerp(left->y(), right->y(), amount),
            lerp(left->w(), right->w(), amount),
            lerp(left->h(), right->h(), amount), label));
    }
    return result;
}

} // namespace mustard
