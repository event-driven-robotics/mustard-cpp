// src/annotation/AnnotationStore.cpp
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/Annotation.h"

#include <sstream>
#include <string>

namespace mustard {

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

void AnnotationStore::add(std::unique_ptr<Annotation> ann) {
    if (!ann) return;
    const int64_t t = ann->timestamp();
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

void AnnotationStore::clear() {
    annotations_.clear();
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

const std::vector<std::unique_ptr<Annotation>>*
AnnotationStore::queryAt(int64_t t) const {
    const auto it = annotations_.find(t);
    if (it == annotations_.end()) return nullptr;
    return &it->second;
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

} // namespace mustard
