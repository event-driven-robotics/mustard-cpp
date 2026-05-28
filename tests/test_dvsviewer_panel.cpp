// tests/test_dvsviewer_panel.cpp
//
// Unit tests for mustard::DVSViewerPanel.
//
// ALL tests exercise only the guard-1 path (nullptr stream), which causes
// onTimeChanged() to return immediately before any OpenGL function is reached.
// The destructor is also safe because texture_id_ starts at 0 and is only
// written inside ensureTexture(), which is never called here.
//
// No display server, no GL context, and no file I/O are required.

#include "mustard/ui/DVSViewerPanel.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>

using namespace mustard;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class DVSViewerPanelTest : public ::testing::Test {};

// ---------------------------------------------------------------------------
// Static constant
// ---------------------------------------------------------------------------

TEST_F(DVSViewerPanelTest, AccumWindowConstantIs33333) {
    EXPECT_EQ(DVSViewerPanel::kAccumWindowUs, int64_t{33'333});
}

// ---------------------------------------------------------------------------
// label() accessor
// ---------------------------------------------------------------------------

TEST_F(DVSViewerPanelTest, LabelIsStored) {
    DVSViewerPanel panel(nullptr, "cam0");
    EXPECT_EQ(panel.label(), "cam0");
}

TEST_F(DVSViewerPanelTest, LabelWithImguiSuffix) {
    DVSViewerPanel panel(nullptr, "DVS Camera##cam0");
    EXPECT_EQ(panel.label(), "DVS Camera##cam0");
}

TEST_F(DVSViewerPanelTest, LabelEmptyString) {
    DVSViewerPanel panel(nullptr, "");
    EXPECT_EQ(panel.label(), "");
}

TEST_F(DVSViewerPanelTest, LabelWithSpecialCharacters) {
    const std::string lbl = "/sensors/dvs_left [0]";
    DVSViewerPanel panel(nullptr, lbl);
    EXPECT_EQ(panel.label(), lbl);
}

// ---------------------------------------------------------------------------
// onTimeChanged — guard 1: !stream_ (nullptr), no GL path reachable
// ---------------------------------------------------------------------------

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamNoCrash) {
    DVSViewerPanel panel(nullptr, "test");
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(0));
}

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamRepeated) {
    DVSViewerPanel panel(nullptr, "test");
    for (int64_t t = 0; t < 1'000'000; t += 100'000) {
        EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(t));
    }
}

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamNegativeTime) {
    DVSViewerPanel panel(nullptr, "test");
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(-1));
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(std::numeric_limits<int64_t>::min()));
}

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamMaxTime) {
    DVSViewerPanel panel(nullptr, "test");
    EXPECT_NO_FATAL_FAILURE(
        panel.onTimeChanged(std::numeric_limits<int64_t>::max()));
}

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamSameTimestampTwice) {
    DVSViewerPanel panel(nullptr, "test");
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(42));
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(42));
}

TEST_F(DVSViewerPanelTest, OnTimeChangedNullStreamZeroThenNegative) {
    DVSViewerPanel panel(nullptr, "test");
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(500'000));
    EXPECT_NO_FATAL_FAILURE(panel.onTimeChanged(0));
}

// ---------------------------------------------------------------------------
// Destructor safety
// ---------------------------------------------------------------------------

TEST_F(DVSViewerPanelTest, DestructorNullStreamNoCrash) {
    // texture_id_ == 0 at construction → ~DVSViewerPanel must NOT call
    // glDeleteTextures, making destruction safe without a GL context
    {
        DVSViewerPanel panel(nullptr, "dtortest");
    }
    SUCCEED();
}

TEST_F(DVSViewerPanelTest, DestructorAfterRepeatedOnTimeChangedNullStream) {
    // Even after many onTimeChanged calls (all no-ops via guard 1),
    // texture_id_ stays 0 and the destructor must remain GL-free
    {
        DVSViewerPanel panel(nullptr, "dtortest2");
        panel.onTimeChanged(0);
        panel.onTimeChanged(10'000);
        panel.onTimeChanged(20'000);
    }
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Move semantics (constructor and assignment are = default)
// ---------------------------------------------------------------------------

TEST_F(DVSViewerPanelTest, MoveConstructorPreservesLabel) {
    DVSViewerPanel src(nullptr, "movable");
    DVSViewerPanel dst(std::move(src));
    EXPECT_EQ(dst.label(), "movable");
}

TEST_F(DVSViewerPanelTest, MoveAssignmentPreservesLabel) {
    DVSViewerPanel src(nullptr, "src_label");
    DVSViewerPanel dst(nullptr, "dst_label");
    dst = std::move(src);
    EXPECT_EQ(dst.label(), "src_label");
}

TEST_F(DVSViewerPanelTest, MovedFromSourceIsDestructibleWithoutCrash) {
    {
        DVSViewerPanel src(nullptr, "moved_from");
        DVSViewerPanel dst(std::move(src));
        (void)dst;
    }
    SUCCEED();
}

TEST_F(DVSViewerPanelTest, MoveConstructedPanelHandlesOnTimeChangedSafely) {
    DVSViewerPanel src(nullptr, "original");
    DVSViewerPanel dst(std::move(src));
    EXPECT_NO_FATAL_FAILURE(dst.onTimeChanged(0));
}
