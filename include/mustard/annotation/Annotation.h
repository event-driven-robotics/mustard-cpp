// include/mustard/annotation/Annotation.h
#pragma once
#include "imgui.h"
#include <cstdint>
#include <memory>
#include <string>

namespace mustard {

/// Abstract base class for all annotation types.
///
/// Subclasses implement:
///   - renderOverlay  — draw themselves onto an ImDrawList in screen space
///   - serialize      — emit a text representation (one line, ends with '\n')
///   - timestamp      — µs timestamp the annotation is anchored to
///   - typeName       — stable string tag used by the deserialize factory
///
/// The static Annotation::deserialize factory dispatches on the type tag
/// prefix and is implemented alongside the concrete types (BoundingBox.cpp).
class Annotation {
public:
    virtual ~Annotation() = default;

    /// Draw this annotation onto @p dl.
    /// @param origin   Top-left corner of the sensor image in screen space.
    /// @param scale    Pixels-per-sensor-unit scaling factor.
    virtual void renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const = 0;

    /// Serialise to a single '\n'-terminated text line.
    virtual std::string serialize()         const = 0;

    /// Anchor timestamp in microseconds.
    virtual int64_t     timestamp()         const noexcept = 0;

    /// Stable string tag, e.g. "BoundingBox".
    virtual std::string typeName()          const noexcept = 0;

    /// Factory: parse a serialised line produced by any known Annotation
    /// subclass.  Returns nullptr if the type tag is unrecognised or
    /// parsing fails.
    static std::unique_ptr<Annotation> deserialize(const std::string& s);
};

} // namespace mustard
