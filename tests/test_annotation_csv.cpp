#include "mustard/annotation/AnnotationCsv.h"
#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/BoundingBox.h"
#include "mustard/annotation/EyeTracking.h"
#include "mustard/annotation/PointAnnotation.h"
#include "mustard/annotation/InterpolationEndpoint.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace mustard;

namespace {

class TempDir {
public:
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("mustard_annotation_csv_" + std::to_string(++counter));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
    static int counter;
};
int TempDir::counter = 0;

void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path) << text;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST(AnnotationCsvTest, LoadsEachSchemaIndependentOfExtension) {
    TempDir dir;
    write(dir.path / "points.data", "timestamp,x,y\r\n100,1.5,2.5\r\n");
    write(dir.path / "eyes.anything",
          "timestamp,phi,theta,center_x,center_y,radius\n200,0.1,-0.2,3,4,5\n");
    write(dir.path / "boxes.bin",
          "timestamp,x,y,w,h,label\n300,1,2,3,4,\"puck, left\"\n");
    AnnotationStore store;
    std::string error;
    EXPECT_TRUE(loadAnnotationCsv((dir.path / "points.data").string(), store, 20, 20, error)) << error;
    EXPECT_TRUE(loadAnnotationCsv((dir.path / "eyes.anything").string(), store, 20, 20, error)) << error;
    EXPECT_TRUE(loadAnnotationCsv((dir.path / "boxes.bin").string(), store, 20, 20, error)) << error;
    EXPECT_EQ(store.totalCount(), 3u);
    const auto* boxes = store.queryAt(300);
    ASSERT_NE(boxes, nullptr);
    const auto* box = dynamic_cast<const BoundingBox*>((*boxes)[0].get());
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->label(), "puck, left");
}

TEST(AnnotationCsvTest, FailureIsAtomicAndRejectsNonFiniteOrBounds) {
    TempDir dir;
    write(dir.path / "bad.csv", "timestamp,x,y\n100,1,2\n200,nan,4\n");
    AnnotationStore store;
    store.add(std::make_unique<BoundingBox>(5, 0, 0, 1, 1));
    std::string error;
    EXPECT_FALSE(loadAnnotationCsv((dir.path / "bad.csv").string(), store, 10, 10, error));
    EXPECT_EQ(store.totalCount(), 1u);

    write(dir.path / "outside.csv", "timestamp,x,y\n100,10,2\n");
    EXPECT_FALSE(loadAnnotationCsv((dir.path / "outside.csv").string(), store, 10, 10, error));
    EXPECT_EQ(store.totalCount(), 1u);
}

TEST(AnnotationCsvTest, PointAtSameTimestampIsReplaced) {
    TempDir dir;
    write(dir.path / "points.csv", "timestamp,x,y\n100,8,9\n");
    AnnotationStore store;
    store.add(std::make_unique<PointAnnotation>(100, 1, 2));
    store.add(std::make_unique<BoundingBox>(100, 0, 0, 1, 1));
    std::string error;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "points.csv").string(), store, 20, 20, error));
    EXPECT_EQ(store.totalCount(), 2u);
    const auto* annotations = store.queryAt(100);
    ASSERT_NE(annotations, nullptr);
    for (const auto& annotation : *annotations) {
        if (const auto* point = dynamic_cast<const PointAnnotation*>(annotation.get())) {
            EXPECT_FLOAT_EQ(point->x(), 8.f);
            EXPECT_FLOAT_EQ(point->y(), 9.f);
        }
    }
}

