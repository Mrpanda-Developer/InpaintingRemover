#pragma once

#include <opencv2/core.hpp>

namespace subtitle_remover {

struct ConfidenceMetrics {
    float overall = 0.0F;
    float unresolvedFraction = 1.0F;
    float disagreement = 1.0F;
    float inpaintedFraction = 0.0F;
    bool requiresReview = true;
};

class ConfidenceEvaluator {
public:
    ConfidenceMetrics evaluate(const cv::Mat& originalMask,
                               const cv::Mat& unresolvedMask,
                               const cv::Mat& confidenceMap,
                               const cv::Mat& inpaintedMask) const;
};

} // namespace subtitle_remover