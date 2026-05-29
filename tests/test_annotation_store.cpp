// tests/test_annotation_store.cpp
//
// Unit tests for mustard::AnnotationStore and mustard::BoundingBox.
//
// No OpenGL context, no display server, and no file I/O are required.
// BoundingBox::renderOverlay is never called; imgui symbols are resolved
// at link time (via the imgui CMake target) but never executed.

#include "mustard/annotation/AnnotationStore.h"
#include "mustard/annotation/BoundingBox.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace mustard;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static std::unique_ptr<BoundingBox> makeBox(int64_t t,
                                            float x = 0.f, float y = 0.f,
                                            float w = 10.f, float h = 5.f,
                                            const std::string& label = "") {
    return std::make_unique<BoundingBox>(t, x, y, w, h, label);
}

// ---------------------------------------------------------------------------
// Test 1 — add / queryAt exact match
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, AddAndQueryAt) {
    AnnotationStore store;
    store.add(makeBox(1000, 10.f, 20.f, 50.f, 30.f, "car"));

    const auto* anns = store.queryAt(1000);
    ASSERT_NE(anns, nullptr);
    ASSERT_EQ(anns->size(), 1u);
    EXPECT_EQ((*anns)[0]->timestamp(), 1000);
}

// ---------------------------------------------------------------------------
// Test 2 — queryAt on a timestamp with no annotations returns nullptr
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, QueryAtMissReturnsNullptr) {
    AnnotationStore store;
    store.add(makeBox(1000));

    EXPECT_EQ(store.queryAt(999),  nullptr);
    EXPECT_EQ(store.queryAt(1001), nullptr);
    EXPECT_EQ(store.queryAt(0),    nullptr);
}

// ---------------------------------------------------------------------------
// Test 3 — queryRange is half-open [t0, t1)
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, QueryRangeIncludesMatches) {
    AnnotationStore store;
    store.add(makeBox(0));     // included  (== t0)
    store.add(makeBox(500));   // included
    store.add(makeBox(999));   // included  (< t1)
    store.add(makeBox(1000));  // excluded  (== t1)

    const auto result = store.queryRange(0, 1000);
    EXPECT_EQ(result.size(), 3u);
}

// ---------------------------------------------------------------------------
// Test 4 — remove shifts surviving elements; empty bucket disappears
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, RemoveAnnotation) {
    AnnotationStore store;
    store.add(makeBox(100, 0.f, 0.f, 1.f, 1.f, "a"));
    store.add(makeBox(100, 5.f, 5.f, 2.f, 2.f, "b"));

    store.remove(100, 0); // remove "a"

    const auto* anns = store.queryAt(100);
    ASSERT_NE(anns, nullptr);
    ASSERT_EQ(anns->size(), 1u);

    const auto* bb = dynamic_cast<const BoundingBox*>((*anns)[0].get());
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->label(), "b");
}

// ---------------------------------------------------------------------------
// Test 5 — totalCount sums across all timestamps
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, TotalCount) {
    AnnotationStore store;
    store.add(makeBox(0));
    store.add(makeBox(0));   // two at t=0
    store.add(makeBox(500)); // one at t=500

    EXPECT_EQ(store.totalCount(), 3u);
}

// ---------------------------------------------------------------------------
// Test 6 — clear() empties everything
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, ClearEmptiesStore) {
    AnnotationStore store;
    store.add(makeBox(0));
    store.add(makeBox(500));
    store.clear();

    EXPECT_EQ(store.totalCount(), 0u);
    EXPECT_EQ(store.queryAt(0),   nullptr);
    EXPECT_EQ(store.queryAt(500), nullptr);
}

// ---------------------------------------------------------------------------
// Test 7 — serialize / deserialize round-trip (single annotation)
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, SerializeDeserializeRoundTrip) {
    AnnotationStore store;
    store.add(makeBox(1000, 10.f, 20.f, 50.f, 30.f, "pedestrian"));

    const std::string data = store.serialize();

    AnnotationStore store2;
    ASSERT_TRUE(store2.deserialize(data));

    const auto* anns = store2.queryAt(1000);
    ASSERT_NE(anns, nullptr);
    ASSERT_EQ(anns->size(), 1u);

    const auto* bb = dynamic_cast<const BoundingBox*>((*anns)[0].get());
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->timestamp(), 1000);
    EXPECT_FLOAT_EQ(bb->x(),  10.f);
    EXPECT_FLOAT_EQ(bb->y(),  20.f);
    EXPECT_FLOAT_EQ(bb->w(),  50.f);
    EXPECT_FLOAT_EQ(bb->h(),  30.f);
    EXPECT_EQ(bb->label(), "pedestrian");
}

// ---------------------------------------------------------------------------
// Test 8 — serialize / deserialize preserves count with multiple annotations
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, DeserializeMultiple) {
    AnnotationStore store;
    store.add(makeBox(100, 1.f, 2.f,  3.f, 4.f, "a"));
    store.add(makeBox(200, 5.f, 6.f,  7.f, 8.f, "b"));
    store.add(makeBox(300, 9.f, 0.f, 11.f, 1.f, "c"));

    const std::string data = store.serialize();

    AnnotationStore store2;
    ASSERT_TRUE(store2.deserialize(data));
    EXPECT_EQ(store2.totalCount(), 3u);
}

// ---------------------------------------------------------------------------
// Test 9 — BoundingBox accessors return constructed values
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, BoundingBoxAccessors) {
    const BoundingBox bb(42, 1.5f, 2.5f, 10.f, 20.f, "test_label");

    EXPECT_EQ(bb.timestamp(), 42);
    EXPECT_FLOAT_EQ(bb.x(),  1.5f);
    EXPECT_FLOAT_EQ(bb.y(),  2.5f);
    EXPECT_FLOAT_EQ(bb.w(), 10.f);
    EXPECT_FLOAT_EQ(bb.h(), 20.f);
    EXPECT_EQ(bb.label(), "test_label");
}

// ---------------------------------------------------------------------------
// Test 10 — BoundingBox::typeName()
// ---------------------------------------------------------------------------

TEST(AnnotationStoreTest, BoundingBoxTypeName) {
    const BoundingBox bb(0, 0.f, 0.f, 1.f, 1.f);
    EXPECT_EQ(bb.typeName(), "BoundingBox");
}