TEST(AnnotationCsvTest, SavesOneCsvPerNonEmptyTypeAndRoundTripsLabels) {
    TempDir dir;
    AnnotationStore store;
    store.add(std::make_unique<PointAnnotation>(100, 1.5f, 2.5f));
    store.add(std::make_unique<EyeTracking>(200, .1f, -.2f, 3, 4, 5));
    store.add(std::make_unique<BoundingBox>(300, 1, 2, 3, 4, "a, \"label\""));
    std::vector<std::string> written;
    std::string error;
    ASSERT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                       written, error)) << error;
    EXPECT_EQ(written.size(), 3u);
    EXPECT_TRUE(std::filesystem::exists(dir.path / "clip_points.csv"));
    EXPECT_TRUE(std::filesystem::exists(dir.path / "clip_eye_tracking.csv"));
    EXPECT_TRUE(std::filesystem::exists(dir.path / "clip_bounding_boxes.csv"));

    std::vector<std::string> second_write;
    EXPECT_FALSE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                        second_write, error));
    EXPECT_NE(error.find("already exists"), std::string::npos);
    EXPECT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, true,
                                       second_write, error));

    AnnotationStore restored;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_bounding_boxes.csv").string(),
                                  restored, 20, 20, error)) << error;
    const auto* annotations = restored.queryAt(300);
    ASSERT_NE(annotations, nullptr);
    const auto* box = dynamic_cast<const BoundingBox*>((*annotations)[0].get());
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->label(), "a, \"label\"");
}

TEST(AnnotationCsvTest, SavesInterpolatedRowsAtRequestedFpsWithoutDuplicates) {
    TempDir dir;
    AnnotationStore store;
    store.add(std::make_unique<PointAnnotation>(0, 0.f, 0.f));
    store.add(std::make_unique<PointAnnotation>(500'000, 5.f, 10.f));
    store.add(std::make_unique<PointAnnotation>(1'000'000, 10.f, 20.f));
    std::vector<std::string> written;
    std::string error;
    AnnotationCsvSaveSettings settings;
    settings.include_interpolated = true;
    settings.interpolation_fps = 4;
    ASSERT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                       written, error, settings)) << error;
    EXPECT_EQ(read(dir.path / "clip_points.csv"),
              "timestamp,x,y,label\n"
              "0,0,0,\n"
              "250000,2.5,5,\n"
              "500000,5,10,\n"
              "750000,7.5,15,\n"
              "1000000,10,20,\n");

    AnnotationStore restored;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_points.csv").string(),
                                  restored, 100, 100, error)) << error;
    EXPECT_EQ(restored.totalCount(), 5u);
}

TEST(AnnotationCsvTest, KeyframeOnlySaveRemainsUnchanged) {
    TempDir dir;
    AnnotationStore store;
    store.add(std::make_unique<PointAnnotation>(0, 1.f, 2.f));
    store.add(std::make_unique<PointAnnotation>(1'000'000, 3.f, 4.f));
    std::vector<std::string> written;
    std::string error;
    ASSERT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                       written, error)) << error;
    EXPECT_EQ(read(dir.path / "clip_points.csv"),
              "timestamp,x,y,label\n0,1,2,\n1000000,3,4,\n");
}

TEST(AnnotationCsvTest, InterpolationEndpointsRoundTripAsEmptyRows) {
    TempDir dir;
    AnnotationStore store;
    store.setInterpolationEndpoint(100, AnnotationKind::kPoint);
    store.setInterpolationEndpoint(200, AnnotationKind::kEyeTracking);
    store.setInterpolationEndpoint(300, AnnotationKind::kBoundingBox);
    std::vector<std::string> written;
    std::string error;
    ASSERT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                       written, error)) << error;
    EXPECT_EQ(read(dir.path / "clip_points.csv"),
              "timestamp,x,y,label\n100,,,\n");
    EXPECT_EQ(read(dir.path / "clip_eye_tracking.csv"),
              "timestamp,phi,theta,center_x,center_y,radius,label\n200,,,,,,\n");
    EXPECT_EQ(read(dir.path / "clip_bounding_boxes.csv"),
              "timestamp,x,y,w,h,label\n300,,,,,\n");

    AnnotationStore restored;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_points.csv").string(),
                                  restored, 10, 10, error)) << error;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_eye_tracking.csv").string(),
                                  restored, 10, 10, error)) << error;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_bounding_boxes.csv").string(),
                                  restored, 10, 10, error)) << error;
    EXPECT_EQ(restored.totalCount(), 3u);
}

