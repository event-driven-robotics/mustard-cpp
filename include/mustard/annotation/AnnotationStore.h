// include/mustard/annotation/AnnotationStore.h
#pragma once
#include "mustard/annotation/Annotation.h"
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mustard {

/// Timestamp-keyed container for Annotation objects.
///
/// Keys are anchor timestamps in microseconds.  Multiple annotations may
/// share the same timestamp (stored in insertion order within each bucket).
class AnnotationStore {
public:
    // ------------------------------------------------------------------
    // Mutation
    // ------------------------------------------------------------------

    /// Insert @p ann under its own timestamp().  Null pointers are ignored.
    void add(std::unique_ptr<Annotation> ann);

    /// Remove the annotation at position @p index within timestamp @p t.
    /// No-op when @p t has no annotations or @p index is out of range.
    /// The map entry is erased when its bucket becomes empty.
    void remove(int64_t t, std::size_t index);

    /// Remove all annotations.
    void clear();

    // ------------------------------------------------------------------
    // Query
    // ------------------------------------------------------------------

    /// Exact-timestamp lookup.
    /// @return Pointer to the bucket vector, or nullptr if none exists.
    const std::vector<std::unique_ptr<Annotation>>* queryAt(int64_t t) const;

    /// Half-open range query [t0, t1).
    /// @return Flat list of raw pointers; ownership stays in the store.
    std::vector<const Annotation*> queryRange(int64_t t0, int64_t t1) const;

    /// Total number of annotations across all timestamps.
    std::size_t totalCount() const;

    // ------------------------------------------------------------------
    // Serialisation
    // ------------------------------------------------------------------

    /// Serialise every annotation in timestamp order (one line each).
    std::string serialize() const;

    /// Append annotations parsed from @p s (produced by serialize()).
    /// Returns false and leaves the store partially populated on any parse
    /// error; call clear() first if you want a clean load.
    bool deserialize(const std::string& s);

private:
    std::map<int64_t, std::vector<std::unique_ptr<Annotation>>> annotations_;
};

} // namespace mustard
