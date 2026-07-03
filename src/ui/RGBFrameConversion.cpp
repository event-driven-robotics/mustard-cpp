#include "mustard/ui/RGBFrameConversion.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace mustard {

bool convertFrameToRgba(const cv::Mat& frame, int& width, int& height,
                        std::vector<uint8_t>& rgba) {
    if (frame.empty() || frame.cols <= 0 || frame.rows <= 0) {
        return false;
    }

    cv::Mat converted;
    switch (frame.channels()) {
        case 1:
            cv::cvtColor(frame, converted, cv::COLOR_GRAY2RGBA);
            break;
        case 3:
            cv::cvtColor(frame, converted, cv::COLOR_BGR2RGBA);
            break;
        case 4:
            cv::cvtColor(frame, converted, cv::COLOR_BGRA2RGBA);
            break;
        default:
            return false;
    }

    width = converted.cols;
    height = converted.rows;
    const std::size_t bytes = converted.total() * converted.elemSize();
    rgba.assign(converted.data, converted.data + bytes);
    return true;
}

} // namespace mustard
