#pragma once

#include "subtitle_remover/config.hpp"

#include <opencv2/core.hpp>

namespace subtitle_remover {

class MaskRefiner {
public:
    explicit MaskRefiner(int padding, int dilation) : padding_(padding), dilation_(dilation) {}
    cv::Mat refine(const cv::Mat& mask) const;

private:
    int padding_;
    int dilation_;
};

} // namespace subtitle_remover