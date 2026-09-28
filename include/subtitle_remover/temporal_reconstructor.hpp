#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace subtitle_remover {

struct ReconstructionResult {
    cv::Mat frame;
    cv::Mat unresolvedMask;
    cv::Mat confidence;
};

class TemporalReconstructor {
public:
    ReconstructionResult reconstruct(const cv::Mat& currentFrame,
                                     const cv::Mat& subtitleMask,
                                     const std::vector<cv::Mat>& alignedCandidates,
                                     float agreementThreshold = 0.8F) const;
};

} // namespace subtitle_remover