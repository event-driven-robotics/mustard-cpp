#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace mustard {

class Annotation;
class AnnotationStore;

/// Build transient annotations for timestamp @p t by linearly interpolating
/// compatible authored keyframes. The returned objects are never inserted into
/// @p store. Interpolation is strictly bounded by keyframes (no extrapolation).
std::vector<std::unique_ptr<Annotation>> interpolateAnnotationsAt(
    const AnnotationStore& store, int64_t t);

} // namespace mustard
