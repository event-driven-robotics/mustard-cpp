#include "mustard/ui/RGBFrameConversion.h"

#include <gtest/gtest.h>
#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

using namespace mustard;

TEST(RGBFrameConversionTest, ConvertsBgrToRgba) {
    cv::Mat frame(1, 1, CV_8UC3);
    frame.at<cv::Vec3b>(0, 0) = cv::Vec3b{10, 20, 30};

    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(convertFrameToRgba(frame, w, h, rgba));

    EXPECT_EQ(w, 1);
    EXPECT_EQ(h, 1);
    ASSERT_EQ(rgba.size(), 4u);
    EXPECT_EQ(rgba[0], 30);
    EXPECT_EQ(rgba[1], 20);
    EXPECT_EQ(rgba[2], 10);
    EXPECT_EQ(rgba[3], 255);
}

TEST(RGBFrameConversionTest, ConvertsGrayToRgba) {
    cv::Mat frame(1, 1, CV_8UC1);
    frame.at<uint8_t>(0, 0) = 42;

    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    ASSERT_TRUE(convertFrameToRgba(frame, w, h, rgba));

    EXPECT_EQ(w, 1);
    EXPECT_EQ(h, 1);
    ASSERT_EQ(rgba.size(), 4u);
    EXPECT_EQ(rgba[0], 42);
    EXPECT_EQ(rgba[1], 42);
    EXPECT_EQ(rgba[2], 42);
    EXPECT_EQ(rgba[3], 255);
}

TEST(RGBFrameConversionTest, RejectsInvalidFrameWithoutChangingOutput) {
    cv::Mat frame;
    int w = 7;
    int h = 9;
    std::vector<uint8_t> rgba{1, 2, 3};

    EXPECT_FALSE(convertFrameToRgba(frame, w, h, rgba));
    EXPECT_EQ(w, 7);
    EXPECT_EQ(h, 9);
    EXPECT_EQ(rgba, (std::vector<uint8_t>{1, 2, 3}));
}
