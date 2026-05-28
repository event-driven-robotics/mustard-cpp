// include/mustard/annotation/BoundingBox.h
#pragma once
#include "mustard/annotation/Annotation.h"
#include <memory>
#include <string>

namespace mustard {

/// Axis-aligned bounding box annotation.
///
/// Coordinates (x, y, w, h) are in sensor-pixel space.  renderOverlay
/// scales them to screen space using the supplied origin and scale factor.
class BoundingBox : public Annotation {
public:
    /// @param t      Timestamp in microseconds.
    /// @param x,y    Top-left corner in sensor pixels.
    /// @param w,h    Width and height in sensor pixels.
    /// @param label  Optional text label displayed at the top-left corner.
    BoundingBox(int64_t t, float x, float y, float w, float h,
                std::string label = "");

    // ------------------------------------------------------------------
    // Annotation interface
    // ------------------------------------------------------------------

    void renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const override;

    std::string serialize()   const override;
    int64_t     timestamp()   const noexcept override { return t_; }
    std::string typeName()    const noexcept override { return "BoundingBox"; }

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    float              x()     const noexcept { return x_; }
    float              y()     const noexcept { return y_; }
    float              w()     const noexcept { return w_; }
    float              h()     const noexcept { return h_; }
    const std::string& label() const noexcept { return label_; }

    // ------------------------------------------------------------------
    // Type-specific deserialisation
    // ------------------------------------------------------------------

    /// Parse a line produced by serialize().  Returns nullptr on failure.
    static std::unique_ptr<BoundingBox> deserialize(const std::string& s);

private:
    int64_t     t_;
    float       x_, y_, w_, h_;
    std::string label_;
};

} // namespace mustard
