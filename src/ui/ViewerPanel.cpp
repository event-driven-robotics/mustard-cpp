// src/ui/ViewerPanel.cpp
#include "mustard/ui/ViewerPanel.h"
#include "mustard/annotation/BoundingBox.h"
#include "ImGuiFileDialog.h"
#include "imgui.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace mustard {

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
    if (!annotating_ || annotation_type_ != AnnotationType::kBoundingBox || !ann_store_)
    {
        dragging_ = false;
        return;
    }

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
}

void ViewerPanel::drawAnnotationOverlay(ImVec2 img_origin, float scale, int64_t t) const {
    if (!ann_store_) return;
    const auto* anns = ann_store_->queryAt(t);
    if (!anns) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& ann : *anns) {
        ann->renderOverlay(dl, img_origin, scale);
    }
}

} // namespace mustard
