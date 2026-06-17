// src/ui/ViewerPanel.cpp
#include "mustard/ui/ViewerPanel.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "ImGuiFileDialog.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

namespace mustard {

namespace {

constexpr float kDefaultEyeRadius = 100.f;
constexpr float kIrisCircleRatio = 0.5f;

ImVec2 screen_to_sensor(ImVec2 p, ImVec2 img_origin, float scale) {
    return ImVec2((p.x - img_origin.x) / scale,
                  (p.y - img_origin.y) / scale);
}

float distance(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

void update_eye_orientation(float& phi, float& theta,
                            ImVec2 center, ImVec2 target, float radius) {
    const float c = std::sqrt(1.f - kIrisCircleRatio * kIrisCircleRatio);
    const float line_scale = std::max(radius * 1.5f * c, 1.f);
    const float sin_phi = std::clamp((target.x - center.x) / line_scale,
                                     -1.f, 1.f);
    phi = std::asin(sin_phi);

    const float cos_phi = std::max(std::cos(phi), 0.0001f);
    const float sin_theta = std::clamp(-(target.y - center.y) /
                                       (line_scale * cos_phi),
                                       -1.f, 1.f);
    theta = std::asin(sin_theta);
}

struct EyeTrackingAnnotationRef {
    int64_t timestamp{0};
    std::size_t index{0};
    const EyeTracking* eye{nullptr};
};

std::vector<EyeTrackingAnnotationRef>
find_eye_tracking_annotations_in_bin(const AnnotationStore& store, int64_t t) {
    std::vector<EyeTrackingAnnotationRef> refs;
    const auto* anns = store.queryAt(t);
    if (!anns) return refs;

    for (std::size_t i = 0; i < anns->size(); ++i) {
        const auto* eye = dynamic_cast<const EyeTracking*>((*anns)[i].get());
        if (!eye) continue;
        refs.push_back(EyeTrackingAnnotationRef{eye->timestamp(), i, eye});
    }
    return refs;
}

EyeTrackingAnnotationRef
find_eye_tracking_annotation_in_bin(const AnnotationStore& store, int64_t t) {
    const auto refs = find_eye_tracking_annotations_in_bin(store, t);
    return refs.empty() ? EyeTrackingAnnotationRef{} : refs.front();
}

void remove_eye_tracking_annotations_in_bin(AnnotationStore& store, int64_t t) {
    const auto refs = find_eye_tracking_annotations_in_bin(store, t);
    for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
        store.remove(it->timestamp, it->index);
    }
}

} // namespace

void ViewerPanel::setAnnotationStore(std::shared_ptr<AnnotationStore> store) {
    ann_store_ = std::move(store);
}

void ViewerPanel::drawAnnotationControls() {
    if (!annotating_) {
        if (ImGui::Button("Annotate")) {
            ImGui::OpenPopup("##ann_type");
        }
        if (ImGui::BeginPopup("##ann_type")) {
            ImGui::TextUnformatted("Annotation type:");
            ImGui::Separator();
            if (ImGui::Selectable("Bounding Box")) {
                annotation_type_ = AnnotationType::kBoundingBox;
                annotating_      = true;
            }
            if (ImGui::Selectable("Eye Tracking")) {
                annotation_type_ = AnnotationType::kEyeTracking;
                annotating_      = true;
            }
            ImGui::EndPopup();
        }
    } else {
        if (ImGui::Button("Stop")) {
            annotating_ = false;
        }
        ImGui::SameLine();
        const bool has_annotations = ann_store_ && ann_store_->totalCount() > 0;
        if (!has_annotations) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Save Annotations")) {
            // Use a per-panel key so multiple panels can coexist
            const std::string key = "SaveAnnotations_" + label_;
            ImGuiFileDialog::Instance()->OpenDialog(
                key, "Save Annotations", ".txt",
                ".", "annotations", 1, nullptr,
                ImGuiFileDialogFlags_ConfirmOverwrite);
        }
        if (!has_annotations) {
            ImGui::EndDisabled();
        }
    }

