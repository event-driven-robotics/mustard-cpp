// src/annotation/AnnotationStore.cpp
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/Annotation.h"
#include "mustard/annotation/PointAnnotation.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "mustard/annotation/InterpolationEndpoint.h"

#include <algorithm>

#include <sstream>
#include <string>

namespace mustard {

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

void AnnotationStore::add(std::unique_ptr<Annotation> ann) {
    if (!ann) return;
    const int64_t t = ann->timestamp();
    bool has_kind = true;
    AnnotationKind kind{};
    if (dynamic_cast<const PointAnnotation*>(ann.get())) kind = AnnotationKind::kPoint;
    else if (dynamic_cast<const EyeTracking*>(ann.get())) kind = AnnotationKind::kEyeTracking;
    else if (dynamic_cast<const BoundingBox*>(ann.get())) kind = AnnotationKind::kBoundingBox;
    else has_kind = false;
    if (has_kind) {
        auto& bucket = annotations_[t];
        bucket.erase(std::remove_if(bucket.begin(), bucket.end(), [kind](const auto& item) {
            const auto* endpoint = dynamic_cast<const InterpolationEndpoint*>(item.get());
            return endpoint && endpoint->kind() == kind;
        }), bucket.end());
        for (auto& existing : bucket) {
            if (existing->typeName() == ann->typeName() &&
                existing->label() == ann->label()) {
                existing = std::move(ann);
                return;
            }
        }
    }
    annotations_[t].push_back(std::move(ann));
}

void AnnotationStore::remove(int64_t t, std::size_t index) {
    const auto it = annotations_.find(t);
    if (it == annotations_.end()) return;

    auto& vec = it->second;
    if (index >= vec.size()) return;

    vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(index));

    // Clean up the bucket when it becomes empty so queryAt returns nullptr
    if (vec.empty()) annotations_.erase(it);
}

std::size_t AnnotationStore::findIndex(int64_t t, const std::string& type,
                                       const std::string& label) const {
    const auto it = annotations_.find(t);
    if (it == annotations_.end()) return static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < it->second.size(); ++i)
        if (it->second[i]->typeName() == type && it->second[i]->label() == label)
            return i;
    return static_cast<std::size_t>(-1);
}

bool AnnotationStore::contains(int64_t t, const std::string& type,
                               const std::string& label) const {
    return findIndex(t, type, label) != static_cast<std::size_t>(-1);
}

bool AnnotationStore::replace(int64_t t, const std::string& type,
                              const std::string& label,
                              std::unique_ptr<Annotation> replacement) {
    const std::size_t index = findIndex(t, type, label);
    if (index == static_cast<std::size_t>(-1) || !replacement ||
        replacement->timestamp() != t) return false;
    auto& bucket = annotations_.find(t)->second;
    bucket[index] = std::move(replacement);
    return true;
}

void AnnotationStore::clear() {
    annotations_.clear();
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

const std::vector<std::unique_ptr<Annotation>>*
AnnotationStore::queryAt(int64_t t) const {
    if (annotations_.empty()) return nullptr;

    // Return the closest timestamp bucket only if it is within 0.3s.
    // Assumes timestamps are in microseconds.
    static constexpr int64_t kMaxDelta = 150'000;

    const auto right = annotations_.lower_bound(t); // first key >= t
    const auto left = (right == annotations_.begin())
        ? annotations_.end()
        : std::prev(right);

    auto absDiff = [](int64_t a, int64_t b) {
        return (a >= b) ? (a - b) : (b - a);
    };

    const typename decltype(annotations_)::const_iterator best = [&]() {
        if (right == annotations_.end()) return left;
        if (left == annotations_.end()) return right;
        return (absDiff(right->first, t) < absDiff(left->first, t)) ? right : left;
    }();

    if (best == annotations_.end()) return nullptr;
    if (absDiff(best->first, t) >= kMaxDelta) return nullptr;
    return &best->second;
}

const std::vector<std::unique_ptr<Annotation>>*
AnnotationStore::queryExact(int64_t t) const {
    const auto it = annotations_.find(t);
    return it == annotations_.end() ? nullptr : &it->second;
}

std::vector<const Annotation*>
AnnotationStore::queryRange(int64_t t0, int64_t t1) const {
    std::vector<const Annotation*> result;
    // lower_bound(t0) → first key >= t0
    // lower_bound(t1) → first key >= t1  (half-open: excludes t1)
    const auto begin = annotations_.lower_bound(t0);
    const auto end   = annotations_.lower_bound(t1);
    for (auto it = begin; it != end; ++it) {
        for (const auto& ann : it->second) {
            result.push_back(ann.get());
        }
    }
    return result;
}

std::size_t AnnotationStore::totalCount() const {
    std::size_t count = 0;
    for (const auto& [t, vec] : annotations_) {
        count += vec.size();
    }
    return count;
}

std::vector<const Annotation*> AnnotationStore::all() const {
    std::vector<const Annotation*> result;
    result.reserve(totalCount());
    for (const auto& [t, vec] : annotations_) {
        for (const auto& ann : vec) result.push_back(ann.get());
    }
    return result;
}

void AnnotationStore::removePointsAt(int64_t t) {
    const auto it = annotations_.find(t);
    if (it == annotations_.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(), [](const auto& ann) {
        return dynamic_cast<const PointAnnotation*>(ann.get()) != nullptr;
    }), vec.end());
    if (vec.empty()) annotations_.erase(it);
}

void AnnotationStore::setInterpolationEndpoint(int64_t t, AnnotationKind kind) {
    const auto it = annotations_.find(t);
    if (it != annotations_.end()) {
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [kind](const auto& ann) {
            if (const auto* endpoint =
                    dynamic_cast<const InterpolationEndpoint*>(ann.get()))
                return endpoint->kind() == kind;
            if (kind == AnnotationKind::kPoint)
                return dynamic_cast<const PointAnnotation*>(ann.get()) != nullptr;
            if (kind == AnnotationKind::kEyeTracking)
                return dynamic_cast<const EyeTracking*>(ann.get()) != nullptr;
            return dynamic_cast<const BoundingBox*>(ann.get()) != nullptr;
        }), vec.end());
        if (vec.empty()) annotations_.erase(it);
    }
    add(std::make_unique<InterpolationEndpoint>(t, kind));
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

std::string AnnotationStore::serialize() const {
    std::string result;
    for (const auto& [t, vec] : annotations_) {
        for (const auto& ann : vec) {
            result += ann->serialize(); // already '\n'-terminated
        }
    }
    return result;
}

bool AnnotationStore::deserialize(const std::string& s) {
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue; // skip blank lines
        auto ann = Annotation::deserialize(line);
        if (!ann) return false;
        add(std::move(ann));
    }
    return true;
}

bool AnnotationStore::restore(const std::string& state) {
    AnnotationStore restored;
    if (!restored.deserialize(state)) return false;
    annotations_ = std::move(restored.annotations_);
    return true;
}

} // namespace mustard
