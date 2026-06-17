// src/annotation/BoundingBox.cpp
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/Annotation.h"
#include "mustard/annotation/EyeTracking.h"

#include <memory>
#include <sstream>
#include <string>

namespace mustard {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BoundingBox::BoundingBox(int64_t t, float x, float y, float w, float h,
                         std::string label)
    : t_(t), x_(x), y_(y), w_(w), h_(h), label_(std::move(label))
{}

// ---------------------------------------------------------------------------
// renderOverlay
// ---------------------------------------------------------------------------

void BoundingBox::renderOverlay(ImDrawList* dl, ImVec2 origin, float scale) const {
    if (!dl) return;

    const ImVec2 tl(origin.x + x_ * scale,        origin.y + y_ * scale);
    const ImVec2 br(origin.x + (x_ + w_) * scale, origin.y + (y_ + h_) * scale);

    constexpr ImU32 kColor = IM_COL32(255, 200, 0, 220); // yellow-orange

    dl->AddRect(tl, br, kColor);

    if (!label_.empty()) {
        dl->AddText(tl, kColor, label_.c_str());
    }
}

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------

std::string BoundingBox::serialize() const {
    std::ostringstream oss;
    oss << "BoundingBox"
        << " t="     << t_
        << " x="     << x_
        << " y="     << y_
        << " w="     << w_
        << " h="     << h_
        << " label=" << label_
        << "\n";
    return oss.str();
}

// ---------------------------------------------------------------------------
// BoundingBox::deserialize  (static)
// ---------------------------------------------------------------------------
//
// Expected format (produced by serialize()):
//   BoundingBox t=<int64> x=<float> y=<float> w=<float> h=<float> label=<text>
//
// * All numeric fields are single tokens (no embedded spaces).
// * "label" is the last field; everything after "label=" to end-of-string
//   is taken as the label value (allows spaces in label text).
// * A missing or empty "label=" is handled gracefully (empty label).

namespace {

/// Return the value string for the first occurrence of "key=" in @p s,
/// delimited by the next space (or end-of-string).
std::string extract_field(const std::string& s, const std::string& key) {
    const std::string search = key + "=";
    const auto pos = s.find(search);
    if (pos == std::string::npos) return {};
    const auto start = pos + search.size();
    const auto end   = s.find(' ', start);
    if (end == std::string::npos) {
        // Last field — take to end, stripping trailing whitespace/newlines
        std::string val = s.substr(start);
        while (!val.empty() &&
               (val.back() == '\n' || val.back() == '\r' || val.back() == ' '))
            val.pop_back();
        return val;
    }
    return s.substr(start, end - start);
}

} // namespace

std::unique_ptr<BoundingBox> BoundingBox::deserialize(const std::string& s) {
    try {
        const int64_t t = std::stoll(extract_field(s, "t"));
        const float   x = std::stof (extract_field(s, "x"));
        const float   y = std::stof (extract_field(s, "y"));
        const float   w = std::stof (extract_field(s, "w"));
        const float   h = std::stof (extract_field(s, "h"));

        // Label: everything after "label=" to end-of-string, newlines stripped.
        std::string label;
        const auto lpos = s.find("label=");
        if (lpos != std::string::npos) {
            label = s.substr(lpos + 6); // 6 == strlen("label=")
            while (!label.empty() &&
                   (label.back() == '\n' || label.back() == '\r' ||
                    label.back() == ' '))
                label.pop_back();
        }

        return std::make_unique<BoundingBox>(t, x, y, w, h, std::move(label));
    } catch (...) {
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Annotation::deserialize  — factory, dispatches on type-tag prefix
// ---------------------------------------------------------------------------

std::unique_ptr<Annotation> Annotation::deserialize(const std::string& s) {
    // rfind with pos=0 checks whether s *starts with* the tag.
    if (s.rfind("BoundingBox", 0) == 0) {
        return BoundingBox::deserialize(s);
    }
    if (s.rfind("EyeTracking", 0) == 0) {
        return EyeTracking::deserialize(s);
    }
    return nullptr;
}

} // namespace mustard