TEST(AnnotationCsvTest, DiscoveryUsesTypedStemAndReportsAmbiguity) {
    TempDir dir;
    write(dir.path / "clip.mkv", "");
    write(dir.path / "clip_points.data", "timestamp,x,y\n");
    write(dir.path / "clip_eye_tracking.csv",
          "timestamp,phi,theta,center_x,center_y,radius\n");
    write(dir.path / "other_points.csv", "timestamp,x,y\n");
    std::vector<std::string> warnings;
    auto found = discoverAnnotationCsvFiles((dir.path / "clip.mkv").string(), warnings);
    EXPECT_EQ(found.size(), 2u);
    EXPECT_TRUE(warnings.empty());

    write(dir.path / "clip_points.csv", "timestamp,x,y\n");
    found = discoverAnnotationCsvFiles((dir.path / "clip.mkv").string(), warnings);
    EXPECT_EQ(found.size(), 1u);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("Ambiguous points"), std::string::npos);
}

TEST(AnnotationCsvTest, SourceSpecificOverridesFolderLevel) {
    TempDir dir;
    write(dir.path / "clip.mkv", "");
    write(dir.path / "clip_points.data", "timestamp,x,y\n");
    write(dir.path / "points.csv", "timestamp,x,y\n");
    write(dir.path / "eye_tracking.data",
          "timestamp,phi,theta,center_x,center_y,radius\n");
    std::vector<std::string> warnings;
    const auto found = discoverAnnotationCsvFiles(
        (dir.path / "clip.mkv").string(), warnings);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(std::filesystem::path(found[0]).filename(), "clip_points.data");
    EXPECT_EQ(std::filesystem::path(found[1]).filename(), "eye_tracking.data");
    EXPECT_TRUE(warnings.empty());
}

TEST(AnnotationCsvTest, DirectSelectionResolvesSpecificAndFolderLevelNames) {
    TempDir dir;
    write(dir.path / "clip.mkv", "");
    write(dir.path / "clip_points.csv", "timestamp,x,y\n");
    write(dir.path / "points.csv", "timestamp,x,y\n");
    std::string target;
    std::string error;
    EXPECT_TRUE(resolveDirectAnnotationSelection(
        (dir.path / "clip_points.csv").string(), target, error));
    EXPECT_EQ(std::filesystem::path(target), dir.path / "clip.mkv");
    EXPECT_TRUE(resolveDirectAnnotationSelection(
        (dir.path / "points.csv").string(), target, error));
    EXPECT_EQ(std::filesystem::path(target), dir.path);

    write(dir.path / "clip.mp4", "");
    error.clear();
    EXPECT_FALSE(resolveDirectAnnotationSelection(
        (dir.path / "clip_points.csv").string(), target, error));
    EXPECT_NE(error.find("Ambiguous"), std::string::npos);

    write(dir.path / "experiment_points.csv", "timestamp,x,y\n");
    error.clear();
    EXPECT_FALSE(resolveDirectAnnotationSelection(
        (dir.path / "experiment_points.csv").string(), target, error));
    EXPECT_TRUE(error.empty());
}

TEST(AnnotationCsvTest, LabeledPointAndEyeSchemasRoundTrip) {
    TempDir dir;
    AnnotationStore store;
    store.add(std::make_unique<PointAnnotation>(10, 1.f, 2.f, "p, one"));
    store.add(std::make_unique<EyeTracking>(20, .1f, .2f, 3.f, 4.f, 2.f, "left eye"));
    std::vector<std::string> written;
    std::string error;
    ASSERT_TRUE(saveAnnotationCsvFiles(dir.path.string(), "clip", store, false,
                                       written, error)) << error;
    AnnotationStore restored;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_points.csv").string(), restored,
                                  20, 20, error)) << error;
    ASSERT_TRUE(loadAnnotationCsv((dir.path / "clip_eye_tracking.csv").string(), restored,
                                  20, 20, error)) << error;
    EXPECT_TRUE(restored.contains(10, "Point", "p, one"));
    EXPECT_TRUE(restored.contains(20, "EyeTracking", "left eye"));
}
