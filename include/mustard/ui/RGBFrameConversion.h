#pragma once

#include <cstdint>
#include <vector>

namespace cv {
class Mat;
}

namespace mustard {

bool convertFrameToRgba(const cv::Mat& frame, int& width, int& height,
                        std::vector<uint8_t>& rgba);

} // namespace mustard
