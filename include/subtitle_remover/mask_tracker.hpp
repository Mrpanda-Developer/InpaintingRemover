#pragma once

#include "subtitle_remover/processing_interfaces.hpp"

namespace subtitle_remover {

class MaskTracker {
public:
    explicit MaskTracker(IOpticalFlow& opticalFlow) : opticalFlow_(opticalFlow) {}
    cv::Mat propagate(const cv::Mat& previousFrame, const cv::Mat& currentFrame,
                      const cv::Mat& previousMask);

private:
    IOpticalFlow& opticalFlow_;
};

} // namespace subtitle_remover