    // Display the file-save dialog when it is open for this panel
    const std::string key = "SaveAnnotations_" + label_;
    if (ImGuiFileDialog::Instance()->Display(
            key.c_str(), ImGuiWindowFlags_NoCollapse,
            ImVec2(600.f, 400.f)))
    {
        if (ImGuiFileDialog::Instance()->IsOk() && ann_store_) {
            const std::string path =
                ImGuiFileDialog::Instance()->GetFilePathName();
            std::ofstream f(path, std::ios::out | std::ios::trunc);
            f << ann_store_->serialize();
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void ViewerPanel::drawAnnotationInteraction(ImVec2 img_origin, float scale, int64_t t) {
    if (!annotating_ || !ann_store_) {
        dragging_ = false;
        return;
    }

    if (annotation_type_ == AnnotationType::kBoundingBox) {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            drag_start_ = ImGui::GetMousePos();
            dragging_   = true;
        }

        if (dragging_ && ImGui::IsMouseDown(0)) {
            const ImVec2 cur = ImGui::GetMousePos();
            ImGui::GetWindowDrawList()->AddRect(
                drag_start_, cur, IM_COL32(255, 200, 0, 180));
        }

        if (dragging_ && ImGui::IsMouseReleased(0)) {
            const ImVec2 cur = ImGui::GetMousePos();
            if (scale > 0.f) {
                const float x0 = std::min(drag_start_.x, cur.x);
                const float y0 = std::min(drag_start_.y, cur.y);
                const float x1 = std::max(drag_start_.x, cur.x);
                const float y1 = std::max(drag_start_.y, cur.y);
                const float bx = (x0 - img_origin.x) / scale;
                const float by = (y0 - img_origin.y) / scale;
                const float bw = (x1 - x0)           / scale;
                const float bh = (y1 - y0)           / scale;
                ann_store_->add(std::make_unique<BoundingBox>(t, bx, by, bw, bh));
            }
            dragging_ = false;
        }
        return;
    }

    if (annotation_type_ == AnnotationType::kEyeTracking) {
        if (scale <= 0.f) {
            dragging_ = false;
            return;
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            const ImGuiIO& io = ImGui::GetIO();
            drag_start_ = ImGui::GetMousePos();
            dragging_ = true;
            eye_drag_mode_ = io.KeyCtrl
                ? EyeTrackingDragMode::kMoveCenter
                : (io.KeyShift ? EyeTrackingDragMode::kResizeRadius
                               : EyeTrackingDragMode::kOrient);

            const ImVec2 start_sensor = screen_to_sensor(drag_start_, img_origin, scale);
            const EyeTrackingAnnotationRef existing_eye =
                find_eye_tracking_annotation_in_bin(*ann_store_, t);
            const bool has_existing_eye = existing_eye.eye != nullptr;

            eye_edit_active_ = has_existing_eye;
            eye_edit_index_ = has_existing_eye ? existing_eye.index : 0u;
            eye_draft_t_ = has_existing_eye ? existing_eye.timestamp : t;

            if (has_existing_eye) {
                eye_draft_phi_ = existing_eye.eye->phi();
                eye_draft_theta_ = existing_eye.eye->theta();
                eye_draft_center_x_ = existing_eye.eye->centerX();
                eye_draft_center_y_ = existing_eye.eye->centerY();
                eye_draft_radius_ = existing_eye.eye->radius();
            } else {
                eye_draft_phi_ = 0.f;
                eye_draft_theta_ = 0.f;
                eye_draft_center_x_ = start_sensor.x;
                eye_draft_center_y_ = start_sensor.y;
                eye_draft_radius_ = kDefaultEyeRadius;
            }
            eye_drag_start_center_x_ = eye_draft_center_x_;
            eye_drag_start_center_y_ = eye_draft_center_y_;
        }

        const auto update_draft = [&]() {
            const ImVec2 start_sensor = screen_to_sensor(drag_start_, img_origin, scale);
            const ImVec2 cur_sensor = screen_to_sensor(ImGui::GetMousePos(),
                                                       img_origin, scale);
            switch (eye_drag_mode_) {
                case EyeTrackingDragMode::kOrient:
                    update_eye_orientation(
                        eye_draft_phi_, eye_draft_theta_,
                        ImVec2(eye_draft_center_x_, eye_draft_center_y_),
                        cur_sensor,
                        eye_draft_radius_);
                    break;
                case EyeTrackingDragMode::kResizeRadius:
                    eye_draft_radius_ = std::max(
                        distance(ImVec2(eye_draft_center_x_, eye_draft_center_y_),
                                 cur_sensor),
                        1.f);
                    break;
                case EyeTrackingDragMode::kMoveCenter:
                    eye_draft_center_x_ =
                        eye_drag_start_center_x_ + (cur_sensor.x - start_sensor.x);
                    eye_draft_center_y_ =
                        eye_drag_start_center_y_ + (cur_sensor.y - start_sensor.y);
                    break;
            }
        };

        const auto commit_draft = [&]() {
            remove_eye_tracking_annotations_in_bin(*ann_store_, eye_draft_t_);
            ann_store_->add(std::make_unique<EyeTracking>(
                eye_draft_t_,
                eye_draft_phi_,
                eye_draft_theta_,
                eye_draft_center_x_,
                eye_draft_center_y_,
                eye_draft_radius_));

            const EyeTrackingAnnotationRef saved_eye =
                find_eye_tracking_annotation_in_bin(*ann_store_, eye_draft_t_);
            eye_edit_active_ = saved_eye.eye != nullptr;
            eye_edit_index_ = eye_edit_active_ ? saved_eye.index : 0u;
        };

        if (dragging_ && ImGui::IsMouseDown(0)) {
            update_draft();
            EyeTracking preview(eye_draft_t_,
                                eye_draft_phi_,
                                eye_draft_theta_,
                                eye_draft_center_x_,
                                eye_draft_center_y_,
                                eye_draft_radius_);
            preview.renderOverlay(ImGui::GetWindowDrawList(), img_origin, scale);
        }

        if (dragging_ && ImGui::IsMouseReleased(0)) {
            update_draft();
            commit_draft();
            dragging_ = false;
        }
    }
}

void ViewerPanel::drawAnnotationOverlay(ImVec2 img_origin, float scale, int64_t t) const {
    if (!ann_store_) return;
    const auto* anns = ann_store_->queryAt(t);
    if (!anns) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (std::size_t i = 0; i < anns->size(); ++i) {
        if (dragging_ &&
            annotation_type_ == AnnotationType::kEyeTracking &&
            eye_edit_active_) {
            const auto* eye = dynamic_cast<const EyeTracking*>((*anns)[i].get());
            if (eye && eye->timestamp() == eye_draft_t_) {
                continue;
            }
        }
        (*anns)[i]->renderOverlay(dl, img_origin, scale);
    }
}

} // namespace mustard
