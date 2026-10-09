// src/ui/ViewerPanel.cpp
#include "mustard/ui/ViewerPanel.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "mustard/annotation/PointAnnotation.h"
#include "mustard/annotation/AnnotationCsv.h"
#include "mustard/annotation/AnnotationInterpolation.h"
#include "mustard/annotation/InterpolationEndpoint.h"
#include "ImGuiFileDialog.h"
#include "imgui.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <set>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mustard {

struct ViewerPanel::ExportJob {
    cv::VideoWriter writer;
    VideoExportSettings settings;
    cv::Size size;
    int64_t next_time_us{0};
    int64_t frame_step_us{0};
    int64_t frame_count{0};
    int64_t frame_index{0};
};

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

std::string type_name(AnnotationType type) {
    if (type == AnnotationType::kPoint) return "Point";
    if (type == AnnotationType::kEyeTracking) return "EyeTracking";
    return "BoundingBox";
}

std::unique_ptr<Annotation> relabeled(const Annotation& ann, std::string label) {
    if (const auto* p = dynamic_cast<const PointAnnotation*>(&ann))
        return std::make_unique<PointAnnotation>(p->timestamp(), p->x(), p->y(), std::move(label));
    if (const auto* e = dynamic_cast<const EyeTracking*>(&ann))
        return std::make_unique<EyeTracking>(e->timestamp(), e->phi(), e->theta(),
            e->centerX(), e->centerY(), e->radius(), std::move(label));
    if (const auto* b = dynamic_cast<const BoundingBox*>(&ann))
        return std::make_unique<BoundingBox>(b->timestamp(), b->x(), b->y(),
            b->w(), b->h(), std::move(label));
    return nullptr;
}

std::optional<int64_t> first_changed_annotation_time(const std::string& before,
                                                     const std::string& after) {
    using Rows = std::map<int64_t, std::multiset<std::string>>;
    const auto parse = [](const std::string& state) {
        Rows rows;
        std::istringstream input(state);
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            if (auto annotation = Annotation::deserialize(line))
                rows[annotation->timestamp()].insert(line);
        }
        return rows;
    };
    const Rows left = parse(before);
    const Rows right = parse(after);
    auto a = left.begin();
    auto b = right.begin();
    while (a != left.end() || b != right.end()) {
        if (b == right.end() || (a != left.end() && a->first < b->first))
            return a->first;
        if (a == left.end() || b->first < a->first) return b->first;
        if (a->second != b->second) return a->first;
        ++a;
        ++b;
    }
    return std::nullopt;
}

} // namespace

ViewerPanel::~ViewerPanel() = default;

ViewerPanel::ViewerPanel(std::string label)
    : label_(std::move(label)), ann_store_(std::make_shared<AnnotationStore>()) {}
ViewerPanel::ViewerPanel(ViewerPanel&&) = default;
ViewerPanel& ViewerPanel::operator=(ViewerPanel&&) = default;

void ViewerPanel::setAnnotationStore(std::shared_ptr<AnnotationStore> store) {
    ann_store_ = std::move(store);
    undo_history_.clear();
    redo_history_.clear();
    clearAnnotationSelection();
}

void ViewerPanel::clearAnnotationSelection() {
    selected_ = {};
    dragging_ = false;
    annotation_drag_mode_ = AnnotationDragMode::kNone;
}

void ViewerPanel::pushAnnotationHistory(const std::string& before) {
    if (!ann_store_) return;
    const std::string after = ann_store_->snapshot();
    if (before == after) return;
    undo_history_.push_back({before, first_changed_annotation_time(before, after)});
    if (undo_history_.size() > 100) undo_history_.erase(undo_history_.begin());
    redo_history_.clear();
}

void ViewerPanel::undoAnnotation() {
    if (!ann_store_ || undo_history_.empty()) return;
    AnnotationHistoryEntry entry = std::move(undo_history_.back());
    undo_history_.pop_back();
    redo_history_.push_back({ann_store_->snapshot(), entry.affected_time_us});
    ann_store_->restore(entry.state);
    clearAnnotationSelection();
    if (entry.affected_time_us && *entry.affected_time_us != current_annotation_time_us_ &&
        timeline_seek_callback_)
        timeline_seek_callback_(*entry.affected_time_us + start_offset_us_);
    export_status_ = "Annotation edit undone";
}

void ViewerPanel::redoAnnotation() {
    if (!ann_store_ || redo_history_.empty()) return;
    AnnotationHistoryEntry entry = std::move(redo_history_.back());
    redo_history_.pop_back();
    undo_history_.push_back({ann_store_->snapshot(), entry.affected_time_us});
    ann_store_->restore(entry.state);
    clearAnnotationSelection();
    if (entry.affected_time_us && *entry.affected_time_us != current_annotation_time_us_ &&
        timeline_seek_callback_)
        timeline_seek_callback_(*entry.affected_time_us + start_offset_us_);
    export_status_ = "Annotation edit redone";
}

