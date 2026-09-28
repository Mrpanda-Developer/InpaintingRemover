#pragma once

#include "subtitle_remover/config.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace subtitle_remover {

struct SubtitleRegion {
    cv::Rect bounds;
    float confidence = 0.0F;
};

class ISubtitleDetector {
public:
    virtual ~ISubtitleDetector() = default;
    virtual std::vector<SubtitleRegion> detect(const cv::Mat& frame) = 0;
};

class OpenCvSubtitleDetector final : public ISubtitleDetector {
public:
    explicit OpenCvSubtitleDetector(SubtitleRegion region = SubtitleRegion::Bottom35)
        : region_(region) {}
    std::vector<SubtitleRegion> detect(const cv::Mat& frame) override;

private:
    SubtitleRegion region_;
};

class IOpticalFlow {
public:
    virtual ~IOpticalFlow() = default;
    virtual void estimate(const cv::Mat& previous, const cv::Mat& current,
                          cv::Mat& flow) = 0;
};

class OpenCvOpticalFlow final : public IOpticalFlow {
public:
    void estimate(const cv::Mat& previous, const cv::Mat& current,
                  cv::Mat& flow) override;
};

class IInpainter {
public:
    virtual ~IInpainter() = default;
    virtual void inpaint(cv::Mat& frame, const cv::Mat& mask) = 0;
};

class OpenCvInpainter final : public IInpainter {
public:
    void inpaint(cv::Mat& frame, const cv::Mat& mask) override;
};

class OnnxInpainter final : public IInpainter {
public:
    explicit OnnxInpainter(const std::filesystem::path& modelPath,
                           Device device = Device::Cpu);
    void inpaint(cv::Mat& frame, const cv::Mat& mask) override;
};

struct SubtitleEventReport {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float confidence = 0.0F;
    std::string method;
    bool requiresReview = true;
};

} // namespace subtitle_remover