void ViewerPanel::drawAnnotationControls(int64_t annotation_time_us) {
    advanceVideoExport();
    current_annotation_time_us_ = annotation_time_us;
    const ImGuiIO& shortcut_io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() && shortcut_io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) undoAnnotation();
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) redoAnnotation();
    }
    if (!annotating_) {
        if (ImGui::Button("Annotate")) {
            ImGui::OpenPopup("##ann_type");
        }
        if (ImGui::BeginPopup("##ann_type")) {
            ImGui::TextUnformatted("Annotation type:");
            ImGui::Separator();
            if (ImGui::Selectable("Bounding Box")) {
                clearAnnotationSelection();
                annotation_type_ = AnnotationType::kBoundingBox;
                annotating_      = true;
            }
            if (ImGui::Selectable("Eye Tracking")) {
                clearAnnotationSelection();
                annotation_type_ = AnnotationType::kEyeTracking;
                annotating_      = true;
            }
            if (ImGui::Selectable("Point")) {
                clearAnnotationSelection();
                annotation_type_ = AnnotationType::kPoint;
                annotating_ = true;
            }
            ImGui::EndPopup();
        }
    } else {
        if (ImGui::Button("Stop")) {
            annotating_ = false;
            clearAnnotationSelection();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.f);
        ImGui::InputText("Label", annotation_label_.data(), annotation_label_.size());
        if (selected_.active && ImGui::IsItemDeactivatedAfterEdit() && ann_store_) {
            const std::string new_label(annotation_label_.data());
            const auto* bucket = ann_store_->queryExact(selected_.timestamp);
            const std::size_t index = ann_store_->findIndex(selected_.timestamp,
                selected_.type, selected_.label);
            if (new_label.empty() && !selected_.label.empty()) {
                export_status_ = "Annotation labels cannot be empty";
                std::snprintf(annotation_label_.data(), annotation_label_.size(), "%s",
                              selected_.label.c_str());
            } else if (new_label != selected_.label &&
                ann_store_->contains(selected_.timestamp, selected_.type, new_label)) {
                export_status_ = "That label already exists for this type and time";
                std::snprintf(annotation_label_.data(), annotation_label_.size(), "%s",
                              selected_.label.c_str());
            } else if (bucket && index < bucket->size()) {
                const std::string before = ann_store_->snapshot();
                auto replacement = relabeled(*(*bucket)[index], new_label);
                if (ann_store_->replace(selected_.timestamp, selected_.type,
                                        selected_.label, std::move(replacement))) {
                    selected_.label = new_label;
                    pushAnnotationHistory(before);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##label_suggestions", "Suggestions")) {
            std::set<std::string> labels;
            if (ann_store_) for (const Annotation* ann : ann_store_->all())
                if (ann->typeName() == type_name(annotation_type_) && !ann->label().empty())
                    labels.insert(ann->label());
            for (const auto& candidate : labels)
                if (ImGui::Selectable(candidate.c_str()))
                    std::snprintf(annotation_label_.data(), annotation_label_.size(), "%s",
                                  candidate.c_str());
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (annotation_time_us < 0) ImGui::BeginDisabled();
        if (ImGui::Button("No Annotation Here") && ann_store_) {
            AnnotationKind kind = AnnotationKind::kBoundingBox;
            if (annotation_type_ == AnnotationType::kPoint)
                kind = AnnotationKind::kPoint;
            else if (annotation_type_ == AnnotationType::kEyeTracking)
                kind = AnnotationKind::kEyeTracking;
            const std::string before = ann_store_->snapshot();
            ann_store_->setInterpolationEndpoint(annotation_time_us, kind);
            pushAnnotationHistory(before);
            export_status_ = "Marked annotation endpoint";
        }
        if (annotation_time_us < 0) ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (undo_history_.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo")) undoAnnotation();
    if (undo_history_.empty()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (redo_history_.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo")) redoAnnotation();
    if (redo_history_.empty()) ImGui::EndDisabled();
    if (annotating_) {
        ImGui::SameLine();
        if (!selected_.active) ImGui::BeginDisabled();
        if (ImGui::Button("Delete Annotation") && ann_store_ && selected_.active) {
            const std::string before = ann_store_->snapshot();
            const std::size_t index = ann_store_->findIndex(selected_.timestamp,
                selected_.type, selected_.label);
            ann_store_->remove(selected_.timestamp, index);
            pushAnnotationHistory(before);
            clearAnnotationSelection();
        }
        if (!selected_.active) ImGui::EndDisabled();
    }

    ImGui::SameLine();
    const bool has_annotations = ann_store_ && ann_store_->totalCount() > 0;
    const bool can_interpolate = has_annotations || annotating_;
    if (!can_interpolate) ImGui::BeginDisabled();
    ImGui::Checkbox("Interpolate", &interpolation_enabled_);
    if (!can_interpolate) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!has_annotations) ImGui::BeginDisabled();
    if (ImGui::Button("Save Annotations")) {
        ImGui::OpenPopup(("Save annotation settings##" + label_).c_str());
    }
    if (!has_annotations) ImGui::EndDisabled();

    const std::string save_settings_key = "Save annotation settings##" + label_;
    if (ImGui::BeginPopupModal(save_settings_key.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Checkbox("Save interpolated values", &save_interpolated_values_);
        if (!save_interpolated_values_) ImGui::BeginDisabled();
        ImGui::InputInt("Interpolation FPS", &interpolation_save_fps_);
        interpolation_save_fps_ = std::clamp(interpolation_save_fps_, 1, 240);
        if (!save_interpolated_values_) ImGui::EndDisabled();
        if (ImGui::Button("Choose Directory")) {
            const std::string key = "SaveAnnotations_" + label_;
            ImGuiFileDialog::Instance()->OpenDialog(
                key, "Choose Annotation Directory", nullptr, ".");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Display the file-save dialog when it is open for this panel
    const std::string key = "SaveAnnotations_" + label_;
    if (ImGuiFileDialog::Instance()->Display(
            key.c_str(), ImGuiWindowFlags_NoCollapse,
            ImVec2(600.f, 400.f)))
    {
        if (ImGuiFileDialog::Instance()->IsOk() && ann_store_) {
            pending_save_directory_ = ImGuiFileDialog::Instance()->GetFilePathName();
            if (pending_save_directory_.empty())
                pending_save_directory_ = ImGuiFileDialog::Instance()->GetCurrentPath();
            std::vector<std::string> written;
            std::string error;
            const AnnotationCsvSaveSettings settings{
                save_interpolated_values_, interpolation_save_fps_};
            if (!saveAnnotationCsvFiles(pending_save_directory_, annotation_file_stem_,
                                        *ann_store_, false, written, error, settings)) {
                if (error.find("already exists") != std::string::npos)
                    ImGui::OpenPopup(("Overwrite annotations?##" + label_).c_str());
                else
                    export_status_ = error;
            } else {
                export_status_ = "Saved " + std::to_string(written.size()) + " annotation file(s)";
                pending_save_directory_.clear();
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    const std::string overwrite_key = "Overwrite annotations?##" + label_;
    if (ImGui::BeginPopupModal(overwrite_key.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("One or more annotation CSV files already exist.");
        if (ImGui::Button("Overwrite") && ann_store_) {
            std::vector<std::string> written;
            std::string error;
            const AnnotationCsvSaveSettings settings{
                save_interpolated_values_, interpolation_save_fps_};
            if (saveAnnotationCsvFiles(pending_save_directory_, annotation_file_stem_,
                                       *ann_store_, true, written, error, settings))
                export_status_ = "Saved " + std::to_string(written.size()) + " annotation file(s)";
            else
                export_status_ = error;
            pending_save_directory_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_save_directory_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Load Annotations")) {
        ImGuiFileDialog::Instance()->OpenDialog(
            "LoadAnnotations_" + label_, "Load Annotations", ".*", ".");
    }
    const std::string load_key = "LoadAnnotations_" + label_;
    if (ImGuiFileDialog::Instance()->Display(load_key.c_str(),
            ImGuiWindowFlags_NoCollapse, ImVec2(600.f, 400.f))) {
        if (ImGuiFileDialog::Instance()->IsOk() && ann_store_) {
            std::string error;
            const std::string before = ann_store_->snapshot();
            if (loadAnnotationCsv(ImGuiFileDialog::Instance()->GetFilePathName(),
                                  *ann_store_, annotation_image_width_,
                                  annotation_image_height_, error)) {
                export_status_ = "Annotations loaded";
                pushAnnotationHistory(before);
                clearAnnotationSelection();
            } else
                export_status_ = error;
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (!export_settings_initialized_) {
        export_settings_.start_us = streamStartUs();
        export_settings_.end_us = streamEndUs();
        export_settings_initialized_ = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Export Video")) {
        ImGui::OpenPopup(("Export settings##" + label_).c_str());
    }
    if (!export_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", export_status_.c_str());
    }
    if (exporting_) {
        ImGui::SameLine();
        ImGui::ProgressBar(export_progress_, ImVec2(120.f, 0.f), "Exporting");
    }

    const std::string popup_key = "Export settings##" + label_;
    if (ImGui::BeginPopupModal(popup_key.c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Export range (seconds from stream start)");
        const int64_t stream_start = streamStartUs();
        const int64_t stream_end = streamEndUs();
        const float stream_duration_s = static_cast<float>(stream_end - stream_start) / 1e6f;
        float start_s = static_cast<float>(export_settings_.start_us - stream_start) / 1e6f;
        float end_s = static_cast<float>(export_settings_.end_us - stream_start) / 1e6f;
        ImGui::InputFloat("Start (s)", &start_s, 0.1f, 1.f, "%.3f");
        ImGui::InputFloat("End (s)", &end_s, 0.1f, 1.f, "%.3f");
        start_s = std::clamp(start_s, 0.f, stream_duration_s);
        end_s = std::clamp(end_s, 0.f, stream_duration_s);
        export_settings_.start_us = stream_start + static_cast<int64_t>(start_s * 1e6f);
        export_settings_.end_us = stream_start + static_cast<int64_t>(end_s * 1e6f);
        ImGui::InputInt("FPS", &export_settings_.fps);
        export_settings_.fps = std::clamp(export_settings_.fps, 1, 240);
        ImGui::Checkbox("Include annotations", &export_settings_.include_annotations);
        ImGui::Checkbox("Lossless (FFV1 in MKV)", &export_settings_.lossless);
        if (export_settings_.lossless) {
            ImGui::TextDisabled("Lossless files use the .mkv extension and can be large.");
        }
        if (export_settings_.end_us <= export_settings_.start_us) {
            ImGui::TextDisabled("End must be after start.");
        }
        if (ImGui::Button("Choose File...", {120.f, 0.f}) &&
            export_settings_.end_us > export_settings_.start_us) {
            ImGuiFileDialog::Instance()->OpenDialog(
                "ExportVideo_" + label_, "Export Video", ".mp4,.mkv",
                ".", export_settings_.lossless ? "export.mkv" : "export.mp4", 1, nullptr,
                ImGuiFileDialogFlags_ConfirmOverwrite);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const std::string export_dialog_key = "ExportVideo_" + label_;
    if (ImGuiFileDialog::Instance()->Display(
            export_dialog_key.c_str(), ImGuiWindowFlags_NoCollapse,
            ImVec2(600.f, 400.f))) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string error;
            if (!startVideoExport(ImGuiFileDialog::Instance()->GetFilePathName(), error))
                export_status_ = "Export failed: " + error;
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

bool ViewerPanel::startVideoExport(const std::string& output_path,
                                   std::string& error) {
    if (output_path.empty()) {
        error = "no output file selected";
        return false;
    }
    if (export_settings_.lossless &&
        std::filesystem::path(output_path).extension() != ".mkv") {
        error = "lossless export requires an .mkv output file";
        return false;
    }
    beginVideoExport();
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!renderFrameForExport(export_settings_.start_us, rgba, width, height) ||
        width <= 0 || height <= 0) {
        error = "could not render the first frame";
        endVideoExport();
        return false;
    }

    cv::VideoWriter writer;
    const cv::Size size(width, height);
    const int codec = export_settings_.lossless
        ? cv::VideoWriter::fourcc('F', 'F', 'V', '1')
        : cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    if (!writer.open(output_path, codec,
                     export_settings_.fps, size, true)) {
        error = "could not open output file";
        return false;
    }
    export_job_ = std::make_unique<ExportJob>();
    export_job_->writer = std::move(writer);
    export_job_->settings = export_settings_;
    export_job_->size = size;
    export_job_->next_time_us = export_settings_.start_us;
    export_job_->frame_step_us = std::max<int64_t>(1, 1'000'000 / export_settings_.fps);
    export_job_->frame_count = std::max<int64_t>(1,
        (export_settings_.end_us - export_settings_.start_us + export_job_->frame_step_us - 1) /
        export_job_->frame_step_us);
    export_progress_ = 0.f;
    exporting_ = true;
    export_status_ = "Exporting video";
    return true;
}

void ViewerPanel::advanceVideoExport() {
    if (!export_job_) return;
    ExportJob& job = *export_job_;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    const int64_t t = job.next_time_us;
    if (t < job.settings.end_us) {
        if (!renderFrameForExport(t, rgba, width, height) ||
            width != job.size.width || height != job.size.height ||
            rgba.size() != static_cast<std::size_t>(width * height * 4)) {
            export_status_ = "Export failed: could not render an export frame";
            endVideoExport();
            export_job_.reset();
            exporting_ = false;
            return;
        }
        cv::Mat rgba_frame(height, width, CV_8UC4, rgba.data());
        cv::Mat bgr_frame;
        cv::cvtColor(rgba_frame, bgr_frame, cv::COLOR_RGBA2BGR);
        job.writer.write(bgr_frame);
        ++job.frame_index;
        job.next_time_us += job.frame_step_us;
        export_progress_ = static_cast<float>(job.frame_index) /
                           static_cast<float>(job.frame_count);
        return;
    }
    export_status_ = "Video exported";
    export_progress_ = 1.f;
    endVideoExport();
    export_job_.reset();
    exporting_ = false;
}

bool ViewerPanel::renderFrameForExport(int64_t, std::vector<uint8_t>&,
                                       int&, int&) {
    return false;
}

void ViewerPanel::beginVideoExport() {}
void ViewerPanel::endVideoExport() {}

void ViewerPanel::drawAnnotationInteraction(ImVec2 img_origin, float scale, int64_t t) {
    if (!annotating_ || !ann_store_) {
        dragging_ = false;
        return;
    }
    if (scale <= 0.f) return;

    const ImVec2 mouse = ImGui::GetMousePos();
    const ImVec2 sensor = screen_to_sensor(mouse, img_origin, scale);
    const Annotation* hit = nullptr;
    bool hit_interpolated = false;
    std::vector<std::unique_ptr<Annotation>> interpolated;
    const auto* visible = interpolation_enabled_ ? ann_store_->queryExact(t)
                                                  : ann_store_->queryAt(t);
    if (selected_.active) {
        bool still_visible = false;
        if (visible) for (const auto& item : *visible)
            if (item->timestamp() == selected_.timestamp &&
                item->typeName() == selected_.type && item->label() == selected_.label) {
                still_visible = true; break;
            }
        if (!still_visible) clearAnnotationSelection();
    }
    auto hit_test = [&](const Annotation* ann) {
        if (const auto* p = dynamic_cast<const PointAnnotation*>(ann))
            return distance(mouse, ImVec2(img_origin.x + p->x() * scale,
                img_origin.y + p->y() * scale)) <= 10.f;
        if (const auto* b = dynamic_cast<const BoundingBox*>(ann))
            return sensor.x >= b->x() && sensor.y >= b->y() &&
                   sensor.x <= b->x() + b->w() && sensor.y <= b->y() + b->h();
        if (const auto* e = dynamic_cast<const EyeTracking*>(ann))
            return distance(sensor, ImVec2(e->centerX(), e->centerY())) <= e->radius();
        return false;
    };
    const auto hit_area = [](const Annotation* ann) {
        if (dynamic_cast<const PointAnnotation*>(ann)) return 1.f;
        if (const auto* b = dynamic_cast<const BoundingBox*>(ann)) return b->w() * b->h();
        if (const auto* e = dynamic_cast<const EyeTracking*>(ann)) return e->radius() * e->radius();
        return 1e30f;
    };
    float best_area = 1e30f;
    if (visible) for (auto it = visible->rbegin(); it != visible->rend(); ++it)
        if (hit_test(it->get()) && hit_area(it->get()) < best_area) {
            hit = it->get(); best_area = hit_area(hit);
        }
    if (!hit && interpolation_enabled_) {
        interpolated = interpolateAnnotationsAt(*ann_store_, t);
        for (auto it = interpolated.rbegin(); it != interpolated.rend(); ++it)
            if (hit_test(it->get()) && hit_area(it->get()) < best_area) {
                hit = it->get(); best_area = hit_area(hit); hit_interpolated = true;
            }
    }

    if (selected_.active && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const std::string before = ann_store_->snapshot();
        ann_store_->remove(selected_.timestamp, ann_store_->findIndex(
            selected_.timestamp, selected_.type, selected_.label));
        pushAnnotationHistory(before);
        clearAnnotationSelection();
        return;
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        drag_start_ = mouse;
        if (hit) {
            if (hit_interpolated) {
                const std::string before = ann_store_->snapshot();
                if (const auto* p = dynamic_cast<const PointAnnotation*>(hit))
                    ann_store_->add(std::make_unique<PointAnnotation>(t, p->x(), p->y(), p->label()));
                else if (const auto* b = dynamic_cast<const BoundingBox*>(hit))
                    ann_store_->add(std::make_unique<BoundingBox>(t, b->x(), b->y(), b->w(), b->h(), b->label()));
                else if (const auto* e = dynamic_cast<const EyeTracking*>(hit))
                    ann_store_->add(std::make_unique<EyeTracking>(t, e->phi(), e->theta(), e->centerX(), e->centerY(), e->radius(), e->label()));
                pushAnnotationHistory(before);
                visible = ann_store_->queryExact(t);
                const std::size_t idx = ann_store_->findIndex(t, hit->typeName(), hit->label());
                hit = visible && idx < visible->size() ? (*visible)[idx].get() : nullptr;
            }
            if (!hit) return;
            selected_ = {true, hit->timestamp(), hit->typeName(), hit->label()};
            std::snprintf(annotation_label_.data(), annotation_label_.size(), "%s", hit->label().c_str());
            if (hit->typeName() == "Point") annotation_type_ = AnnotationType::kPoint;
            else if (hit->typeName() == "EyeTracking") annotation_type_ = AnnotationType::kEyeTracking;
            else annotation_type_ = AnnotationType::kBoundingBox;
            drag_history_snapshot_ = ann_store_->snapshot();
            dragging_ = true;
            annotation_drag_mode_ = AnnotationDragMode::kMove;
            if (const auto* p = dynamic_cast<const PointAnnotation*>(hit)) {
                drag_base_x_ = p->x(); drag_base_y_ = p->y();
            } else if (const auto* b = dynamic_cast<const BoundingBox*>(hit)) {
                drag_base_x_ = b->x(); drag_base_y_ = b->y();
                drag_base_w_ = b->w(); drag_base_h_ = b->h();
                const ImVec2 handles[] = {
                    {img_origin.x + b->x() * scale, img_origin.y + b->y() * scale},
                    {img_origin.x + (b->x() + b->w()) * scale, img_origin.y + b->y() * scale},
                    {img_origin.x + b->x() * scale, img_origin.y + (b->y() + b->h()) * scale},
                    {img_origin.x + (b->x() + b->w()) * scale, img_origin.y + (b->y() + b->h()) * scale},
                    {img_origin.x + (b->x() + b->w() * .5f) * scale, img_origin.y + b->y() * scale},
                    {img_origin.x + (b->x() + b->w() * .5f) * scale, img_origin.y + (b->y() + b->h()) * scale},
                    {img_origin.x + b->x() * scale, img_origin.y + (b->y() + b->h() * .5f) * scale},
                    {img_origin.x + (b->x() + b->w()) * scale, img_origin.y + (b->y() + b->h() * .5f) * scale}};
                for (int corner = 0; corner < 8; ++corner) if (distance(mouse, handles[corner]) <= 12.f) {
                    annotation_drag_mode_ = AnnotationDragMode::kResize;
                    bbox_resize_x_ = (corner == 0 || corner == 2 || corner == 6) ? -1
                        : ((corner == 4 || corner == 5) ? 0 : 1);
                    bbox_resize_y_ = (corner == 0 || corner == 1 || corner == 4) ? -1
                        : ((corner == 6 || corner == 7) ? 0 : 1);
                    break;
                }
            } else if (const auto* e = dynamic_cast<const EyeTracking*>(hit)) {
                eye_draft_phi_ = e->phi(); eye_draft_theta_ = e->theta();
                eye_draft_center_x_ = e->centerX(); eye_draft_center_y_ = e->centerY();
                eye_drag_start_center_x_ = e->centerX(); eye_drag_start_center_y_ = e->centerY();
                eye_draft_radius_ = e->radius();
                const ImGuiIO& io = ImGui::GetIO();
                const ImVec2 center(img_origin.x + e->centerX() * scale,
                                    img_origin.y + e->centerY() * scale);
                const ImVec2 radius_handle(center.x + e->radius() * scale, center.y);
                const float c = std::sqrt(1.f - kIrisCircleRatio * kIrisCircleRatio);
                const ImVec2 gaze(center.x + e->radius() * 1.5f * c * std::sin(e->phi()) * scale,
                    center.y - e->radius() * 1.5f * c * std::sin(e->theta()) * std::cos(e->phi()) * scale);
                annotation_drag_mode_ = (io.KeyShift || distance(mouse, radius_handle) <= 12.f)
                    ? AnnotationDragMode::kResize
                    : ((io.KeyCtrl || distance(mouse, center) <= 12.f)
                        ? AnnotationDragMode::kMove : AnnotationDragMode::kOrient);
            }
        } else {
            clearAnnotationSelection();
            const std::string label(annotation_label_.data());
            if (label.empty()) { export_status_ = "Enter a label before creating an annotation"; return; }
            if (ann_store_->contains(t, type_name(annotation_type_), label)) {
                export_status_ = "That label already exists for this type and time"; return;
            }
            drag_history_snapshot_ = ann_store_->snapshot();
            if (annotation_type_ == AnnotationType::kPoint) {
                ann_store_->add(std::make_unique<PointAnnotation>(t, sensor.x, sensor.y, label));
                selected_ = {true, t, "Point", label};
                pushAnnotationHistory(drag_history_snapshot_);
                return;
            }
            dragging_ = true;
            annotation_drag_mode_ = AnnotationDragMode::kCreate;
            if (annotation_type_ == AnnotationType::kEyeTracking) {
                eye_draft_phi_ = eye_draft_theta_ = 0.f;
                eye_draft_center_x_ = sensor.x; eye_draft_center_y_ = sensor.y;
                eye_draft_radius_ = kDefaultEyeRadius;
            }
        }
    }

    if (!dragging_) return;
    const ImVec2 start_sensor = screen_to_sensor(drag_start_, img_origin, scale);
    const float max_x = annotation_image_width_ > 0 ? static_cast<float>(annotation_image_width_) : 1e9f;
    const float max_y = annotation_image_height_ > 0 ? static_cast<float>(annotation_image_height_) : 1e9f;
    const ImVec2 cur(std::clamp(sensor.x, 0.f, max_x), std::clamp(sensor.y, 0.f, max_y));
    if (annotation_type_ == AnnotationType::kBoundingBox) {
        if (annotation_drag_mode_ == AnnotationDragMode::kCreate) {
            draft_x_ = std::min(start_sensor.x, cur.x); draft_y_ = std::min(start_sensor.y, cur.y);
            draft_w_ = std::abs(cur.x - start_sensor.x); draft_h_ = std::abs(cur.y - start_sensor.y);
        } else if (annotation_drag_mode_ == AnnotationDragMode::kMove) {
            draft_x_ = std::clamp(drag_base_x_ + cur.x - start_sensor.x, 0.f, std::max(0.f, max_x - drag_base_w_));
            draft_y_ = std::clamp(drag_base_y_ + cur.y - start_sensor.y, 0.f, std::max(0.f, max_y - drag_base_h_));
            draft_w_ = drag_base_w_; draft_h_ = drag_base_h_;
        } else {
            if (bbox_resize_x_ == 0) { draft_x_ = drag_base_x_; draft_w_ = drag_base_w_; }
            else {
                const float opposite_x = bbox_resize_x_ < 0 ? drag_base_x_ + drag_base_w_ : drag_base_x_;
                draft_x_ = std::min(cur.x, opposite_x);
                draft_w_ = std::max(1.f, std::abs(cur.x - opposite_x));
            }
            if (bbox_resize_y_ == 0) { draft_y_ = drag_base_y_; draft_h_ = drag_base_h_; }
            else {
                const float opposite_y = bbox_resize_y_ < 0 ? drag_base_y_ + drag_base_h_ : drag_base_y_;
                draft_y_ = std::min(cur.y, opposite_y);
                draft_h_ = std::max(1.f, std::abs(cur.y - opposite_y));
            }
        }
        BoundingBox preview(t, draft_x_, draft_y_, draft_w_, draft_h_, annotation_label_.data());
        preview.renderOverlay(ImGui::GetWindowDrawList(), img_origin, scale);
    } else if (annotation_type_ == AnnotationType::kEyeTracking) {
        if (annotation_drag_mode_ == AnnotationDragMode::kMove) {
            eye_draft_radius_ = std::min(eye_draft_radius_,
                std::max(1.f, std::min(max_x, max_y) * 0.5f));
            eye_draft_center_x_ = std::clamp(eye_drag_start_center_x_ + cur.x - start_sensor.x,
                eye_draft_radius_, std::max(eye_draft_radius_, max_x - eye_draft_radius_));
            eye_draft_center_y_ = std::clamp(eye_drag_start_center_y_ + cur.y - start_sensor.y,
                eye_draft_radius_, std::max(eye_draft_radius_, max_y - eye_draft_radius_));
        } else if (annotation_drag_mode_ == AnnotationDragMode::kResize)
            eye_draft_radius_ = std::clamp(distance(ImVec2(eye_draft_center_x_, eye_draft_center_y_), cur),
                1.f, std::max(1.f, std::min({eye_draft_center_x_, eye_draft_center_y_,
                    max_x - eye_draft_center_x_, max_y - eye_draft_center_y_})));
        else update_eye_orientation(eye_draft_phi_, eye_draft_theta_,
            ImVec2(eye_draft_center_x_, eye_draft_center_y_), cur, eye_draft_radius_);
        EyeTracking preview(t, eye_draft_phi_, eye_draft_theta_, eye_draft_center_x_,
            eye_draft_center_y_, eye_draft_radius_, annotation_label_.data());
        preview.renderOverlay(ImGui::GetWindowDrawList(), img_origin, scale);
    } else if (annotation_type_ == AnnotationType::kPoint) {
        draft_x_ = cur.x; draft_y_ = cur.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 preview(img_origin.x + draft_x_ * scale,
                             img_origin.y + draft_y_ * scale);
        constexpr ImU32 kPreviewColor = IM_COL32(255, 40, 220, 255);
        dl->AddCircleFilled(preview, 6.f, kPreviewColor);
        if (annotation_label_[0] != '\0')
            dl->AddText(ImVec2(preview.x + 8.f, preview.y - 8.f),
                        kPreviewColor, annotation_label_.data());
    }

    if (ImGui::IsMouseReleased(0)) {
        const std::string label(annotation_label_.data());
        std::unique_ptr<Annotation> replacement;
        if (annotation_type_ == AnnotationType::kBoundingBox)
            replacement = std::make_unique<BoundingBox>(selected_.active ? selected_.timestamp : t,
                draft_x_, draft_y_, std::max(1.f, draft_w_), std::max(1.f, draft_h_), label);
        else if (annotation_type_ == AnnotationType::kEyeTracking)
        {
            eye_draft_radius_ = std::min(eye_draft_radius_,
                std::max(1.f, std::min(max_x, max_y) * 0.5f));
            eye_draft_center_x_ = std::clamp(eye_draft_center_x_, eye_draft_radius_,
                std::max(eye_draft_radius_, max_x - eye_draft_radius_));
            eye_draft_center_y_ = std::clamp(eye_draft_center_y_, eye_draft_radius_,
                std::max(eye_draft_radius_, max_y - eye_draft_radius_));
            replacement = std::make_unique<EyeTracking>(selected_.active ? selected_.timestamp : t,
                eye_draft_phi_, eye_draft_theta_, eye_draft_center_x_, eye_draft_center_y_,
                eye_draft_radius_, label);
        }
        else replacement = std::make_unique<PointAnnotation>(selected_.timestamp, draft_x_, draft_y_, label);
        if (selected_.active)
            ann_store_->replace(selected_.timestamp, selected_.type, selected_.label, std::move(replacement));
        else {
            ann_store_->add(std::move(replacement));
            selected_ = {true, t, type_name(annotation_type_), label};
        }
        pushAnnotationHistory(drag_history_snapshot_);
        dragging_ = false;
        annotation_drag_mode_ = AnnotationDragMode::kNone;
    }
}

void ViewerPanel::drawAnnotationOverlay(ImVec2 img_origin, float scale, int64_t t) const {
    if (!ann_store_) return;
    const auto* anns = interpolation_enabled_
        ? ann_store_->queryExact(t)
        : ann_store_->queryAt(t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (anns) for (std::size_t i = 0; i < anns->size(); ++i) {
        const Annotation* current = (*anns)[i].get();
        if (dragging_ && selected_.active &&
            current->timestamp() == selected_.timestamp &&
            current->typeName() == selected_.type &&
            current->label() == selected_.label)
            continue;
        if (dynamic_cast<const PointAnnotation*>((*anns)[i].get())) continue;
        (*anns)[i]->renderOverlay(dl, img_origin, scale);
    }

    if (interpolation_enabled_) {
        if (anns) for (const auto& ann : *anns) {
            if (dragging_ && selected_.active &&
                ann->timestamp() == selected_.timestamp &&
                ann->typeName() == selected_.type &&
                ann->label() == selected_.label)
                continue;
            if (const auto* point = dynamic_cast<const PointAnnotation*>(ann.get()))
                point->renderOverlay(dl, img_origin, scale);
        }
        auto interpolated = interpolateAnnotationsAt(*ann_store_, t);
        for (const auto& ann : interpolated) {
            if (dragging_ && annotation_type_ == AnnotationType::kEyeTracking &&
                dynamic_cast<const EyeTracking*>(ann.get()))
                continue;
            ann->renderOverlay(dl, img_origin, scale);
        }
    } else {
        if (anns) for (const auto& ann : *anns)
            if (!(dragging_ && selected_.active &&
                  ann->timestamp() == selected_.timestamp &&
                  ann->typeName() == selected_.type &&
                  ann->label() == selected_.label))
              if (const auto* point = dynamic_cast<const PointAnnotation*>(ann.get()))
                point->renderOverlay(dl, img_origin, scale);
    }

    if (!annotating_ || !selected_.active || scale <= 0.f) return;
    if (dragging_) return;
    bool selected_visible = false;
    const auto* current = interpolation_enabled_ ? ann_store_->queryExact(t)
                                                  : ann_store_->queryAt(t);
    if (current) for (const auto& item : *current)
        if (item->timestamp() == selected_.timestamp && item->typeName() == selected_.type &&
            item->label() == selected_.label) { selected_visible = true; break; }
    if (!selected_visible) return;
    const auto* bucket = ann_store_->queryExact(selected_.timestamp);
    const std::size_t index = ann_store_->findIndex(selected_.timestamp,
        selected_.type, selected_.label);
    if (!bucket || index >= bucket->size()) return;
    const Annotation* ann = (*bucket)[index].get();
    constexpr ImU32 kSelected = IM_COL32(255, 40, 220, 255);
    constexpr float kHandle = 4.f;
    const auto handle = [&](ImVec2 p) {
        dl->AddRectFilled(ImVec2(p.x - kHandle, p.y - kHandle),
                          ImVec2(p.x + kHandle, p.y + kHandle), kSelected);
    };
    if (const auto* p = dynamic_cast<const PointAnnotation*>(ann)) {
        const ImVec2 pos(img_origin.x + p->x() * scale,
                         img_origin.y + p->y() * scale);
        dl->AddCircle(pos, 9.f, kSelected, 0, 2.5f);
        if (!p->label().empty()) dl->AddText(ImVec2(pos.x + 10.f, pos.y - 10.f), kSelected, p->label().c_str());
    } else if (const auto* b = dynamic_cast<const BoundingBox*>(ann)) {
        const ImVec2 tl(img_origin.x + b->x() * scale, img_origin.y + b->y() * scale);
        const ImVec2 br(img_origin.x + (b->x() + b->w()) * scale,
                        img_origin.y + (b->y() + b->h()) * scale);
        dl->AddRect(tl, br, kSelected, 0.f, 0, 2.5f);
        handle(tl); handle(br); handle(ImVec2(br.x, tl.y)); handle(ImVec2(tl.x, br.y));
        handle(ImVec2((tl.x + br.x) * .5f, tl.y));
        handle(ImVec2((tl.x + br.x) * .5f, br.y));
        handle(ImVec2(tl.x, (tl.y + br.y) * .5f));
        handle(ImVec2(br.x, (tl.y + br.y) * .5f));
        if (!b->label().empty()) dl->AddText(tl, kSelected, b->label().c_str());
    } else if (const auto* e = dynamic_cast<const EyeTracking*>(ann)) {
        const ImVec2 center(img_origin.x + e->centerX() * scale,
                            img_origin.y + e->centerY() * scale);
        dl->AddCircle(center, e->radius() * scale, kSelected, 0, 2.5f);
        handle(center);
        const ImVec2 radius_handle(center.x + e->radius() * scale, center.y);
        handle(radius_handle);
        const float c = std::sqrt(1.f - kIrisCircleRatio * kIrisCircleRatio);
        const ImVec2 gaze(center.x + e->radius() * 1.5f * c * std::sin(e->phi()) * scale,
            center.y - e->radius() * 1.5f * c * std::sin(e->theta()) * std::cos(e->phi()) * scale);
        dl->AddLine(center, gaze, kSelected, 2.5f); handle(gaze);
        if (!e->label().empty()) dl->AddText(ImVec2(center.x + e->radius() * scale + 5.f,
            center.y - e->radius() * scale), kSelected, e->label().c_str());
    }
}

} // namespace mustard
